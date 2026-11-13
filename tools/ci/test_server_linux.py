#!/usr/bin/env python3
"""Boot the packaged JAR in disposable Linux containers; never read development data."""
import argparse
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import secrets
import select
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class SmokeFailure(Exception):
    pass


def command(*args, timeout=45):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        # Child output may contain configuration; report only the fixed command category.
        raise SmokeFailure('command_failed:' + Path(str(args[0])).name)
    return result.stdout.strip()


def run(bundle, java_image, auth_rotation=False, memory_rotation=False):
    auth_rotation = auth_rotation or memory_rotation
    raw = bundle.read_bytes()
    containers = []
    volumes = []
    network = 'pet-server-smoke-' + uuid.uuid4().hex[:12]
    def cleanup_containers():
        failed = False
        for container in reversed(containers):
            result = subprocess.run(['docker', 'rm', '-f', container], capture_output=True, timeout=30)
            failed = failed or result.returncode != 0
        for volume in volumes:
            result = subprocess.run(['docker', 'volume', 'rm', volume], capture_output=True, timeout=30)
            failed = failed or result.returncode != 0
        if failed:
            raise SmokeFailure('container_cleanup_failed')
    command('docker', 'network', 'create', network)
    try:
        with contextlib.ExitStack() as lifecycle:
            temporary = lifecycle.enter_context(tempfile.TemporaryDirectory(prefix='pet-server-linux-'))
            lifecycle.callback(cleanup_containers)
            stage = Path(temporary)
            frozen = stage / 'server.zip'
            frozen.write_bytes(raw)
            delivery = json.loads(command(sys.executable, ROOT / 'tools/server_release.py', 'verify', '--bundle', frozen))
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                archive.extractall(stage)
            core = stage / 'core'
            # Only the copied initializer sees the disposable data directory.
            command(sys.executable, core / 'tools/init_local.py', '--host', 'mqtt', '--device', 'isolated-pet', timeout=90)
            local = dict(line.split('=', 1) for line in (core / 'data/local.env').read_text().splitlines()
                         if line and not line.startswith('#'))

            def env_file(name, values):
                path = stage / name
                path.write_text(''.join(key + '=' + value + '\n' for key, value in values.items()))
                path.chmod(0o600)
                return path

            postgres_env = env_file('postgres.env', {'POSTGRES_DB': 'nexus', 'POSTGRES_USER': 'nexus',
                                                   'POSTGRES_PASSWORD': local['NEXUS_DB_PASSWORD']})
            core_values = {
                'NEXUS_ADMIN_TOKEN': local['NEXUS_ADMIN_TOKEN'],
                'NEXUS_DB_URL': 'jdbc:postgresql://database:5432/nexus', 'NEXUS_DB_USER': 'nexus',
                'NEXUS_DB_PASSWORD': local['NEXUS_DB_PASSWORD'], 'NEXUS_BIND_ADDRESS': '127.0.0.1',
                'NEXUS_MQTT_URI': 'ssl://mqtt:8883', 'NEXUS_MQTT_USERNAME': 'nexus-core',
                'NEXUS_MQTT_PASSWORD': local['NEXUS_MQTT_PASSWORD'], 'NEXUS_MQTT_CA_CERT': '/test-ca.crt',
                'NEXUS_MQTT_ALLOW_INSECURE': 'false', 'NEXUS_HALO_ENABLED': 'false',
                'NEXUS_AI_ENABLED': 'false', 'NEXUS_MEMORY_ENABLED': 'false',
                'NEXUS_MEMORY_MCP_ENABLED': 'false', 'NEXUS_BRIDGE_ENABLED': 'false', 'NEXUS_RETENTION_ENABLED': 'false'}
            if memory_rotation:
                core_values.update({'NEXUS_HALO_ENABLED': 'true', 'NEXUS_HALO_URL': 'http://127.0.0.1:19099',
                    'NEXUS_HALO_TOKEN': secrets.token_urlsafe(48), 'NEXUS_MEMORY_ENABLED': 'true',
                    'NEXUS_MEMORY_MCP_ENABLED': 'true', 'NEXUS_MEMORY_MCP_TOKEN': secrets.token_urlsafe(48),
                    'NEXUS_MEMORY_URL': 'http://127.0.0.1:19099', 'NEXUS_MEMORY_API_KEY': secrets.token_urlsafe(48),
                    'NEXUS_MEMORY_ACCOUNT': 'isolated-account', 'NEXUS_MEMORY_USER': 'isolated-user',
                    'NEXUS_MEMORY_PEER': 'linux-rotation'})
            core_env = env_file('core.env', core_values)
            cli = stage / 'desktop-pet/tools/server_release.py'
            ready = json.loads(command(sys.executable, cli, 'preflight', '--env-file', core_env))
            assert ready['ok']
            assert {key for key, value in ready['features'].items() if value} == (
                {'HALO', 'MEMORY', 'MEMORY_MCP'} if memory_rotation else set())

            def start(*args):
                container = command('docker', 'run', '-d', *args)
                containers.append(container)
                return container

            auth_mounts = []
            runtime_env = core_env
            helper = None
            client_mounts = []
            python_image = 'python:3.13-alpine'
            if auth_rotation:
                if not (core / 'tools/credential_store.py').is_file():
                    raise SmokeFailure('bundle_has_no_credential_rotation_cli')
                volume = network + '-auth'
                command('docker', 'volume', 'create', '--driver', 'local', '--opt', 'type=tmpfs', '--opt', 'device=tmpfs',
                        '--opt', 'o=size=1m,uid=65534,gid=65534,mode=0700', volume)
                volumes.append(volume)
                if memory_rotation:
                    client_volume = network + '-client'
                    command('docker', 'volume', 'create', '--driver', 'local', '--opt', 'type=tmpfs', '--opt', 'device=tmpfs',
                            '--opt', 'o=size=1m,uid=65534,gid=65534,mode=0700', client_volume)
                    volumes.append(client_volume)
                    client_mounts = ['-v', client_volume + ':/client']
                command('docker', 'pull', python_image, timeout=180)
                helper_env = env_file('auth-helper.env', {'NEXUS_ADMIN_TOKEN': local['NEXUS_ADMIN_TOKEN'],
                    'NEXUS_MEMORY_MCP_TOKEN': secrets.token_urlsafe(48)})
                helper = start('--network', 'none', '--read-only', '--tmpfs', '/tmp', '--user', '65534:65534',
                    '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges', '--env-file', helper_env,
                    '-v', volume + ':/auth', *client_mounts,
                    '-v', str(core / 'tools/credential_store.py') + ':/tools/credential_store.py:ro',
                    python_image, 'python3', '-c', 'import time;time.sleep(900)')
                command('docker', 'exec', helper, 'python3', '/tools/credential_store.py', 'init', '--file', '/auth/tokens.json')
                runtime_env = env_file('core-auth.env', {**core_values, 'NEXUS_AUTH_TOKEN_FILE': '/auth/tokens.json'})
                auth_mounts = ['-v', volume + ':/auth:ro']

            database = start('--network', network, '--network-alias', 'database', '--env-file', postgres_env,
                             '--tmpfs', '/var/lib/postgresql/data:rw,size=256m', 'postgres:17-alpine')
            start('--network', network, '--network-alias', 'mqtt', '--read-only', '--tmpfs', '/tmp',
                  '-v', str(core / 'data/mosquitto/config') + ':/mosquitto/config:ro',
                  '-v', str(core / 'data/mosquitto/certs') + ':/mosquitto/certs:ro', 'eclipse-mosquitto:2.0.22')
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                result = subprocess.run(['docker', 'exec', database, 'pg_isready', '-U', 'nexus', '-d', 'nexus'],
                                        capture_output=True, timeout=5)
                if result.returncode == 0:
                    break
                time.sleep(.5)
            else:
                raise SmokeFailure('database_start_timeout')
            jar = core / 'nexus-core.jar'
            jar.chmod(0o644)
            runtime = start('--network', network, '--read-only', '--tmpfs', '/tmp:rw,size=32m',
                            '--user', '65534:65534', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                            '--memory', '768m', '--env-file', runtime_env, '-p', '127.0.0.1::9080',
                            '-v', str(jar) + ':/app/nexus-core.jar:ro',
                            '-v', str(core / 'data/mosquitto/certs/ca.crt') + ':/test-ca.crt:ro',
                            *auth_mounts,
                            java_image, 'java', '-jar', '/app/nexus-core.jar',
                            *(('--nexus.halo.allow-insecure=true', '--nexus.halo.poll-ms=600000') if memory_rotation else ()))
            port = command('docker', 'port', runtime, '9080/tcp').rsplit(':', 1)[1]
            url = 'http://127.0.0.1:' + port

            def proxy():
                return start('--network', 'container:' + runtime, '--read-only', '--tmpfs', '/data', '--tmpfs', '/config',
                             '-e', 'NEXUS_CONSOLE_DOMAIN=http://:9080',
                             '-v', str(stage / 'desktop-pet/deployment/Caddyfile') + ':/etc/caddy/Caddyfile:ro',
                             'caddy:2.10.2-alpine', 'caddy', 'run', '--config', '/etc/caddy/Caddyfile', '--adapter', 'caddyfile')

            def wait_ready():
                deadline = time.monotonic() + 70
                while time.monotonic() < deadline:
                    probe = subprocess.run([sys.executable, str(cli), 'probe', '--env-file', str(core_env), '--url', url],
                                           capture_output=True, text=True, timeout=20)
                    if probe.returncode == 0:
                        return json.loads(probe.stdout)
                    if command('docker', 'inspect', '-f', '{{.State.Running}}', runtime) != 'true':
                        raise SmokeFailure('linux_core_exited')
                    time.sleep(1)
                raise SmokeFailure('linux_core_readiness_timeout')

            edge = proxy()
            initial = wait_ready()
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            rotation_evidence = {'tested': False}
            memory_evidence = {'tested': False}
            if auth_rotation:
                def credentials():
                    # Generated disposable tokens stay in this process; never put them in argv or evidence.
                    return json.loads(command('docker', 'exec', helper, 'python3', '-c',
                        "from pathlib import Path;print(Path('/auth/tokens.json').read_text())"))

                def credential_cli(*args):
                    return json.loads(command('docker', 'exec', helper, 'python3', '/tools/credential_store.py',
                        *args, '--file', '/auth/tokens.json'))

                def auth_status(token):
                    request = urllib.request.Request(url + '/api/v1/status', headers={'Authorization': 'Bearer ' + token})
                    try:
                        with opener.open(request, timeout=8) as response:
                            return response.status, json.loads(response.read(65536))
                    except urllib.error.HTTPError as response:
                        return response.code, json.loads(response.read(65536))

                def expect_status(token, status, label):
                    actual, body = auth_status(token)
                    if actual != status:
                        raise SmokeFailure('linux_auth_' + label)
                    return body

                before = json.loads(command('docker', 'inspect', runtime))[0]
                old = credentials()
                old_token = old['admin']['current']['token']
                expect_status(old_token, 200, 'initial')
                credential_cli('rotate', '--scope', 'admin', '--expected-revision', str(old['revision']), '--grace-seconds', '120')
                overlap = credentials(); current_token = overlap['admin']['current']['token']
                expect_status(old_token, 200, 'overlap_old');expect_status(current_token, 200, 'overlap_current')
                expect_status(overlap['memory']['current']['token'], 401, 'scope_isolation')
                credential_cli('revoke', '--scope', 'admin', '--expected-revision', str(overlap['revision']),
                    '--id', overlap['admin']['previous']['id'])
                expect_status(old_token, 401, 'revoked');expect_status(current_token, 200, 'after_revoke')
                revoked = credentials()
                credential_cli('rotate', '--scope', 'admin', '--expected-revision', str(revoked['revision']), '--grace-seconds', '1')
                expiring = credentials(); next_token = expiring['admin']['current']['token']
                expect_status(next_token, 200, 'expiry_current')
                deadline = time.monotonic() + 4
                while auth_status(current_token)[0] == 200 and time.monotonic() < deadline:
                    time.sleep(.1)
                expect_status(current_token, 401, 'expired');expect_status(next_token, 200, 'after_expiry')
                command('docker', 'exec', helper, 'python3', '-c',
                    "from pathlib import Path;import os;p=Path('/auth/tokens.json');b=p.with_name('valid.json');"
                    "b.write_bytes(p.read_bytes());b.chmod(0o600);t=p.with_name('invalid.json');t.write_text('{}');t.chmod(0o600);os.replace(t,p)")
                broken = expect_status(next_token, 503, 'malformed_file')
                if broken.get('error') != 'authentication_configuration_unavailable':
                    raise SmokeFailure('linux_auth_error_body')
                expect_status(old_token, 503, 'no_environment_fallback')
                command('docker', 'exec', helper, 'python3', '-c',
                    "import os;os.replace('/auth/valid.json','/auth/tokens.json')")
                expect_status(next_token, 200, 'repaired_file')
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/auth/tokens.json',0o644)")
                expect_status(next_token, 503, 'unsafe_permissions')
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/auth/tokens.json',0o600)")
                expect_status(next_token, 200, 'repaired_permissions')
                after = json.loads(command('docker', 'inspect', runtime))[0]
                if before['State']['StartedAt'] != after['State']['StartedAt'] or before['RestartCount'] != after['RestartCount']:
                    raise SmokeFailure('linux_auth_unexpected_restart')
                # The host probe cannot read the private Linux volume. Its temporary env file
                # carries only the current token, while the unchanged runtime uses file mode.
                core_env = env_file('probe-current.env', {**core_values, 'NEXUS_ADMIN_TOKEN': next_token})
                rotation_evidence = {'tested': True, 'scope': 'admin', 'linuxAtomicReplacement': True,
                    'overlap': True, 'expiry': True, 'revocation': True, 'scopeIsolation': True,
                    'malformedAndUnsafeFileRejected': True, 'repairWithoutRestart': True,
                    'runtimeRestartedDuringRotation': False, 'memoryMcpRotationTested': False}
            if memory_rotation:
                fixture_env = env_file('memory-fixture.env', {'NEXUS_MEMORY_API_KEY': core_values['NEXUS_MEMORY_API_KEY']})
                fixture = start('--network', 'container:' + runtime, '--read-only', '--tmpfs', '/tmp',
                    '--user', '65534:65534', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                    '--env-file', fixture_env, '-v', str(ROOT / 'tools/ci/server_memory_fixture.py') + ':/fixture.py:ro',
                    python_image, 'python3', '/fixture.py')

                def provider_count():
                    return json.loads(command('docker', 'exec', fixture, 'python3', '-c',
                        "import urllib.request;print(urllib.request.urlopen('http://127.0.0.1:19099/_test/counts',timeout=3).read().decode())"))['searches']

                deadline = time.monotonic() + 10
                while True:
                    try:
                        assert provider_count() == 0
                        break
                    except SmokeFailure:
                        if command('docker', 'inspect', '-f', '{{.State.Running}}', fixture) != 'true':
                            raise SmokeFailure('linux_memory_fixture_exited')
                        if time.monotonic() >= deadline:
                            raise SmokeFailure('linux_memory_fixture_readiness_timeout')
                        time.sleep(.1)

                # The real JAR performs its public memory search against this empty provider fixture.
                def mcp_status(token, method='ping', params=None):
                    payload = {'jsonrpc': '2.0', 'id': 'linux-http', 'method': method, 'params': params or {}}
                    request = urllib.request.Request(url + '/mcp/memory', data=json.dumps(payload).encode(),
                        headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json',
                            'Accept': 'application/json, text/event-stream', 'MCP-Protocol-Version': '2025-06-18'})
                    try:
                        response = opener.open(request, timeout=8)
                    except urllib.error.HTTPError as error:
                        response = error
                    with response:
                        return response.status, json.loads(response.read(65536))

                def expect_mcp(token, status, label):
                    actual, body = mcp_status(token)
                    if actual != status:
                        raise SmokeFailure('linux_memory_' + label)
                    if status == 200 and body.get('result') != {}:
                        raise SmokeFailure('linux_memory_ping_body')
                    return body

                search = {'name': 'memory.search', 'arguments': {'query': 'Linux rotation fixture', 'limit': 1}}
                def expect_search(body):
                    result = body.get('result', {})
                    if result.get('isError') is True:
                        code = result.get('content', [{}])[0].get('text')
                        if code in ('memory_not_configured', 'memory_search_busy', 'memory_search_unavailable'):
                            raise SmokeFailure('linux_memory_' + code)
                    if (result.get('isError') is not False
                            or result.get('structuredContent', {}).get('items') != []
                            or result.get('structuredContent', {}).get('dataPolicy') != 'verified_public_comments_only'):
                        raise SmokeFailure('linux_memory_search_body')

                original_memory = credentials()['memory']['current']['token']
                expect_mcp(original_memory, 200, 'initial')
                expect_mcp(next_token, 401, 'admin_scope_rejected')
                expect_status(original_memory, 401, 'memory_scope_rejected')
                assert provider_count() == 0
                code, body = mcp_status(original_memory, 'tools/call', search)
                if code != 200:
                    raise SmokeFailure('linux_memory_search_status')
                expect_search(body)
                assert provider_count() == 1
                credential_cli('export-memory', '--output', '/client/client.json')
                stdio_env = env_file('stdio.env', {'NEXUS_MEMORY_MCP_URL': 'http://127.0.0.1:8088/mcp/memory',
                    'NEXUS_MEMORY_MCP_TOKEN_FILE': '/client/client.json', 'NEXUS_MEMORY_MCP_TOKEN': original_memory})
                stdio_container = start('--network', 'container:' + runtime, '--read-only', '--tmpfs', '/tmp',
                    '--user', '65534:65534', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                    '--env-file', stdio_env, '-v', client_volume + ':/client:ro',
                    '-v', str(stage / 'core-tools/memory_mcp_stdio.py') + ':/tools/memory_mcp_stdio.py:ro',
                    python_image, 'python3', '-c', 'import time;time.sleep(900)')
                stderr = lifecycle.enter_context(tempfile.TemporaryFile())
                stdio = subprocess.Popen(['docker', 'exec', '-i', stdio_container, 'python3', '/tools/memory_mcp_stdio.py'],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr, bufsize=0)
                def close_stdio():
                    if stdio.stdin and not stdio.stdin.closed:
                        stdio.stdin.close()
                    try:
                        stdio.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        stdio.kill(); stdio.wait(timeout=5)
                    stdio.stdout.close()
                lifecycle.callback(close_stdio)
                stdio_output = bytearray()
                def rpc(identifier, method, params=None, error=False):
                    message = {'jsonrpc': '2.0', 'method': method, 'params': params or {}}
                    if identifier is not None:
                        message['id'] = identifier
                    stdio.stdin.write(json.dumps(message).encode() + b'\n')
                    stdio.stdin.flush()
                    if identifier is None:
                        return
                    line = bytearray(); deadline = time.monotonic() + 25
                    while not line.endswith(b'\n'):
                        remaining = deadline - time.monotonic()
                        if remaining <= 0 or not select.select([stdio.stdout], [], [], remaining)[0]:
                            raise SmokeFailure('linux_stdio_response_timeout')
                        chunk = os.read(stdio.stdout.fileno(), 1)
                        if not chunk or len(line) >= 65536:
                            raise SmokeFailure('linux_stdio_response_invalid')
                        line.extend(chunk)
                    stdio_output.extend(line)
                    result = json.loads(line)
                    if result.get('jsonrpc') != '2.0' or result.get('id') != identifier:
                        raise SmokeFailure('linux_stdio_response_identity')
                    if error:
                        if result.get('error') != {'code': -32000, 'message': 'memory_endpoint_unavailable'}:
                            raise SmokeFailure('linux_stdio_unavailable_body')
                    elif 'result' not in result or 'error' in result:
                        raise SmokeFailure('linux_stdio_request_failed')
                    return result

                initialized = rpc('linux-init', 'initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                    'clientInfo': {'name': 'linux-delivery-check', 'version': '1'}})
                assert initialized['result']['protocolVersion'] == '2025-06-18'
                rpc(None, 'notifications/initialized')
                assert [t['name'] for t in rpc('tools', 'tools/list')['result']['tools']] == ['memory.search']
                expect_search(rpc('initial-search', 'tools/call', search))
                assert provider_count() == 2
                before_memory = json.loads(command('docker', 'inspect', runtime))[0]
                initial_credentials = credentials()
                credential_cli('rotate', '--scope', 'memory', '--expected-revision', str(initial_credentials['revision']),
                    '--grace-seconds', '120')
                memory_overlap = credentials(); rotated_memory = memory_overlap['memory']['current']['token']
                expect_mcp(original_memory, 200, 'overlap_old'); expect_mcp(rotated_memory, 200, 'overlap_current')
                expect_status(next_token, 200, 'admin_unchanged_by_memory_rotation')

                # The environment token is still valid here, proving that an invalid client file
                # neither falls back to it nor forwards/replays the request.
                count = provider_count()
                command('docker', 'exec', helper, 'python3', '-c',
                    "from pathlib import Path;import os;p=Path('/client/client.json');b=p.with_name('valid.json');"
                    "b.write_bytes(p.read_bytes());b.chmod(0o600);t=p.with_name('invalid.json');t.write_text('{}');t.chmod(0o600);os.replace(t,p)")
                rpc('invalid-client', 'tools/call', search, error=True)
                assert provider_count() == count
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.replace('/client/valid.json','/client/client.json')")
                expect_search(rpc('repaired-client', 'tools/call', search)); assert provider_count() == count + 1
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/client/client.json',0o644)")
                rpc('unsafe-client', 'tools/call', search, error=True); assert provider_count() == count + 1
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/client/client.json',0o600)")
                credential_cli('export-memory', '--output', '/client/client.json', '--replace')
                credential_cli('revoke', '--scope', 'memory', '--expected-revision', str(memory_overlap['revision']),
                    '--id', memory_overlap['memory']['previous']['id'])
                expect_mcp(original_memory, 401, 'revoked'); expect_mcp(rotated_memory, 200, 'after_revoke')
                expect_search(rpc('reloaded-client', 'tools/call', search)); assert provider_count() == count + 2
                memory_revoked = credentials()
                credential_cli('rotate', '--scope', 'memory', '--expected-revision', str(memory_revoked['revision']),
                    '--grace-seconds', '1')
                final_memory = credentials()['memory']['current']['token']
                expect_mcp(final_memory, 200, 'expiry_current')
                deadline = time.monotonic() + 4
                while mcp_status(rotated_memory)[0] == 200 and time.monotonic() < deadline:
                    time.sleep(.1)
                expect_mcp(rotated_memory, 401, 'expired'); expect_mcp(final_memory, 200, 'after_expiry')
                rpc('expired-client', 'tools/call', search, error=True); assert provider_count() == count + 2
                credential_cli('export-memory', '--output', '/client/client.json', '--replace')
                expect_search(rpc('current-client', 'tools/call', search)); assert provider_count() == count + 3
                command('docker', 'exec', helper, 'python3', '-c',
                    "from pathlib import Path;import os;p=Path('/auth/tokens.json');b=p.with_name('valid.json');"
                    "b.write_bytes(p.read_bytes());b.chmod(0o600);t=p.with_name('invalid.json');t.write_text('{}');t.chmod(0o600);os.replace(t,p)")
                assert expect_mcp(final_memory, 503, 'invalid_server_file')['error'] == 'authentication_configuration_unavailable'
                rpc('invalid-server', 'tools/call', search, error=True); assert provider_count() == count + 3
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.replace('/auth/valid.json','/auth/tokens.json')")
                expect_mcp(final_memory, 200, 'repaired_server_file')
                expect_search(rpc('server-recovered', 'tools/call', search)); assert provider_count() == count + 4
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/auth/tokens.json',0o644)")
                expect_mcp(final_memory, 503, 'unsafe_server_file')
                rpc('unsafe-server', 'tools/call', search, error=True); assert provider_count() == count + 4
                command('docker', 'exec', helper, 'python3', '-c', "import os;os.chmod('/auth/tokens.json',0o600)")
                expect_search(rpc('all-recovered', 'tools/call', search)); assert provider_count() == count + 5
                assert stdio.poll() is None
                close_stdio()
                if stdio.returncode != 0:
                    raise SmokeFailure('linux_stdio_exit_failed')
                stderr.seek(0)
                if stderr.read(65537) or any(token.encode() in stdio_output for token in
                        (original_memory, rotated_memory, final_memory, next_token, core_values['NEXUS_MEMORY_API_KEY'])):
                    raise SmokeFailure('linux_stdio_output_not_isolated')
                after_memory = json.loads(command('docker', 'inspect', runtime))[0]
                assert before_memory['State']['StartedAt'] == after_memory['State']['StartedAt']
                assert before_memory['RestartCount'] == after_memory['RestartCount']
                rotation_evidence['memoryMcpRotationTested'] = True
                memory_evidence = {'tested': True, 'overlap': True, 'expiry': True, 'revocation': True,
                    'scopeIsolation': True, 'malformedAndUnsafeFilesRejected': True,
                    'stdioSameProcessReload': True, 'stdioRecoveryWithoutInitialize': True,
                    'noForwardOrReplayOnFailure': True, 'stdoutAndStderrIsolated': True,
                    'coreAuthNotMountedInStdio': True, 'runtimeRestartedDuringRotation': False,
                    'provider': 'empty_http_fixture', 'realProviderTested': False}
            with opener.open(url + '/', timeout=8) as response:
                html = response.read(65537)
                assert response.status == 200 and len(html) <= 65536
            with zipfile.ZipFile(jar) as archive:
                assert html == archive.read('BOOT-INF/classes/static/index.html')
            assets = re.findall(r'(?:src|href)="(/assets/[^"<>]+)"', html.decode())
            assert assets
            for asset in assets:
                with opener.open(url + asset, timeout=8) as response, zipfile.ZipFile(jar) as archive:
                    assert response.status == 200
                    assert response.read(2 * 1024 * 1024) == archive.read('BOOT-INF/classes/static' + asset)
            migrations = command('docker', 'exec', database, 'psql', '-U', 'nexus', '-d', 'nexus', '-t', '-A', '-c',
                                 'SELECT version FROM flyway_schema_history WHERE success ORDER BY installed_rank').splitlines()
            with zipfile.ZipFile(jar) as archive:
                expected = [match.group(1) for name in archive.namelist()
                    if (match := re.fullmatch(r'BOOT-INF/classes/db/migration/V([0-9]+)__[^/]+\.sql', name))]
            if not expected or len(set(expected)) != len(expected) or migrations != sorted(expected, key=int):
                raise SmokeFailure('migration_history_mismatch')
            # Reattach the proxy after Docker recreates CORE's network namespace.
            command('docker', 'rm', '-f', edge)
            containers.remove(edge)
            command('docker', 'restart', runtime)
            # Docker can allocate another host port after restarting a dynamic publication.
            port = command('docker', 'port', runtime, '9080/tcp').rsplit(':', 1)[1]
            url = 'http://127.0.0.1:' + port
            proxy()
            restarted = wait_ready()
            if memory_rotation:
                expect_mcp(final_memory, 200, 'after_restart')
                memory_evidence['mcpAuthenticatedAfterCoreRestart'] = True
            runtime_config = json.loads(command('docker', 'inspect', runtime))[0]
            assert runtime_config['Config']['User'] == '65534:65534'
            assert runtime_config['HostConfig']['ReadonlyRootfs'] is True
            java_version = command('docker', 'exec', runtime, 'java', '--version').splitlines()[0]
            assert re.search(r'\b21[. ]', java_version)
            images = {}
            for name in (java_image, 'postgres:17-alpine', 'eclipse-mosquitto:2.0.22', 'caddy:2.10.2-alpine', *((python_image,) if auth_rotation else ())):
                metadata = json.loads(command('docker', 'image', 'inspect', name))[0]
                assert metadata['Os'] == 'linux'
                images[name] = {'id': metadata['Id'], 'digests': metadata.get('RepoDigests', [])}
            return {'ok': True, 'bundleSha256': hashlib.sha256(raw).hexdigest(), 'coreCommit': delivery['coreCommit'],
                    'linuxRuntime': True, 'coreRunsAsNonRoot': True, 'readOnlyCoreFilesystem': True,
                    'javaVersion': java_version,
                    'initialStatus': initial, 'restartedStatus': restarted, 'migrations': migrations,
                    'consoleAssetsVerified': len(assets), 'images': images,
                    'credentialRotation': rotation_evidence,
                    'memoryRotation': memory_evidence,
                    'systemdTested': False, 'productionTlsTested': False, 'realProvidersTested': False}
    finally:
        result = subprocess.run(['docker', 'network', 'rm', network], capture_output=True, timeout=30)
        if result.returncode:
            raise SmokeFailure('network_cleanup_failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--java-image', default='eclipse-temurin:21-jre-jammy')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--auth-rotation', action='store_true', help='Exercise protected file reload in an isolated Linux volume')
    parser.add_argument('--memory-rotation', action='store_true', help='Also exercise Memory MCP and one live stdio process; implies --auth-rotation')
    args = parser.parse_args()
    if args.evidence and args.evidence.exists():
        parser.error('evidence output must be new')
    try:
        result = run(args.bundle, args.java_image, args.auth_rotation, args.memory_rotation)
        output = json.dumps(result, indent=2, sort_keys=True) + '\n'
        if args.evidence:
            with args.evidence.open('x') as file:
                file.write(output)
        print(output, end='')
        return 0
    except (OSError, ValueError, AssertionError, SmokeFailure, subprocess.SubprocessError) as error:
        reason = str(error) if isinstance(error, SmokeFailure) else 'linux_delivery_smoke_failed'
        print(json.dumps({'ok': False, 'reason': reason}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
