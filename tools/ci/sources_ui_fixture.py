#!/usr/bin/env python3
"""RAM-only source management, credentials and signed endpoint boundary."""
import argparse
import copy
import functools
import json
import time
from urllib.parse import urlparse
from routing_ui_fixture import RoutingFixture
from management_ui_fixture import FixtureServer, TOKEN


class SourcesFixture(RoutingFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(sources=[])

    def do_GET(self):
        if urlparse(self.path).path != '/api/v1/sources':
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'GET', 'path': '/api/v1/sources'})
            value = {'items': copy.deepcopy(state['sources'])}
            mode = state['read_mode']
        return self.json_response(200, value) if mode == 'success' else self.json_response(503, {'error': 'unavailable'})

    def source_write(self, method):
        path = urlparse(self.path).path
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': method, 'path': path, 'body': body})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('error', 'unauthorized'):
                return self.json_response(401 if mode == 'unauthorized' else 503, {'error': 'unavailable'})
            source_id = path.split('/')[4]
            source = next((s for s in state['sources'] if s['id'] == source_id), None)
            if path.endswith('/test'):
                value = {'messageId': 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', 'created': True}
            elif path.endswith('/credentials'):
                if source['version'] != body['expectedVersion'] or source['enabled']:
                    return self.json_response(409, {'error': 'source_version_conflict'})
                source['version'] += 1
                value = {'source': source, 'credentialsAvailable': True, 'token': 'fixture-token-rotated-not-real', 'secret': 'fixture-secret-rotated-not-real'}
            elif method == 'PATCH':
                if source['version'] != body['expectedVersion']:
                    return self.json_response(409, {'error': 'source_version_conflict'})
                source.update(name=body['name'], enabled=body['enabled'], deviceIds=body['deviceIds'], version=source['version'] + 1)
                value = source
            elif source:
                value = {'source': source, 'credentialsAvailable': False}
            else:
                source = {'id': source_id, 'name': body['name'], 'enabled': False, 'version': 1, 'deviceIds': body['deviceIds'], 'received': 0, 'lastReceivedAt': None, 'recentEvents': []}
                state['sources'].append(source)
                value = {'source': source, 'credentialsAvailable': True, 'token': 'fixture-token-once-not-real', 'secret': 'fixture-secret-once-not-real'}
            value = copy.deepcopy(value)
        time.sleep(delay)
        if mode == 'drop_after':
            return self.drop()
        return self.json_response(200, value)

    def do_PUT(self):
        return self.source_write('PUT') if self.path.startswith('/api/v1/sources') else super().do_PUT()

    def do_PATCH(self):
        return self.source_write('PATCH') if self.path.startswith('/api/v1/sources') else super().do_PATCH()

    def do_POST(self):
        return self.source_write('POST') if self.path.startswith('/api/v1/sources') else super().do_POST()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    SourcesFixture.reset()
    with FixtureServer(('127.0.0.1', 18816), functools.partial(SourcesFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
