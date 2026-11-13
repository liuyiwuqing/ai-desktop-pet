#!/usr/bin/env python3
"""Loopback RAM-only delivery history/cancel fixture for the actual Vue build."""
import argparse
import copy
import http.server
import json
from pathlib import Path
import socket
import time
from urllib.parse import parse_qs, urlparse
from management_ui_fixture import CONTROL, TOKEN, Fixture


class DeliveryFixture(Fixture):
    rows = []
    histories = {}
    requests = []
    history_mode = 'success'
    history_delay = 0
    next_cancel = 'success'
    cancel_delay = 0

    @classmethod
    def reset(cls):
        cls.rows = []
        cls.histories = {}
        cls.requests = []
        cls.history_mode = 'success'
        cls.history_delay = 0
        cls.next_cancel = 'success'
        cls.cancel_delay = 0
        for i, (status, attempts, can_cancel) in enumerate([('pending', 0, True), ('pending', 0, False), ('failed', 2, False), ('displayed', 1, False)]):
            row = {'eventId': 'delivery-history-' + str(i), 'messageId': 'history-message-' + str(i),
                   'deviceId': 'dodo-ui-fixture', 'title': '状态时间线 ' + str(i), 'status': status,
                   'attempts': attempts, 'canCancel': can_cancel, 'lastError': 'ack_timeout' if status == 'failed' else None,
                   'nextAttemptAt': '2026-11-13T10:00:00Z', 'readAt': None}
            cls.rows.append(row)
            cls.histories[row['eventId']] = [cls.event(row, 'baseline' if i == 1 else 'created')]

    @classmethod
    def event(cls, row, kind, previous=None):
        return {'sequence': len(cls.histories.get(row['eventId'], [])) + 1,
                'kind': kind, 'recordedAt': '2026-11-13T10:00:00Z', 'previousStatus': previous,
                'status': row['status'], 'attempts': row['attempts'], 'lastError': row['lastError'],
                'nextAttemptAt': row['nextAttemptAt'], 'receivedAt': None, 'displayedAt': None, 'readAt': None}

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != '/api/v1/deliveries' and not (parsed.path.startswith('/api/v1/deliveries/') and parsed.path.endswith('/history')):
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            cls = type(self)
            cls.requests.append({'method': 'GET', 'path': self.path})
            query = parse_qs(parsed.query)
            page, size = int(query.get('page', ['1'])[0]), int(query.get('size', ['30'])[0])
            mode, delay = cls.history_mode, cls.history_delay
            if parsed.path.endswith('/history'):
                row = next((r for r in cls.rows if r['eventId'] == parsed.path.split('/')[-2]), None)
                if row is None:
                    return self.json_response(404, {'error': 'delivery_not_found'})
                items = copy.deepcopy(cls.histories[row['eventId']])
                if mode == 'empty':
                    items = []
                value = {'items': items[(page-1)*size:page*size], 'total': len(items), 'page': page, 'size': size,
                         'includesCreation': bool(items) and items[0]['kind'] == 'created', 'delivery': copy.deepcopy(row)}
            else:
                rows = [copy.deepcopy(r) for r in cls.rows if not query.get('status') or r['status'] == query['status'][0]]
                value = {'items': rows[(page-1)*size:page*size], 'total': len(rows), 'page': page, 'size': size}
                delay, mode = 0, 'success'
        time.sleep(delay)
        try:
            if mode in ('error', 'unauthorized'):
                return self.json_response(401 if mode == 'unauthorized' else 503, {'error': 'unauthorized' if mode == 'unauthorized' else 'unavailable'})
            return self.json_response(200, value)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        path = urlparse(self.path).path
        if path == '/__qa/deliveries':
            if self.headers.get('X-QA-Key') != CONTROL:
                return self.json_response(401, {'error': 'unauthorized'})
            value = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
            with self.lock:
                cls = type(self)
                if value.get('reset'):
                    cls.reset()
                for key in ('history_mode', 'next_cancel'):
                    if key in value:
                        setattr(cls, key, value[key])
                for key in ('history_delay', 'cancel_delay'):
                    if key in value:
                        setattr(cls, key, min(max(float(value[key]), 0), 2))
                if value.get('many'):
                    cls.histories[cls.rows[0]['eventId']] = [dict(cls.event(cls.rows[0], 'created' if i == 0 else 'detail_changed'), sequence=i+1) for i in range(31)]
                return self.json_response(200, {'requests': cls.requests, 'rows': cls.rows})
        if not (path.startswith('/api/v1/deliveries/') and path.endswith('/cancel')):
            return super().do_POST()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            cls = type(self)
            cls.requests.append({'method': 'POST', 'path': path})
            row = next((r for r in cls.rows if r['eventId'] == path.split('/')[-2]), None)
            if row is None:
                return self.json_response(404, {'error': 'delivery_not_found'})
            mode, delay = cls.next_cancel, cls.cancel_delay
            cls.next_cancel = 'success'
            if mode == 'conflict':
                row.update(canCancel=False, attempts=1)
                cls.histories[row['eventId']].append(cls.event(row, 'attempt_reserved', 'pending'))
            elif mode != 'drop_before' and row['canCancel']:
                row.update(status='cancelled', canCancel=False, lastError='cancelled_by_admin')
                cls.histories[row['eventId']].append(cls.event(row, 'state_changed', 'pending'))
            elif mode != 'drop_before' and row['status'] != 'cancelled':
                return self.json_response(409, {'error': 'delivery_not_cancellable'})
        time.sleep(delay)
        if mode in ('drop_before', 'drop_after'):
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        if mode == 'conflict':
            return self.json_response(409, {'error': 'delivery_not_cancellable'})
        self.send_response(204)
        self.end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18807)
    args = parser.parse_args()
    root = args.frontend.resolve()
    if not (root / 'index.html').is_file():
        parser.error('build the actual CORE frontend first')
    DeliveryFixture.reset()
    handler = lambda *a, **kw: DeliveryFixture(*a, directory=str(root), **kw)
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
