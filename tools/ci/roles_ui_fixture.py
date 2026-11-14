#!/usr/bin/env python3
"""Actual Vue role presentation against RAM-only access and audit APIs."""
import argparse
import functools
import http.server
import json
from urllib.parse import urlparse
from sources_ui_fixture import SourcesFixture
from management_ui_fixture import TOKEN


class RolesFixture(SourcesFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(role='admin', role_tokens=[])

    def do_GET(self):
        path = urlparse(self.path).path
        if path == '/api/v1/access/tokens':
            return self.json_response(200, {'items': type(self).qa['role_tokens']})
        if path == '/api/v1/audit':
            return self.json_response(200, {'items': [{'auditId': 'fixture-audit', 'actor': 'fixture-actor', 'role': 'viewer', 'method': 'POST', 'target': '/api/v1/messages', 'status': 403, 'createdAt': '2026-11-15T00:00:00Z', 'completedAt': '2026-11-15T00:00:01Z'}], 'total': 1})
        return super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        if path == '/api/v1/session':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            return self.json_response(200, {'role': type(self).qa['role'], 'actor': 'fixture-actor'})
        if path.startswith('/api/v1/access/tokens'):
            body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
            with self.lock:
                state = type(self).qa
                state['requests'].append({'method': 'POST', 'path': path, 'body': body})
                if path.endswith('/revoke'):
                    state['role_tokens'][0]['revokedAt'] = '2026-11-15T00:00:00Z'
                    return self.json_response(204, None)
                item = {'tokenId': 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa', 'name': body['name'], 'role': body['role'], 'expiresAt': body['expiresAt'], 'revokedAt': None, 'version': 1}
                state['role_tokens'].append(item)
                return self.json_response(200, {**item, 'token': 'fixture-role-token-not-a-real-secret'})
        return super().do_POST()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    RolesFixture.reset()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 18817), functools.partial(RolesFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
