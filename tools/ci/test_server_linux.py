#!/usr/bin/env python3
"""Boot the packaged JAR in disposable Linux containers; never read development data."""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
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


def run(bundle, java_image):
    raw = bundle.read_bytes()
    containers = []
    network = 'pet-server-smoke-' + uuid.uuid4().hex[:12]
    def cleanup_containers():
        failed = False
        for container in reversed(containers):
            result = subprocess.run(['docker', 'rm', '-f', container], capture_output=True, timeout=30)
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
            core_env = env_file('core.env', {
                'NEXUS_ADMIN_TOKEN': local['NEXUS_ADMIN_TOKEN'],
                'NEXUS_DB_URL': 'jdbc:postgresql://database:5432/nexus', 'NEXUS_DB_USER': 'nexus',
                'NEXUS_DB_PASSWORD': local['NEXUS_DB_PASSWORD'], 'NEXUS_BIND_ADDRESS': '127.0.0.1',
                'NEXUS_MQTT_URI': 'ssl://mqtt:8883', 'NEXUS_MQTT_USERNAME': 'nexus-core',
                'NEXUS_MQTT_PASSWORD': local['NEXUS_MQTT_PASSWORD'], 'NEXUS_MQTT_CA_CERT': '/test-ca.crt',
                'NEXUS_MQTT_ALLOW_INSECURE': 'false', 'NEXUS_HALO_ENABLED': 'false',
                'NEXUS_AI_ENABLED': 'false', 'NEXUS_MEMORY_ENABLED': 'false',
                'NEXUS_MEMORY_MCP_ENABLED': 'false', 'NEXUS_BRIDGE_ENABLED': 'false', 'NEXUS_RETENTION_ENABLED': 'false'})
            cli = stage / 'desktop-pet/tools/server_release.py'
            ready = json.loads(command(sys.executable, cli, 'preflight', '--env-file', core_env))
            assert ready['ok'] and not any(ready['features'].values())

            def start(*args):
                container = command('docker', 'run', '-d', *args)
                containers.append(container)
                return container

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
                            '--memory', '768m', '--env-file', core_env, '-p', '127.0.0.1::9080',
                            '-v', str(jar) + ':/app/nexus-core.jar:ro',
                            '-v', str(core / 'data/mosquitto/certs/ca.crt') + ':/test-ca.crt:ro',
                            java_image, 'java', '-jar', '/app/nexus-core.jar')
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
            assert migrations == [str(n) for n in range(1, 11)]
            # Reattach the proxy after Docker recreates CORE's network namespace.
            command('docker', 'rm', '-f', edge)
            containers.remove(edge)
            command('docker', 'restart', runtime)
            # Docker can allocate another host port after restarting a dynamic publication.
            port = command('docker', 'port', runtime, '9080/tcp').rsplit(':', 1)[1]
            url = 'http://127.0.0.1:' + port
            proxy()
            restarted = wait_ready()
            runtime_config = json.loads(command('docker', 'inspect', runtime))[0]
            assert runtime_config['Config']['User'] == '65534:65534'
            assert runtime_config['HostConfig']['ReadonlyRootfs'] is True
            java_version = command('docker', 'exec', runtime, 'java', '--version').splitlines()[0]
            assert re.search(r'\b21[. ]', java_version)
            images = {}
            for name in (java_image, 'postgres:17-alpine', 'eclipse-mosquitto:2.0.22', 'caddy:2.10.2-alpine'):
                metadata = json.loads(command('docker', 'image', 'inspect', name))[0]
                assert metadata['Os'] == 'linux'
                images[name] = {'id': metadata['Id'], 'digests': metadata.get('RepoDigests', [])}
            return {'ok': True, 'bundleSha256': hashlib.sha256(raw).hexdigest(), 'coreCommit': delivery['coreCommit'],
                    'linuxRuntime': True, 'coreRunsAsNonRoot': True, 'readOnlyCoreFilesystem': True,
                    'javaVersion': java_version,
                    'initialStatus': initial, 'restartedStatus': restarted, 'migrations': migrations,
                    'consoleAssetsVerified': len(assets), 'images': images,
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
    args = parser.parse_args()
    if args.evidence and args.evidence.exists():
        parser.error('evidence output must be new')
    try:
        result = run(args.bundle, args.java_image)
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
