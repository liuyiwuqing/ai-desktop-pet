#!/usr/bin/env python3
"""Actual Vue presentation states with loopback RAM reads; never a live CORE."""
import argparse
import http.server
import json
from pathlib import Path
import time
from urllib.parse import urlparse

from management_ui_fixture import CONTROL, TOKEN
from overview_ui_fixture import OverviewFixture


class StatesFixture(OverviewFixture):
    presentation = {'path': '', 'mode': 'success', 'delay': 0}

    def do_GET(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/'):
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            mode = dict(type(self).presentation)
        if mode['path'] == path:
            time.sleep(mode['delay'])
            if mode['mode'] == 'error':
                try:
                    return self.json_response(503, {'error': 'unavailable'})
                except (BrokenPipeError, ConnectionResetError):
                    return
        # Minimal successful reads only; existing dedicated fixtures own writes,
        # permissions, pagination, unknown submissions and device contracts.
        responses = {
            '/api/v1/messages': {'items': [], 'total': 0, 'page': 1, 'size': 30},
            '/api/v1/connectors': {'items': []},
            '/api/v1/routing': {'policy': {'mode': 'broadcast', 'version': 1}, 'items': []},
            '/api/v1/templates': {'items': [], 'variables': []},
            '/api/v1/automation/rules': {'items': []},
            '/api/v1/automation/runs': {'items': []},
            '/api/v1/ai': {'configured': False},
            '/api/v1/tools': {'configured': False, 'state': 'off', 'tools': []},
            '/api/v1/tool-jobs': {'items': []},
            '/api/v1/access/tokens': {'items': []},
            '/api/v1/audit': {'items': [], 'total': 0},
            '/api/v1/sources': {'items': []},
            '/api/v1/memory': {'configured': False, 'storedCopies': 0, 'pendingDeletions': 0, 'deletionErrors': 0},
        }
        if path == '/api/v1/devices' and mode['path'] == path:
            return self.json_response(200, {'items': []})
        if path in responses:
            return self.json_response(200, responses[path])
        return super().do_GET()

    def do_POST(self):
        if urlparse(self.path).path != '/__qa/presentation':
            return super().do_POST()
        if self.headers.get('X-QA-Key') != CONTROL:
            return self.json_response(401, {'error': 'unauthorized'})
        value = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            for key in ('path', 'mode'):
                if key in value:
                    type(self).presentation[key] = value[key]
            if 'delay' in value:
                type(self).presentation['delay'] = min(2, max(0, float(value['delay'])))
            return self.json_response(200, dict(type(self).presentation))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18826)
    args = parser.parse_args()
    root = args.frontend.resolve()
    if not (root / 'index.html').is_file():
        parser.error('build the actual CORE frontend first')
    handler = lambda *a, **kw: StatesFixture(*a, directory=str(root), **kw)
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
