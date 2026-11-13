#!/usr/bin/env python3
"""Loopback RAM boundary for actual console tool/automation fault regression."""
import argparse
import copy
import http.server
import json
from pathlib import Path
import socket
import time
import uuid
from urllib.parse import urlparse
from management_ui_fixture import CONTROL, TOKEN, Fixture

STAMP = '2026-11-13T10:00:00Z'
TOOLS = [{'id': 'system.info', 'title': '系统信息', 'risk': 'read_only', 'argument': ''},
         {'id': 'project.status', 'title': '项目状态', 'risk': 'read_only', 'argument': 'projectId'},
         {'id': 'app.open', 'title': '打开应用', 'risk': 'confirmation', 'argument': 'applicationId'}]


class OperationsFixture(Fixture):
    qa = {}

    @classmethod
    def reset(cls):
        cls.qa = {'read_mode': 'success', 'read_delay': 0, 'audit_mode': 'success', 'audit_delay': 0,
                  'next_write': 'success', 'write_delay': 0, 'requests': [], 'jobs': [], 'rules': [], 'runs': [],
                  'tool_keys': {}, 'rule_keys': {}, 'bridge': 'ready', 'ai': True}

    def drop(self):
        self.close_connection = True
        self.connection.shutdown(socket.SHUT_RDWR)
        self.connection.close()

    def do_GET(self):
        path = urlparse(self.path).path
        if path not in ('/api/v1/tools', '/api/v1/tool-jobs', '/api/v1/automation/rules', '/api/v1/automation/runs', '/api/v1/ai') and not path.startswith('/api/v1/tool-jobs/'):
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'GET', 'path': path})
            audit = path.endswith('/audit')
            mode = state['audit_mode'] if audit else state['read_mode']
            delay = state['audit_delay'] if audit else state['read_delay']
            if path == '/api/v1/tools':
                value = {'configured': state['bridge'] != 'off', 'state': state['bridge'], 'tools': TOOLS,
                         'bridge': {'tools': [{'id': t['id'], 'available': state['bridge'] == 'ready', 'projectIds': ['project-a']} if t['id'] == 'project.status'
                                              else {'id': t['id'], 'available': state['bridge'] == 'ready', 'applicationIds': ['codex']} for t in TOOLS]}}
            elif path == '/api/v1/tool-jobs':
                value = {'items': state['jobs']}
            elif path.startswith('/api/v1/tool-jobs/'):
                job = next((j for j in state['jobs'] if j['jobId'] == path.split('/')[4]), None)
                if not job:
                    return self.json_response(404, {'error': 'not_found'})
                value = {'items': [{'sequence': 1, 'event': 'created', 'detail': '', 'createdAt': STAMP}]} if audit else job
            elif path.endswith('/rules'):
                value = {'items': state['rules']}
            elif path.endswith('/runs'):
                value = {'items': state['runs']}
            else:
                value = {'configured': state['ai']}
            value = copy.deepcopy(value)
        time.sleep(delay)
        try:
            return self.json_response(401 if mode == 'unauthorized' else 503 if mode == 'error' else 200,
                                      {'error': 'unauthorized' if mode == 'unauthorized' else 'unavailable'} if mode != 'success' else value)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        path = urlparse(self.path).path
        if path != '/__qa/operations' and path != '/api/v1/tool-jobs' and path != '/api/v1/automation/rules' and not path.startswith(('/api/v1/tool-jobs/', '/api/v1/automation/runs/')):
            return super().do_POST()
        raw = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            return self.json_response(400, {'error': 'invalid_request'})
        with self.lock:
            if path == '/__qa/operations':
                if self.headers.get('X-QA-Key') != CONTROL:
                    return self.json_response(401, {'error': 'unauthorized'})
                if body.pop('reset', False):
                    type(self).reset()
                for key, value in body.items():
                    if key in type(self).qa and key not in ('requests', 'tool_keys', 'rule_keys'):
                        type(self).qa[key] = value
                return self.json_response(200, type(self).qa)
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            state = type(self).qa
            state['requests'].append({'method': 'POST', 'path': path, 'body': body})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('error', 'deny', 'unauthorized', 'conflict'):
                return self.json_response({'error': 503, 'deny': 400, 'unauthorized': 401, 'conflict': 409}[mode],
                                          {'error': 'unauthorized' if mode == 'unauthorized' else 'job_not_confirmable' if mode == 'conflict' else 'invalid_request'})
            status = 204
            value = None
            if path == '/api/v1/tool-jobs':
                key = body.get('requestKey')
                old = state['tool_keys'].get(key)
                if old:
                    value = next(j for j in state['jobs'] if j['jobId'] == old)
                    status = 200
                else:
                    value = {'jobId': str(uuid.uuid4()), 'toolId': body['toolId'], 'state': 'awaiting_confirmation' if body['toolId'] == 'app.open' else 'queued',
                             'risk': 'confirmation' if body['toolId'] == 'app.open' else 'read', 'arguments': body['arguments'], 'argumentsHash': 'fixture-argument-hash',
                             'result': None, 'lastError': None, 'createdAt': STAMP, 'expiresAt': '2099-11-13T10:00:00Z'}
                    state['jobs'].insert(0, value)
                    state['tool_keys'][key] = value['jobId']
                    status = 201
            elif path == '/api/v1/automation/rules':
                key = body.get('requestKey')
                old = state['rule_keys'].get(key) if key else None
                if old:
                    value = {'ruleId': old}
                    status = 200
                else:
                    rule = {'ruleId': str(uuid.uuid4()), 'name': body['name'], 'trigger': body['trigger'], 'action': body['action'], 'enabled': False, 'version': 1,
                            'arguments': {k: body[k] for k in ('localTime', 'zone', 'title', 'content') if k in body}}
                    state['rules'].insert(0, rule)
                    if key:
                        state['rule_keys'][key] = rule['ruleId']
                    value = {'ruleId': rule['ruleId']}
                    status = 201
            elif path.startswith('/api/v1/tool-jobs/'):
                job = next(j for j in state['jobs'] if j['jobId'] == path.split('/')[4])
                if path.endswith('/confirm'):
                    job['state'] = 'queued'
                elif path.endswith('/cancel'):
                    job['state'] = 'cancelled'
                elif path.endswith('/reconcile'):
                    job['state'] = 'succeeded'
                    job['lastError'] = ''
                    job['result'] = {'applicationId': 'codex', 'launchRequested': True}
            elif path.endswith('/retry'):
                run = next(r for r in state['runs'] if r['runId'] == path.split('/')[5])
                run['state'] = 'pending'
            value = copy.deepcopy(value)
        time.sleep(delay)
        try:
            if mode == 'drop_after':
                return self.drop()
            return self.json_response(status, {} if mode == 'invalid' else value)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_PUT(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/v1/automation/rules/') or not path.endswith('/enabled'):
            return self.json_response(404, {'error': 'not_found'})
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'PUT', 'path': path, 'body': body})
            rule = next(r for r in state['rules'] if r['ruleId'] == path.split('/')[5])
            rule['enabled'] = body['enabled']
            rule['version'] += 1
            return self.json_response(204, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18808)
    args = parser.parse_args()
    root = args.frontend.resolve()
    if not (root / 'index.html').is_file():
        parser.error('build actual CORE frontend first')
    OperationsFixture.reset()
    handler = lambda *a, **kw: OperationsFixture(*a, directory=str(root), **kw)
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
