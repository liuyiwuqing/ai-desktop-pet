#!/usr/bin/env python3
"""Exercise the server delivery CLI; JAR/HTTP fixtures do not simulate production."""
import http.server
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/server_release.py'
TOKEN = 'isolated-admin-token-for-server-preflight-2026'


class ServerReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pet-server-cli-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def run_cli(self, *args, ok=True):
        result = subprocess.run([sys.executable, str(CLI), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stdout + result.stderr)
        self.assertNotIn(TOKEN, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def core_fixture(self):
        core = self.directory / 'core'
        core.mkdir()
        for name in ('compose.yml', 'tools/init_local.py', 'tools/broker_policy.py',
                     'tools/upgrade_broker_policy.py', 'tools/memory_mcp_stdio.py'):
            path = core / name
            path.parent.mkdir(exist_ok=True)
            path.write_text('# isolated packaging boundary fixture\n')
        subprocess.run(['git', 'init', '-q', str(core)], check=True)
        subprocess.run(['git', '-C', str(core), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(core), '-c', 'user.name=Fixture', '-c',
                        'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        jar = self.directory / 'core.jar'
        with zipfile.ZipFile(jar, 'w') as archive:
            archive.writestr('META-INF/MANIFEST.MF',
                             'Start-Class: dev.nexus.core.NexusCore\nImplementation-Version: 0.1.0-SNAPSHOT\n')
            for name in ('dev/nexus/core/NexusCore.class', 'static/index.html',
                         'db/migration/V10__memory_copy_lifecycle.sql'):
                archive.writestr('BOOT-INF/classes/' + name, 'isolated jar structure fixture')
        return core, jar

    def env_fixture(self, **changes):
        values = {'NEXUS_ADMIN_TOKEN': TOKEN, 'NEXUS_DB_URL': 'jdbc:postgresql://127.0.0.1:54329/nexus',
                  'NEXUS_DB_USER': 'nexus', 'NEXUS_DB_PASSWORD': 'isolated-db-password',
                  'NEXUS_MQTT_URI': 'ssl://mqtt.example.org:8883', 'NEXUS_MQTT_USERNAME': 'nexus-core',
                  'NEXUS_MQTT_PASSWORD': 'isolated-mqtt-password', 'NEXUS_MQTT_CA_CERT': '/etc/nexus/mqtt-ca.crt',
                  'NEXUS_MQTT_ALLOW_INSECURE': 'false'}
        values.update(changes)
        path = self.directory / 'core.env'
        path.write_text(''.join(key + '=' + value + '\n' for key, value in values.items()))
        path.chmod(0o600)
        return path

    def test_bundle_round_trip_refuses_overwrite_dirty_source_and_tampering(self):
        core, jar = self.core_fixture()
        bundle = self.directory / 'server.zip'
        result = self.run_cli('create', '--core', core, '--jar', jar, '--output', bundle)
        self.assertTrue(result['ok'])
        verified = self.run_cli('verify', '--bundle', bundle)
        self.assertEqual(verified['coreCommit'], result['coreCommit'])
        self.run_cli('create', '--core', core, '--jar', jar, '--output', bundle, ok=False)
        changed = self.directory / 'changed.zip'
        with zipfile.ZipFile(bundle) as original, zipfile.ZipFile(changed, 'w') as target:
            for item in original.infolist():
                data = original.read(item)
                if item.filename == 'core-tools/memory_mcp_stdio.py':
                    data += b'changed'
                target.writestr(item, data)
        self.run_cli('verify', '--bundle', changed, ok=False)
        (core / 'compose.yml').write_text('uncommitted change\n')
        self.run_cli('create', '--core', core, '--jar', jar,
                     '--output', self.directory / 'dirty.zip', ok=False)

    def test_bundle_rejects_unexpected_members_and_non_application_jar(self):
        core, jar = self.core_fixture()
        bundle = self.directory / 'server.zip'
        self.run_cli('create', '--core', core, '--jar', jar, '--output', bundle)
        with zipfile.ZipFile(bundle, 'a') as archive:
            archive.writestr('../outside', b'unexpected')
        self.run_cli('verify', '--bundle', bundle, ok=False)
        jar.write_bytes(b'not a runnable jar')
        output = self.directory / 'bad.zip'
        self.run_cli('create', '--core', core, '--jar', jar, '--output', output, ok=False)
        self.assertFalse(output.exists())

    def test_bundle_refuses_private_files_even_when_committed(self):
        core, jar = self.core_fixture()
        private = core / 'data/local.env'
        private.parent.mkdir()
        private.write_text('NEXUS_ADMIN_TOKEN=' + TOKEN + '\n')
        subprocess.run(['git', '-C', str(core), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(core), '-c', 'user.name=Fixture', '-c',
                        'user.email=fixture@example.invalid', 'commit', '-qm', 'private fixture'], check=True)
        output = self.directory / 'private.zip'
        result = self.run_cli('create', '--core', core, '--jar', jar, '--output', output, ok=False)
        self.assertEqual(result['reason'], 'private_or_generated_source_tracked')
        self.assertFalse(output.exists())

    def test_configuration_preflight_rejects_templates_unsafe_modes_and_incomplete_features(self):
        result = self.run_cli('preflight', '--env-file', self.env_fixture())
        self.assertTrue(result['ok'])
        self.assertFalse(result['hostChecked'])
        missing = self.run_cli('preflight', '--env-file', self.env_fixture(NEXUS_DB_PASSWORD='', NEXUS_MQTT_PASSWORD=''), ok=False)
        self.assertEqual(missing['fields'], ['NEXUS_DB_PASSWORD', 'NEXUS_MQTT_PASSWORD'])
        self.assertTrue(self.run_cli('preflight', '--env-file', self.env_fixture(
            NEXUS_DB_URL='jdbc:postgresql://database.example.org:5432/nexus?sslmode=verify-full'))['ok'])
        self.run_cli('preflight', '--env-file', self.env_fixture(NEXUS_MQTT_URI='ssl://mqtt.example.invalid:8883'), ok=False)
        self.run_cli('preflight', '--env-file', ROOT / 'deployment/core.env.example', ok=False)
        env = self.env_fixture(NEXUS_MEMORY_MCP_ENABLED='true', NEXUS_MEMORY_MCP_TOKEN=TOKEN)
        self.run_cli('preflight', '--env-file', env, ok=False)
        env = self.env_fixture(NEXUS_MQTT_ALLOW_INSECURE='true')
        self.run_cli('preflight', '--env-file', env, ok=False)
        env = self.env_fixture()
        env.chmod(0o644)
        self.run_cli('preflight', '--env-file', env, ok=False)
        env.chmod(0o600)
        linked = self.directory / 'linked.env'
        linked.symlink_to(env)
        self.run_cli('preflight', '--env-file', linked, ok=False)
        with env.open('a') as output:
            output.write('NEXUS_ADMIN_TOKEN=duplicate-must-not-be-printed\n')
        self.run_cli('preflight', '--env-file', env, ok=False)

    def test_voice_preflight_requires_matching_restricted_credentials_and_wss(self):
        machine = 'isolated-machine-token-for-server-preflight-2026'
        env = self.env_fixture(NEXUS_HALO_ENABLED='true', NEXUS_HALO_URL='https://blog.example.org',
                               NEXUS_HALO_TOKEN='isolated-halo-token', NEXUS_MEMORY_ENABLED='true',
                               NEXUS_MEMORY_URL='http://127.0.0.1:1933', NEXUS_MEMORY_API_KEY='isolated-key',
                               NEXUS_MEMORY_ACCOUNT='default', NEXUS_MEMORY_USER='default',
                               NEXUS_MEMORY_PEER='nexus-pet', NEXUS_MEMORY_MCP_ENABLED='true',
                               NEXUS_MEMORY_MCP_TOKEN=machine)
        voice = self.directory / 'voice.env'
        def write_voice(token, endpoint):
            voice.write_text('NEXUS_MEMORY_MCP_URL=http://127.0.0.1:8088/mcp/memory\n'
                             'NEXUS_MEMORY_MCP_TOKEN=' + token + '\nMCP_ENDPOINT=' + endpoint + '\n')
            voice.chmod(0o600)
        write_voice(machine, 'wss://voice.example.org/mcp/?token=isolated')
        self.assertTrue(self.run_cli('preflight', '--env-file', env, '--voice-env-file', voice)['voiceConfigured'])
        write_voice(TOKEN, 'wss://voice.example.org/mcp/')
        self.run_cli('preflight', '--env-file', env, '--voice-env-file', voice, ok=False)
        write_voice(machine, 'ws://voice.example.org/mcp/')
        self.run_cli('preflight', '--env-file', env, '--voice-env-file', voice, ok=False)
        env.write_text(env.read_text().replace(machine, TOKEN))
        result = self.run_cli('preflight', '--env-file', env, ok=False)
        self.assertEqual(result['reason'], 'machine_token_reuses_admin')

    def start_http(self, mode):
        self.calls = []
        calls = self.calls
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                auth = self.headers.get('Authorization')
                calls.append((self.path, auth))
                if mode == 'redirect':
                    self.send_response(302)
                    self.send_header('Location', '/trap')
                    self.end_headers()
                    return
                status = 401 if auth != 'Bearer ' + TOKEN else 200
                body = json.dumps({'database': 'ready', 'mqtt': 'ready' if mode == 'ready' else 'unavailable',
                                   'mqttError': TOKEN, 'schemaVersion': 1}).encode()
                self.send_response(status)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *args):
                pass
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        def stop():
            server.shutdown()
            server.server_close()
            thread.join()
        self.addCleanup(stop)
        return 'http://127.0.0.1:' + str(server.server_port)

    def test_probe_checks_unauthenticated_boundary_and_both_dependencies_without_echoing_body(self):
        env = self.env_fixture()
        self.assertTrue(self.run_cli('probe', '--env-file', env, '--url', self.start_http('ready'))['ok'])
        self.run_cli('probe', '--env-file', env, '--url', self.start_http('unavailable'), ok=False)
        self.run_cli('probe', '--env-file', env, '--url', 'http://remote.example.org', ok=False)

    def test_probe_does_not_follow_redirect_or_forward_token_to_it(self):
        self.run_cli('probe', '--env-file', self.env_fixture(), '--url', self.start_http('redirect'), ok=False)
        self.assertEqual(self.calls, [('/api/v1/status', None)])


if __name__ == '__main__':
    unittest.main()
