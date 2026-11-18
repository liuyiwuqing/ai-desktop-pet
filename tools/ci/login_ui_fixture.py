#!/usr/bin/env python3
"""Loopback RAM-only session UI fixture; real authentication is tested in Java/HTTP."""
import argparse
from datetime import datetime, timezone
import functools
import json
import secrets
import socket
import time
from http.cookies import SimpleCookie
from urllib.parse import urlparse
from roles_ui_fixture import RolesFixture
from management_ui_fixture import FixtureServer, TOKEN, CONTROL


class LoginFixture(RolesFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.sessions = {}
        cls.admin_token = TOKEN
        cls.admin_revision = 1
        cls.qa.update(session_failure=False, admin_file=True, admin_writable=True)

    def reply(self, status, value, cookie=None):
        payload = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.send_header('Cache-Control', 'no-store')
        if cookie is not None:
            self.send_header('Set-Cookie', f'nexus_console_session={cookie}; Path=/api/v1; HttpOnly; SameSite=Strict; Max-Age={28800 if cookie else 0}')
        self.end_headers()
        if self.command=='GET' and urlparse(self.path).path=='/api/v1/session' and status==200:
            self.wfile.flush()
            type(self).qa['session_headers_sent']=True
            time.sleep(type(self).qa.get('session_body_delay',0))
        self.wfile.write(payload)

    def read(self):
        return json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))) or '{}')

    def cookie_id(self):
        cookie = SimpleCookie(self.headers.get('Cookie', ''))
        return cookie['nexus_console_session'].value if 'nexus_console_session' in cookie else ''

    def identity(self):
        authorization = self.headers.get('Authorization')
        session = type(self).sessions.get(self.cookie_id()) if authorization is None else None
        token = session['token'] if session else (authorization or '')[7:]
        if token == type(self).admin_token:
            return 'admin', 'fixture-admin', session, token
        for item in type(self).qa['role_tokens']:
            if item.get('_token') == token and not item['revokedAt'] and datetime.fromisoformat(item['expiresAt'].replace('Z', '+00:00')).timestamp() > time.time():
                return item['role'], item['tokenId'], session, token
        return None

    def session(self, identity, cookie=None):
        role, actor, session, token = identity
        result = {'role': role, 'actor': actor}
        if session:
            result.update(persisted=True, csrfToken=session['csrf'], expiresAt='2099-01-01T00:00:00Z')
        return self.reply(200, result, cookie)

    def new_session(self, token):
        type(self).sessions.pop(self.cookie_id(), None)
        key = secrets.token_urlsafe(32)
        type(self).sessions[key] = {'token': token, 'csrf': secrets.token_urlsafe(32)}
        return key, type(self).sessions[key]

    def parent(self, method):
        if 'Authorization' in self.headers:
            self.headers.replace_header('Authorization', 'Bearer ' + TOKEN)
        else:
            self.headers['Authorization'] = 'Bearer ' + TOKEN
        return getattr(super(), method)()

    def do_GET(self):
        path = urlparse(self.path).path
        if not path.startswith('/api/'):
            return super().do_GET()
        identity = self.identity()
        if path == '/api/v1/session' and type(self).qa['session_failure']:
            return self.reply(503, {'error': 'authentication_configuration_unavailable'})
        if not identity:
            return self.reply(401, {'error': 'unauthorized'}, '' if self.cookie_id() else None)
        if path == '/api/v1/session':
            return self.session(identity)
        if path == '/api/v1/access/admin-token':
            state = type(self).qa
            return self.reply(200, {'mode': 'file' if state['admin_file'] else 'environment', 'editable': state['admin_file'] and state['admin_writable'], 'revision': type(self).admin_revision})
        if path == '/api/v1/access/tokens':
            return self.reply(200, {'items': [{k: v for k, v in item.items() if k != '_token'} for item in type(self).qa['role_tokens']]})
        return self.parent('do_GET')

    def do_POST(self):
        path = urlparse(self.path).path
        if path == '/__qa/login':
            if self.headers.get('X-QA-Key') != CONTROL:
                return self.reply(401, {})
            value = self.read()
            if value.pop('invalidate_sessions', False):
                type(self).sessions.clear()
            type(self).qa.update(value)
            return self.reply(200, {'ok': True,'session_headers_sent':type(self).qa.get('session_headers_sent',False)})
        if not path.startswith('/api/'):
            return super().do_POST()
        identity = self.identity()
        if not identity:
            return self.reply(401, {'error': 'unauthorized'})
        body = self.read()
        role, actor, session, token = identity
        if session and self.headers.get('X-Nexus-CSRF') != session['csrf']:
            return self.reply(403, {'error': 'csrf_required'})
        if path == '/api/v1/session':
            if body.get('remember'):
                cookie, session = self.new_session(token)
                return self.session((role, actor, session, token), cookie)
            type(self).sessions.pop(self.cookie_id(), None)
            return self.session((role, actor, None, token), '')
        if path == '/api/v1/session/logout':
            type(self).sessions.pop(self.cookie_id(), None)
            return self.reply(204, None, '')
        if path == '/api/v1/access/tokens':
            item = {'tokenId': secrets.token_hex(16), 'name': body['name'], 'role': body['role'], 'expiresAt': body['expiresAt'], 'version': 1, 'revokedAt': None, '_token': secrets.token_urlsafe(32)}
            type(self).qa['role_tokens'].append(item)
            return self.reply(200, {'tokenId': item['tokenId'], 'token': item['_token']})
        for item in type(self).qa['role_tokens']:
            if item['tokenId'] not in path:
                continue
            if item['version'] != body.get('expectedVersion'):
                return self.reply(409, {'error': 'role_token_version_conflict'})
            item['version'] += 1
            if path.endswith('/revoke'):
                item['revokedAt'] = '2026-11-17T00:00:00Z'
                return self.reply(204, None)
            if path.endswith('/rotate'):
                item['_token'] = secrets.token_urlsafe(32)
                if type(self).qa.pop('drop_rotation', False):
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                    self.connection.close()
                    return
                return self.reply(200, {'tokenId': item['tokenId'], 'version': item['version'], 'token': item['_token']})
        return self.reply(404, {'error': 'request_failed'})

    def do_PUT(self):
        path = urlparse(self.path).path
        identity = self.identity()
        if not identity:
            return self.reply(401, {'error': 'unauthorized'})
        body = self.read()
        role, actor, session, token = identity
        if role != 'admin' or session and self.headers.get('X-Nexus-CSRF') != session['csrf']:
            return self.reply(403, {'error': 'forbidden'})
        if path == '/api/v1/access/admin-token':
            if body['currentToken'] != type(self).admin_token:
                return self.reply(400, {'error': 'invalid_current_admin_token'})
            if body['expectedRevision'] != type(self).admin_revision:
                return self.reply(409, {'error': 'admin_token_version_conflict'})
            time.sleep(type(self).qa.pop('admin_change_delay', 0))
            type(self).admin_token = body['newToken']
            type(self).admin_revision += 1
            if session:
                cookie, session = self.new_session(body['newToken'])
                result = {'role': role, 'actor': actor, 'persisted': True, 'csrfToken': session['csrf'], 'revision': type(self).admin_revision}
                return self.reply(200, result, cookie)
            return self.reply(200, {'role': role, 'actor': actor, 'revision': type(self).admin_revision})
        for item in type(self).qa['role_tokens']:
            if path.endswith(item['tokenId']):
                if item['version'] != body['expectedVersion']:
                    return self.reply(409, {'error': 'role_token_version_conflict'})
                item.update(name=body['name'], role=body['role'], expiresAt=body['expiresAt'], version=item['version'] + 1)
                return self.reply(200, {'tokenId': item['tokenId'], 'version': item['version']})
        return self.reply(404, {})


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--frontend', required=True)
    args = parser.parse_args()
    LoginFixture.reset()
    with FixtureServer(('127.0.0.1', 18828), functools.partial(LoginFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
