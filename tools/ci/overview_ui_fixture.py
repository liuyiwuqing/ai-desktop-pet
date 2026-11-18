#!/usr/bin/env python3
"""Actual Vue with a loopback RAM-only overview boundary, not a CORE integration."""
import argparse
import http.server
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import time
from management_ui_fixture import CONTROL, TOKEN, Fixture

BUCKETS = {'15m': 900, '1h': 3600, '6h': 21600, '1d': 86400}
AS_OF = datetime(2026, 11, 13, 10, 0, tzinfo=timezone.utc)


class OverviewFixture(Fixture):
    overview_mode = 'success'
    overview_delay = 0
    overview_requests = 0
    trend_mode = 'success'

    def trend(self, mode):
        query = parse_qs(urlparse(self.path).query)
        hours = int(query.get('hours', ['24'])[0])
        bucket = query.get('bucket', ['1h'])[0]
        seconds = BUCKETS.get(bucket, 3600)
        total = max(1, hours * 3600 // seconds)
        last = AS_OF - timedelta(seconds=AS_OF.timestamp() % seconds)
        first = last - timedelta(seconds=seconds * (total - 1))
        stamp = '%Y-%m-%dT%H:%M:%SZ'
        points = []
        for index in range(total):
            empty = mode == 'empty'
            messages = 0 if empty else 2 + index % 4
            points.append({'at': (first + timedelta(seconds=seconds * index)).strftime(stamp),
                           'messages': messages, 'deliveries': 0 if empty else messages + 1,
                           'failures': 0 if empty else (1 if index == total - 3 else 0),
                           'partial': index == total - 1})
        totals = {key: sum(point[key] for point in points) for key in ('messages', 'deliveries', 'failures')}
        return {'asOf': AS_OF.strftime(stamp), 'from': points[0]['at'], 'to': AS_OF.strftime(stamp),
                'hours': hours, 'bucket': bucket, 'bucketSeconds': seconds, 'points': points, 'totals': totals}

    def do_GET(self):
        if urlparse(self.path).path == '/api/v1/connectors':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            return self.json_response(200, {'items': [{'id': 'halo-ui', 'source': 'halo', 'configured': True,
                                                      'enabled': True, 'state': 'ready'}]})
        if urlparse(self.path).path == '/api/v1/overview/trend':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            mode = type(self).trend_mode
            if mode in ('error', 'unauthorized'):
                return self.json_response(401 if mode == 'unauthorized' else 503,
                                          {'error': 'unauthorized' if mode == 'unauthorized' else 'unavailable'})
            return self.json_response(200, self.trend(mode))
        if urlparse(self.path).path != '/api/v1/overview':
            return super().do_GET()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        with self.lock:
            cls = type(self)
            cls.overview_requests += 1
            mode, delay = cls.overview_mode, cls.overview_delay
        time.sleep(delay)
        if mode in ('error', 'unauthorized'):
            return self.json_response(401 if mode == 'unauthorized' else 503, {'error': 'unauthorized' if mode == 'unauthorized' else 'unavailable'})
        value = {'asOf': '2026-11-13T10:00:00Z', 'dayStart': '2026-11-13T00:00:00Z', 'timezone': 'UTC',
                 'devices': {'total': 5, 'online': 2, 'offline': 3, 'disabled': 1},
                 'today': {'messages': 7, 'deliveries': 12, 'failedDeliveries': 3, 'deliveryFailureRate': 0.25},
                 'backlog': {'pending': 4, 'sent': 2}, 'failedDeliveries': 8, 'mqtt': 'ready'}
        if mode == 'empty':
            for key in ('devices', 'today', 'backlog'):
                value[key] = {k: 0 for k in value[key]}
            value['today']['deliveryFailureRate'] = None
            value['failedDeliveries'] = 0
        if mode == 'mqtt_down':
            value['mqtt'] = 'unavailable'
        try:
            self.json_response(200, value)
        except (BrokenPipeError, ConnectionResetError):
            pass  # A navigation/logout deliberately aborts the delayed read.

    def do_POST(self):
        if urlparse(self.path).path != '/__qa/overview':
            return super().do_POST()
        if self.headers.get('X-QA-Key') != CONTROL:
            return self.json_response(401, {'error': 'unauthorized'})
        value = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        with self.lock:
            cls = type(self)
            if 'mode' in value:
                cls.overview_mode = value['mode']
            if 'trendMode' in value:
                cls.trend_mode = value['trendMode']
            if 'delay' in value:
                cls.overview_delay = min(max(float(value['delay']), 0), 2)
            return self.json_response(200, {'requests': cls.overview_requests})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18806)
    args = parser.parse_args()
    root = args.frontend.resolve()
    if not (root / 'index.html').is_file():
        parser.error('build the actual CORE frontend first')
    handler = lambda *a, **kw: OverviewFixture(*a, directory=str(root), **kw)
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler) as server:
        print('UI boundary fixture ready', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
