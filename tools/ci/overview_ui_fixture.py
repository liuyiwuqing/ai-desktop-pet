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
THRESHOLDS = {'failureRate': 0.1, 'failureRateMinSample': 20, 'backlogPending': 100, 'onlineMin': 1}


def alerts_for(value, thresholds):
    """Mirror the documented alert contract; the real rule is pinned by the CORE tests."""
    devices, today, backlog = value['devices'], value['today'], value['backlog']
    alerts = []
    if value['mqtt'] != 'ready':
        alerts.append({'code': 'mqtt_connection', 'level': 'error', 'value': 0,
                       'threshold': 1, 'comparison': 'lt', 'unit': 'state'})
    enabled = devices['total'] - devices['disabled']
    online_enabled = devices['online'] if devices['online'] else 0
    if enabled > 0 and online_enabled < thresholds['onlineMin']:
        alerts.append({'code': 'devices_online', 'level': 'warn', 'value': online_enabled,
                       'threshold': thresholds['onlineMin'], 'comparison': 'lt', 'unit': 'count'})
    if backlog['pending'] >= thresholds['backlogPending']:
        alerts.append({'code': 'delivery_backlog', 'level': 'warn', 'value': backlog['pending'],
                       'threshold': thresholds['backlogPending'], 'comparison': 'gte', 'unit': 'count'})
    if today['deliveries'] >= thresholds['failureRateMinSample']:
        rate = today['failedDeliveries'] / today['deliveries']
        if rate >= thresholds['failureRate']:
            alerts.append({'code': 'delivery_failure_rate', 'level': 'warn', 'value': rate,
                           'threshold': thresholds['failureRate'], 'comparison': 'gte', 'unit': 'ratio'})
    return alerts


class OverviewFixture(Fixture):
    overview_mode = 'success'
    overview_delay = 0
    overview_requests = 0
    trend_mode = 'success'
    thresholds = dict(THRESHOLDS)

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
            mode, delay, thresholds = cls.overview_mode, cls.overview_delay, dict(cls.thresholds)
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
        if mode == 'alerts':
            # 三处同时越线：无在线设备、积压 120、失败率 9/40=22.5%。
            value['devices'] = {'total': 5, 'online': 0, 'offline': 5, 'disabled': 1}
            value['today'] = {'messages': 40, 'deliveries': 40, 'failedDeliveries': 9, 'deliveryFailureRate': 0.225}
            value['backlog'] = {'pending': 120, 'sent': 2}
            value['failedDeliveries'] = 9
        value['thresholds'] = thresholds
        value['alerts'] = alerts_for(value, thresholds)
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
            if 'thresholds' in value:
                cls.thresholds = {**THRESHOLDS, **value['thresholds']}
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
