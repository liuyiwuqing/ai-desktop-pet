#!/usr/bin/env python3
"""Explicit content export against RAM-only rows, with real browser downloads."""
import argparse
import functools
import http.server
import time
from urllib.parse import urlparse
from metadata_export_ui_fixture import MetadataFixture


class ContentFixture(MetadataFixture):
    def do_GET(self):
        if urlparse(self.path).path != '/api/v1/messages/content-export':
            return super().do_GET()
        with self.lock:
            self.state['exportRequests'].append(self.path)
            mode, delay = self.state['exportMode'], self.state['exportDelay']
        time.sleep(delay)
        if mode != 'success':
            return self.json_response(401 if mode == 'unauthorized' else 409, {'error': 'unauthorized' if mode == 'unauthorized' else 'content_export_too_large'})
        body = '\ufeff"messageId","title","content"\r\n"isolated","私有测试","正文，\"\"引号\"\""\r\n'.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/csv;charset=UTF-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    with http.server.ThreadingHTTPServer(('127.0.0.1', 18820), functools.partial(ContentFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
