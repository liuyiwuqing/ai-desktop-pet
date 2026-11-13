#!/usr/bin/env python3
"""Empty public-memory provider for the disposable Linux delivery check only."""
import http.server
import json
import os


class Provider(http.server.BaseHTTPRequestHandler):
    searches = 0

    def respond(self, status, body):
        raw = json.dumps(body).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path == '/_test/counts':
            self.respond(200, {'searches': Provider.searches})
        else:
            self.respond(404, {'error': 'fixture_path_rejected'})

    def do_POST(self):
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 4096:
                raise ValueError()
            body = json.loads(self.rfile.read(length))
            expected = {'query', 'target_uri', 'limit', 'level', 'context_type', 'tags'}
            headers = {'X-API-Key': os.environ['NEXUS_MEMORY_API_KEY'],
                       'X-OpenViking-Account': 'isolated-account', 'X-OpenViking-User': 'isolated-user',
                       'X-OpenViking-Actor-Peer': 'linux-rotation'}
            if (self.path != '/api/v1/search/find' or not isinstance(body, dict) or set(body) != expected
                    or not isinstance(body['query'], str) or not body['query']
                    or type(body['limit']) is not int or not 1 <= body['limit'] <= 10
                    or body['target_uri'] != 'viking://user/isolated-user/resources/nexus-pet/public-comments/'
                    or body['level'] != [2] or body['context_type'] != ['resource']
                    or body['tags'] != ['project=nexus-pet', 'data_class=public_comment']
                    or any(self.headers.get(key) != value for key, value in headers.items())):
                raise ValueError()
        except (KeyError, ValueError, TypeError):
            self.respond(400, {'error': 'fixture_request_rejected'})
            return
        Provider.searches += 1
        self.respond(200, {'status': 'ok', 'result': {'resources': []}})

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    http.server.HTTPServer(('127.0.0.1', 19099), Provider).serve_forever()
