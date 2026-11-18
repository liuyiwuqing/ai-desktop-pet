#!/usr/bin/env python3
"""Versioned SSE and onboarding with RAM state; no actual CORE or devices."""
import argparse
import copy
import functools
import json
import time
from urllib.parse import urlparse
from roles_ui_fixture import RolesFixture
from management_ui_fixture import FixtureServer, TOKEN


class EventsFixture(RolesFixture):
    @classmethod
    def reset(cls):
        super().reset()
        cls.qa.update(events='ready', revision=1, devices=[], streams=0, setup=[])

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ('/api/v1/devices', '/api/v1/setup'):
            with self.lock:
                state = type(self).qa
                state['requests'].append({'method': 'GET', 'path': path})
                value = {'items': copy.deepcopy(state['devices'])} if path.endswith('devices') else {'devices': copy.deepcopy(state['setup']), 'mqtt': 'ready', 'credentialsProvisioned': 'unknown'}
            return self.json_response(200, value)
        if path != '/api/v1/events':
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            state = type(self).qa
            state['streams'] += 1
            mode = state['events']
            revision = state['revision']
        if mode == 'unavailable':
            return self.json_response(503, {'error': 'unavailable'})
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()

        def send(event, version):
            data = ('event:' + event + '\r\ndata:' + json.dumps({'schemaVersion': 1, 'revision': str(version)}) + '\r\n\r\n').encode()
            self.wfile.write(data[:13])
            self.wfile.flush()
            self.wfile.write(data[13:])
            self.wfile.flush()

        try:
            send('ready', revision)
            for _ in range(80):
                time.sleep(.1)
                with self.lock:
                    mode, next_revision = type(self).qa['events'], type(self).qa['revision']
                if mode == 'unauthorized':
                    send('unauthorized', next_revision)
                    break
                if mode == 'unavailable':
                    break
                send('invalidate' if revision != next_revision else 'heartbeat', next_revision)
                revision = next_revision
        except (BrokenPipeError, ConnectionResetError):
            pass
        self.close_connection = True


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--frontend', required=True)
    args = p.parse_args()
    EventsFixture.reset()
    with FixtureServer(('127.0.0.1', 18819), functools.partial(EventsFixture, directory=args.frontend)) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()
