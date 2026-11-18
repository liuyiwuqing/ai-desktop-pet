#!/usr/bin/env python3
"""Actual Vue against RAM-only pull controls; no provider requests or database."""
import argparse
import copy
import functools
import json
import time
from urllib.parse import urlparse
from roles_ui_fixture import RolesFixture
from management_ui_fixture import FixtureServer


class PullFixture(RolesFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(pulls=[{'id': 'demo', 'name': '隔离 RSS', 'kind': 'rss', 'host': 'example.com', 'deviceIds': ['dodo-1'], 'intervalMinutes': 15, 'configuration': 'a' * 64, 'enabled': False, 'version': 1, 'baseline': False, 'lastAttemptAt': None, 'lastSuccessAt': None, 'lastError': None, 'lastReceived': 0, 'state': 'disabled'}])

    def do_GET(self):
        if urlparse(self.path).path != '/api/v1/connectors/pull':
            return super().do_GET()
        with self.lock:
            state = type(self).qa
            mode, delay = state['read_mode'], state['read_delay']
            value = copy.deepcopy(state['pulls'])
        time.sleep(delay)
        return self.json_response(503 if mode == 'error' else 200, {'error': 'unavailable'} if mode == 'error' else {'items': value})

    def control(self):
        path = urlparse(self.path).path
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))) or '{}')
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': self.command, 'path': path, 'body': body})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('unauthorized', 'conflict', 'error'):
                return self.json_response({'unauthorized': 401, 'conflict': 409, 'error': 502}[mode], {'error': 'pull_provider_failed'})
            source = state['pulls'][0]
            if path.endswith('/enabled'):
                if source['version'] == body['expectedVersion']:
                    source.update(enabled=body['enabled'], version=source['version'] + 1, state='not_checked' if body['enabled'] else 'disabled')
                value = copy.deepcopy(source)
            elif path.endswith('/probe'):
                value = {'dryRun': True, 'items': 2, 'samples': [{'title': '样例标题', 'content': '样例正文'}]}
            else:
                source.update(baseline=True, lastSuccessAt='2026-11-15T01:00:00Z', lastAttemptAt='2026-11-15T01:00:00Z', state='ready')
                value = {'baseline': True, 'received': 0}
        time.sleep(delay)
        if mode == 'drop_after':
            return self.drop()
        return self.json_response(200, value)

    def do_PUT(self):
        return self.control() if urlparse(self.path).path.startswith('/api/v1/connectors/pull/') else super().do_PUT()

    def do_POST(self):
        return self.control() if urlparse(self.path).path.startswith('/api/v1/connectors/pull/') else super().do_POST()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    PullFixture.reset()
    with FixtureServer(('127.0.0.1', 18822), functools.partial(PullFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
