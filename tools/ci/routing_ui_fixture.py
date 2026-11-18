#!/usr/bin/env python3
"""RAM-only routing API boundary for actual Vue; never sends device messages."""
import argparse
import copy
import functools
import json
import time
from urllib.parse import urlparse
from operations_ui_fixture import OperationsFixture
from management_ui_fixture import FixtureServer, TOKEN


class RoutingFixture(OperationsFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(routes=[], policy={'mode': 'broadcast', 'version': 1})

    def do_GET(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/v1/routing'):
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'GET', 'path': path})
            if path == '/api/v1/routing':
                result = {'policy': state['policy'], 'items': [r for r in state['routes'] if not r.get('deleted')]}
            else:
                result = next((r for r in state['routes'] if r['ruleId'] == path.split('/')[-1]), {})
            result = copy.deepcopy(result)
            mode, delay = state['read_mode'], state['read_delay']
        time.sleep(delay)
        return self.json_response(200, result) if mode == 'success' else self.json_response(503, {'error': 'unavailable'})

    def write(self, method):
        path = urlparse(self.path).path
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        raw = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        body = json.loads(raw) if raw else {}
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': method, 'path': path, 'body': body})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('error', 'unauthorized', 'conflict'):
                return self.json_response({'error': 503, 'unauthorized': 401, 'conflict': 409}[mode], {'error': 'route_version_conflict'})
            status = 200
            if path.endswith('/policy'):
                if state['policy']['mode'] != body['mode']:
                    state['policy'] = {'mode': body['mode'], 'version': state['policy']['version'] + 1}
                result = state['policy']
            elif path.endswith('/preview'):
                result = {'dryRun': True, 'targets': [{'deviceId': 'dodo-ui-fixture', 'priority': None}]}
            elif path.endswith('/test'):
                result = {'messageId': 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'}
            elif method == 'DELETE':
                result = None
                status = 204
                next(r for r in state['routes'] if r['ruleId'] == path.split('/')[-1])['deleted'] = True
            else:
                rule = next((r for r in state['routes'] if r['ruleId'] == path.split('/')[-1]), None)
                result = {'ruleId': path.split('/')[-1], 'version': body['expectedVersion'] + 1,
                          'definition': body['definition'], 'deleted': False}
                if rule:
                    if rule['definition'] != body['definition']:
                        rule.update(result)
                    result = rule
                else:
                    state['routes'].append(result)
            result = copy.deepcopy(result)
        time.sleep(delay)
        if mode == 'drop_after':
            return self.drop()
        return self.json_response(status, result)

    def do_PUT(self):
        return self.write('PUT') if self.path.startswith('/api/v1/routing') else super().do_PUT()

    def do_POST(self):
        return self.write('POST') if self.path.startswith('/api/v1/routing') else super().do_POST()

    def do_DELETE(self):
        return self.write('DELETE')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', required=True)
    args = parser.parse_args()
    RoutingFixture.reset()
    with FixtureServer(('127.0.0.1', 18815), functools.partial(RoutingFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
