#!/usr/bin/env python3
"""Validate production proxy templates and isolated HTTP routing; no deployment or real TLS issuance."""
import argparse
import http.server
import json
from pathlib import Path
import socket
import subprocess
import threading
import time
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
IMAGE = 'caddy:2.10.2-alpine'
EVENTS = []
TOKEN = 'isolated-proxy-test-token'

class Upstream(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.respond(b'')
    def do_POST(self):
        self.respond(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
    def respond(self, body):
        EVENTS.append((self.command, self.path, body))
        if body == b'drop':
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        authorized = self.headers.get('Authorization') == 'Bearer ' + TOKEN
        payload = json.dumps({'path': self.path, 'bytes': len(body), 'authorized': authorized}).encode()
        self.send_response(200 if authorized else 401)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def log_message(self, *args):
        pass

class DeploymentProxyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Upstream)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
    def start_proxy(self, control):
        prefix = 'PET_CONTROL' if control else 'NEXUS_CONSOLE'
        template = 'Caddyfile.control' if control else 'Caddyfile'
        mount = str(ROOT / 'deployment' / template) + ':/etc/caddy/Caddyfile:ro'
        # Validate the HTTPS template with no network, certificate issuance or host data mount.
        validate = ['docker', 'run', '--rm', '--network', 'none', '--read-only',
            '--tmpfs', '/data', '--tmpfs', '/config', '-e', prefix + '_DOMAIN=proxy.example.invalid', '-v', mount,
            IMAGE, 'caddy', 'validate', '--config', '/etc/caddy/Caddyfile', '--adapter', 'caddyfile']
        result = subprocess.run(validate, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        command = ['docker', 'run', '--rm', '-d', '--read-only', '--tmpfs', '/data', '--tmpfs', '/config',
            '-p', '127.0.0.1::8080', '-e', prefix + '_DOMAIN=http://:8080',
            '-e', prefix + '_UPSTREAM=host.docker.internal:' + str(self.server.server_port), '-v', mount,
            IMAGE, 'caddy', 'run', '--config', '/etc/caddy/Caddyfile', '--adapter', 'caddyfile']
        self.container = subprocess.check_output(command, text=True).strip()
        self.addCleanup(lambda: subprocess.run(['docker', 'stop', self.container], check=True, stdout=subprocess.DEVNULL))
        port = subprocess.check_output(['docker', 'port', self.container, '8080/tcp'], text=True).strip().rsplit(':', 1)[1]
        self.url = 'http://127.0.0.1:' + port
        for _ in range(60):
            try:
                self.request('/ready', authorization=False)
                return
            except OSError:
                time.sleep(.1)
        self.fail('isolated proxy did not start')
    def request(self, path, body=None, authorization=True):
        request = urllib.request.Request(self.url + path, data=body,
            headers={'Authorization': 'Bearer ' + TOKEN} if authorization else {})
        try:
            with urllib.request.urlopen(request, timeout=4) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            with error:
                return error.code, error.read()
    def test_console_template_preserves_auth_and_limits_body(self):
        self.start_proxy(False)
        self.assertEqual(self.request('/api/v1/status', authorization=False)[0], 401)
        status, body = self.request('/api/v1/status')
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(body)['authorized'])
        self.assertEqual(self.request('/api/v1/messages', b'x' * 32768)[0], 200)
        self.assertEqual(self.request('/api/v1/messages', b'x' * 32769)[0], 413)
    def test_control_template_routes_only_supported_methods_and_never_retries_post(self):
        self.start_proxy(True)
        before = len(EVENTS)
        for path in ('/v1/execute', '/v1/results/other/device', '/v1/results/dodo/1/extra', '/admin'):
            self.assertEqual(self.request(path)[0], 404)
        self.assertEqual(len(EVENTS), before)
        self.assertEqual(self.request('/v1/results/dodo-1/9', authorization=False)[0], 401)
        self.assertEqual(self.request('/v1/results/dodo-1/9')[0], 200)
        self.assertEqual(self.request('/v1/execute', b'x' * 2048)[0], 200)
        self.assertEqual(self.request('/v1/execute', b'x' * 2049)[0], 413)
        before = sum(event[2] == b'drop' for event in EVENTS)
        self.assertEqual(self.request('/v1/execute', b'drop')[0], 502)
        self.assertEqual(sum(event[2] == b'drop' for event in EVENTS), before + 1)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', default=IMAGE)
    args = parser.parse_args()
    IMAGE = args.image
    unittest.main(argv=['test_deployment.py'])
