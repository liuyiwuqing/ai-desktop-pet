#!/usr/bin/env python3
"""Actual Vue against RAM-only rule edit/readback and fault boundaries."""
import argparse
import copy
import functools
import http.server
import json
import time
from urllib.parse import urlparse
from operations_ui_fixture import OperationsFixture
from management_ui_fixture import TOKEN

DAILY_ID = '11111111-1111-4111-8111-111111111111'
ACTIVE_ID = '22222222-2222-4222-8222-222222222222'
COMMENT_ID = '33333333-3333-4333-8333-333333333333'
RUNNING_ID = '44444444-4444-4444-8444-444444444444'


class AutomationEditFixture(OperationsFixture):
    @classmethod
    def reset(cls):
        super().reset()
        daily = {'ruleId': DAILY_ID, 'name': '隔离每日提醒', 'trigger': 'schedule.daily', 'action': 'notify_device',
                 'enabled': False, 'version': 1, 'arguments': {'localTime': '08:00', 'zone': 'Asia/Shanghai', 'title': '原提醒标题', 'content': '原提醒内容'}}
        cls.qa.update(detail_mode='success', detail_delay=0, rules=[daily,
            {**copy.deepcopy(daily), 'ruleId': ACTIVE_ID, 'name': '启用中的规则', 'enabled': True, 'version': 2},
            {'ruleId': COMMENT_ID, 'name': '隔离评论分析', 'trigger': 'message.received', 'action': 'analyze_public_comment', 'enabled': False, 'version': 1, 'arguments': {}},
            {**copy.deepcopy(daily), 'ruleId': RUNNING_ID, 'name': '入队中的规则', 'executing': True}])

    @staticmethod
    def view(rule):
        block = 'rule_enabled' if rule['enabled'] else 'rule_execution_in_progress' if rule.get('executing') else ''
        return {**copy.deepcopy(rule), 'canEdit': not block, 'editBlock': block}

    def do_GET(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/v1/automation/rules'):
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'GET', 'path': path})
            if path == '/api/v1/automation/rules':
                value = {'items': [self.view(rule) for rule in state['rules']]}
                mode, delay = state['read_mode'], state['read_delay']
            else:
                rule = next((r for r in state['rules'] if r['ruleId'] == path.split('/')[-1]), None)
                if rule is None:
                    return self.json_response(404, {'error': 'not_found'})
                value = self.view(rule)
                mode, delay = state['detail_mode'], state['detail_delay']
        time.sleep(delay)
        if mode == 'invalid':
            return self.json_response(200, {})
        if mode != 'success':
            return self.json_response(401 if mode == 'unauthorized' else 503, {'error': 'unauthorized' if mode == 'unauthorized' else 'unavailable'})
        try:
            return self.json_response(200, value)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_PUT(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/v1/automation/rules/') or path.endswith('/enabled'):
            return super().do_PUT()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'PUT', 'path': path, 'body': body})
            mode, delay = state['next_write'], state['write_delay']
            state['next_write'] = 'success'
            if mode == 'drop_before':
                return self.drop()
            if mode in ('error', 'deny', 'unauthorized', 'conflict'):
                return self.json_response({'error': 503, 'deny': 400, 'unauthorized': 401, 'conflict': 409}[mode],
                    {'error': 'unauthorized' if mode == 'unauthorized' else 'automation_version_conflict' if mode == 'conflict' else 'invalid_request'})
            rule = next(r for r in state['rules'] if r['ruleId'] == path.split('/')[-1])
            args = {key: body[key] for key in ('localTime', 'zone', 'title', 'content') if key in body} if rule['action'] == 'notify_device' else {}
            args.update({k+'Filter': v for k, v in body.get('conditions', {}).items() if v})
            same = body['name'] == rule['name'] and args == rule['arguments']
            if rule['version'] != body['expectedVersion'] and not (same and rule['version'] == body['expectedVersion'] + 1 and not rule['enabled']):
                return self.json_response(409, {'error': 'automation_version_conflict'})
            if not self.view(rule)['canEdit']:
                return self.json_response(409, {'error': self.view(rule)['editBlock']})
            if not same:
                rule.update(name=body['name'], arguments=args, version=rule['version'] + 1)
            value = self.view(rule)
        time.sleep(delay)
        try:
            if mode == 'drop_after':
                return self.drop()
            return self.json_response(200, {} if mode == 'invalid' else value)
        except (BrokenPipeError, ConnectionResetError):
            pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', required=True)
    parser.add_argument('--port', type=int, default=18813)
    args = parser.parse_args()
    AutomationEditFixture.reset()
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), functools.partial(AutomationEditFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
