#!/usr/bin/env python3
"""Actual Vue against RAM-only deletion/preview boundaries, without real actions."""
import argparse
import functools
import http.server
import json
import time
from urllib.parse import parse_qs, urlparse
from automation_edit_ui_fixture import AutomationEditFixture
from management_ui_fixture import TOKEN


class LifecycleFixture(AutomationEditFixture):
    @staticmethod
    def view(rule):
        value = AutomationEditFixture.view(rule)
        if rule.get('deleted'):
            value.update(deleted=True, canEdit=False, editBlock='rule_deleted')
        return value

    def do_GET(self):
        if urlparse(self.path).path != '/api/v1/automation/rules':
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'GET', 'path': '/api/v1/automation/rules'})
            value = {'items': [self.view(r) for r in state['rules'] if not r.get('deleted')]}
            mode, delay = state['read_mode'], state['read_delay']
        time.sleep(delay)
        return self.json_response(200, value) if mode == 'success' else self.json_response(503, {'error': 'unavailable'})

    def do_POST(self):
        path = urlparse(self.path).path
        if not path.endswith('/preview'):
            return super().do_POST()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'POST', 'path': path, 'body': body})
            rule = next(r for r in state['rules'] if r['ruleId'] == path.split('/')[-2])
            result = {'dryRun': True, 'rule': self.view(rule), 'candidateDevices': 2, 'block': '', 'asOf': '2026-11-15T00:00:00Z'}
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
        time.sleep(delay)
        if mode == 'unauthorized':
            return self.json_response(401, {'error': 'unauthorized'})
        if mode == 'error':
            return self.json_response(503, {'error': 'unavailable'})
        return self.json_response(200, result)

    def do_DELETE(self):
        path = urlparse(self.path).path
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        version = int(parse_qs(urlparse(self.path).query)['expectedVersion'][0])
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'DELETE', 'path': path, 'version': version})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('unauthorized', 'conflict', 'error'):
                return self.json_response({'unauthorized': 401, 'conflict': 409, 'error': 503}[mode], {'error': 'automation_version_conflict'})
            rule = next(r for r in state['rules'] if r['ruleId'] == path.split('/')[-1])
            if rule['enabled'] or rule.get('executing'):
                return self.json_response(409, {'error': 'rule_enabled'})
            if not rule.get('deleted'):
                if version != rule['version']:
                    return self.json_response(409, {'error': 'automation_version_conflict'})
                rule.update(deleted=True, version=version + 1)
        time.sleep(delay)
        if mode == 'drop_after':
            return self.drop()
        return self.json_response(204, None)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', required=True)
    args = parser.parse_args()
    LifecycleFixture.reset()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 18814), functools.partial(LifecycleFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
