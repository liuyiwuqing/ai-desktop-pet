#!/usr/bin/env python3
"""RAM template CRUD/preview boundary for actual Vue regression."""
import argparse
import copy
import functools
import json
from urllib.parse import urlparse
from roles_ui_fixture import RolesFixture
from management_ui_fixture import FixtureServer


class TemplatesFixture(RolesFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(templates=[])

    def do_GET(self):
        path = urlparse(self.path).path
        if path == '/api/v1/templates':
            return self.json_response(200, {'items': [t for t in type(self).qa['templates'] if not t['deleted']], 'variables': ['source', 'title', 'summary', 'content', 'sender', 'eventType']})
        if path.startswith('/api/v1/templates/'):
            return self.json_response(200, next(t for t in type(self).qa['templates'] if t['templateId'] == path.split('/')[-1]))
        return super().do_GET()

    def do_PUT(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/v1/templates/'):
            return super().do_PUT()
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            state = type(self).qa
            state['requests'].append({'method': 'PUT', 'path': path, 'body': body})
            mode = state['next_write']
            state['next_write'] = 'success'
            if mode == 'unauthorized':
                return self.json_response(401, {'error': 'unauthorized'})
            if mode == 'error':
                return self.json_response(503, {'error': 'unavailable'})
            current = next((t for t in state['templates'] if t['templateId'] == path.split('/')[-1]), None)
            if current:
                if current['definition'] != body['definition']:
                    current.update(definition=body['definition'], version=current['version'] + 1)
            else:
                current = {'templateId': path.split('/')[-1], 'version': 1, 'definition': body['definition'], 'deleted': False}
                state['templates'].append(current)
            value = copy.deepcopy(current)
        return self.drop() if mode == 'drop_after' else self.json_response(200, value)

    def do_POST(self):
        if urlparse(self.path).path != '/api/v1/templates/preview':
            return super().do_POST()
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        definition, message = body['definition'], body['message']
        rendered = {}
        for key in ('title', 'summary'):
            value = definition[key]
            for name, content in message.items():
                value = value.replace('{' + name + '}', str(content))
            rendered[key] = value
        return self.json_response(200, {**rendered, 'eventType': definition['icon'] + '.template',
                                        'sound': definition['sound'],
                                        'durationMs': int(float(definition['durationSeconds']) * 1000)})

    def do_DELETE(self):
        if not self.path.startswith('/api/v1/templates/'):
            return super().do_DELETE()
        path = urlparse(self.path).path
        next(t for t in type(self).qa['templates'] if t['templateId'] == path.split('/')[-1])['deleted'] = True
        return self.json_response(204, None)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    TemplatesFixture.reset()
    with FixtureServer(('127.0.0.1', 18818), functools.partial(TemplatesFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
