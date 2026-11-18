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
# 钻取后必须看到的一行结果：两类投递各一行，用于证明页面带上了筛选而不是只换了标签页。
DELIVERY_ROWS = [
    {'eventId': 'drill-pending', 'messageId': 'drill-message-pending', 'deviceId': 'dodo-ui-fixture',
     'title': '等待投递样本', 'status': 'pending', 'attempts': 0, 'canCancel': True, 'lastError': None,
     'nextAttemptAt': AS_OF.strftime('%Y-%m-%dT%H:%M:%SZ'), 'readAt': None},
    {'eventId': 'drill-failed', 'messageId': 'drill-message-failed', 'deviceId': 'dodo-ui-fixture',
     'title': '失败投递样本', 'status': 'failed', 'attempts': 3, 'canCancel': False, 'lastError': 'ack_timeout',
     'nextAttemptAt': AS_OF.strftime('%Y-%m-%dT%H:%M:%SZ'), 'readAt': None},
    {'eventId': 'drill-broker', 'messageId': 'drill-message-broker', 'deviceId': 'dodo-ui-fixture',
     'title': 'Broker 失败样本', 'status': 'failed', 'attempts': 2, 'canCancel': False,
     'lastError': 'broker_unavailable', 'nextAttemptAt': AS_OF.strftime('%Y-%m-%dT%H:%M:%SZ'), 'readAt': None},
]
# 今日失败原因的折行；计数字面量与各模式的 today.failedDeliveries 对齐。
FAILURE_REASONS = {'success': [('ack_timeout', 2), ('broker_unavailable', 1)],
                   'alerts': [('ack_timeout', 6), ('broker_unavailable', 3)]}
# 钻取用的设备：一台启用但离线、一台已停用；两者都不在线，才能验证「离线」不是「全部非在线」。
ALERT_DEVICES = [
    {'deviceId': 'dodo-ui-offline', 'name': '离线样机', 'enabled': True, 'online': False,
     'lastSeen': '2026-11-13T09:58:00Z', 'wifiIp': None, 'wifiRssi': None, 'wifiSsid': None, 'telemetryAt': None},
    {'deviceId': 'dodo-ui-disabled', 'name': '停用样机', 'enabled': False, 'online': False,
     'lastSeen': '2026-11-12T20:00:00Z', 'wifiIp': None, 'wifiRssi': None, 'wifiSsid': None, 'telemetryAt': None},
]
# 分页样本：22 在线、2 启用离线、2 停用；24 台一页，第二页只剩停用的两台。
MANY_DEVICES = [
    {'deviceId': 'dodo-many-%02d' % index, 'name': '分页设备 %02d' % index,
     'enabled': index < 24, 'online': index < 22,
     'lastSeen': '2026-11-13T09:58:00Z' if index < 22 else None,
     'wifiIp': None, 'wifiRssi': None, 'wifiSsid': None, 'telemetryAt': None}
    for index in range(26)
]
# 顶栏搜索样本：120 台超过默认每页 100 台，只有搜索才能选到第 120 台。
CROWD_DEVICES = [
    {'deviceId': 'dodo-crowd-%03d' % index, 'name': '海量设备 %03d' % index,
     'enabled': True, 'online': index < 100,
     'lastSeen': '2026-11-13T09:58:00Z' if index < 100 else None,
     'wifiIp': None, 'wifiRssi': None, 'wifiSsid': None, 'telemetryAt': None}
    for index in range(120)
]


def alerts_for(value, thresholds):
    """Mirror the documented alert contract; the real rule is pinned by the CORE tests."""
    devices, today, backlog = value['devices'], value['today'], value['backlog']
    alerts = []
    if value['mqtt'] != 'ready':
        alerts.append({'code': 'mqtt_connection', 'level': 'error', 'value': 0,
                       'threshold': 1, 'comparison': 'lt', 'unit': 'state', 'target': None})
    enabled = devices['total'] - devices['disabled']
    online_enabled = devices['online'] if devices['online'] else 0
    if enabled > 0 and online_enabled < thresholds['onlineMin']:
        alerts.append({'code': 'devices_online', 'level': 'warn', 'value': online_enabled,
                       'threshold': thresholds['onlineMin'], 'comparison': 'lt', 'unit': 'count',
                       'target': {'tab': 'devices', 'status': 'offline'}})
    if backlog['pending'] >= thresholds['backlogPending']:
        alerts.append({'code': 'delivery_backlog', 'level': 'warn', 'value': backlog['pending'],
                       'threshold': thresholds['backlogPending'], 'comparison': 'gte', 'unit': 'count',
                       'target': {'tab': 'deliveries', 'status': 'pending'}})
    if today['deliveries'] >= thresholds['failureRateMinSample']:
        rate = today['failedDeliveries'] / today['deliveries']
        if rate >= thresholds['failureRate']:
            alerts.append({'code': 'delivery_failure_rate', 'level': 'warn', 'value': rate,
                           'threshold': thresholds['failureRate'], 'comparison': 'gte', 'unit': 'ratio',
                           'target': {'tab': 'deliveries', 'status': 'failed'}})
    return alerts


class OverviewFixture(Fixture):
    overview_mode = 'success'
    overview_delay = 0
    overview_requests = 0
    trend_mode = 'success'
    thresholds = dict(THRESHOLDS)
    delivery_requests = []
    trend_requests = []
    device_requests = []
    # 阈值配置的版本与审计：只在 PUT 时推进；history 保留全部行，读取时只给最新 20 条。
    threshold_version = 1
    threshold_updated_at = '2026-11-13T10:00:00Z'
    threshold_updates = []
    threshold_history = []
    audit_requests = []

    def trend(self, mode):
        query = parse_qs(urlparse(self.path).query)
        hours = int(query.get('hours', ['24'])[0])
        bucket = query.get('bucket', ['1h'])[0]
        reason = query.get('reason', [''])[0]
        seconds = BUCKETS.get(bucket, 3600)
        total = max(1, hours * 3600 // seconds)
        last = AS_OF - timedelta(seconds=AS_OF.timestamp() % seconds)
        first = last - timedelta(seconds=seconds * (total - 1))
        stamp = '%Y-%m-%dT%H:%M:%SZ'
        points = []
        empty = mode == 'empty'
        for index in range(total):
            messages = 0 if empty else 2 + index % 4
            # 同一桶里两类原因各一次；reason 只收窄失败列，折行仍保留整个窗口。
            failures = 0 if empty or index != total - 3 else (1 if reason in ('ack_timeout', 'busy') else 0)
            if not reason and not empty and index == total - 3:
                failures = 2
            points.append({'at': (first + timedelta(seconds=seconds * index)).strftime(stamp),
                           'messages': messages, 'deliveries': 0 if empty else messages + 1,
                           'failures': failures, 'partial': index == total - 1})
        totals = {key: sum(point[key] for point in points) for key in ('messages', 'deliveries', 'failures')}
        totals['allFailures'] = 0 if empty else 2
        reasons = [] if empty else [{'code': 'ack_timeout', 'count': 1}, {'code': 'busy', 'count': 1}]
        return {'asOf': AS_OF.strftime(stamp), 'from': points[0]['at'], 'to': AS_OF.strftime(stamp),
                'hours': hours, 'bucket': bucket, 'bucketSeconds': seconds, 'reason': reason or None,
                'failureReasons': reasons, 'points': points, 'totals': totals}

    def device_list(self, rows):
        """Mirror the CORE device list: literal search, state filter, sorting, paging and counts."""
        query = parse_qs(urlparse(self.path).query)
        state = query.get('state', ['all'])[0] or 'all'
        q = query.get('q', [''])[0].strip()
        sort = query.get('sort', ['created'])[0]
        order = query.get('order', ['asc'])[0]
        page = int(query.get('page', ['1'])[0])
        size = int(query.get('size', ['100'])[0])
        all_rows = rows
        if q:
            needle = q.lower()
            rows = [row for row in rows if needle in row['name'].lower() or needle in row['deviceId'].lower()]
        online = lambda row: bool(row['online'])
        offline = lambda row: bool(row['enabled']) and not row['online']
        if sort == 'name':
            rows = sorted(rows, key=lambda row: (row['name'].lower(), row['deviceId']), reverse=order == 'desc')
        elif sort == 'last_seen':
            # Never-seen devices stay last in both directions, like the backend's NULLS LAST.
            dated = [row for row in rows if row['lastSeen']]
            undated = [row for row in rows if not row['lastSeen']]
            rows = sorted(dated, key=lambda row: row['lastSeen'], reverse=order == 'desc') + undated
        elif order == 'desc':
            rows = list(reversed(rows))
        groups = {'online': [row for row in rows if online(row)],
                  'offline': [row for row in rows if offline(row)],
                  'disabled': [row for row in rows if not row['enabled']]}
        filtered = groups.get(state, rows)
        return {'items': filtered[(page - 1) * size:page * size], 'total': len(all_rows),
                'matched': len(filtered), 'page': page, 'size': size, 'state': state,
                'q': q, 'sort': sort, 'order': order,
                'counts': {'online': sum(1 for row in rows if online(row)),
                           'offline': sum(1 for row in rows if offline(row)),
                           'disabled': sum(1 for row in rows if not row['enabled'])}}

    def do_GET(self):
        path = urlparse(self.path).path
        if path == '/api/v1/deliveries':
            # 钻取样本只属于告警演示；其它模式的投递空态/列表交给管理台夹具。
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            with self.lock:
                mode = type(self).overview_mode
            if mode != 'alerts':
                return super().do_GET()
            query = parse_qs(urlparse(self.path).query)
            status = query.get('status', [''])[0]
            reason = query.get('lastError', [''])[0]
            page = int(query.get('page', ['1'])[0])
            size = int(query.get('size', ['30'])[0])
            rows = [row for row in DELIVERY_ROWS
                    if (not status or row['status'] == status) and (not reason or row['lastError'] == reason)]
            with self.lock:
                type(self).delivery_requests.append(self.path)
            return self.json_response(200, {'items': rows[(page - 1) * size:page * size],
                                            'total': len(rows), 'page': page, 'size': size})
        if path == '/api/v1/devices':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            with self.lock:
                mode = type(self).overview_mode
                type(self).device_requests.append(self.path)
            # 告警模式给两台"非在线"设备；分页模式给 26 台；海量模式给 120 台；其它模式沿用管理台夹具的在线设备。
            rows = (MANY_DEVICES if mode == 'many' else CROWD_DEVICES if mode == 'crowd'
                    else ALERT_DEVICES if mode == 'alerts' else None)
            if rows is None:
                return super().do_GET()
            return self.json_response(200, self.device_list(rows))
        if path == '/api/v1/overview/thresholds/audit':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            query = parse_qs(urlparse(self.path).query)
            try:
                before = int(query['before'][0]) if 'before' in query else None
                size = int(query.get('size', ['20'])[0])
            except ValueError:
                return self.json_response(400, {'error': 'invalid_request'})
            with self.lock:
                cls = type(self)
                cls.audit_requests.append(self.path)
                rows = [row for row in cls.threshold_history if before is None or row['sequence'] < before]
                items = rows[:max(size, 1)]
                return self.json_response(200, {'items': items, 'total': len(cls.threshold_history),
                                                'hasMore': len(rows) > len(items)})
        if path == '/api/v1/overview/thresholds':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            with self.lock:
                cls = type(self)
                value = {'thresholds': dict(cls.thresholds), 'version': cls.threshold_version,
                         'updatedBy': 'overview-ui-fixture', 'updatedAt': cls.threshold_updated_at,
                         'history': list(cls.threshold_history[:20]), 'historyTotal': len(cls.threshold_history)}
            return self.json_response(200, value)
        if path == '/api/v1/connectors':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            return self.json_response(200, {'items': [{'id': 'halo-ui', 'source': 'halo', 'configured': True,
                                                      'enabled': True, 'state': 'ready'}]})
        if urlparse(self.path).path == '/api/v1/overview/trend':
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                return self.json_response(401, {'error': 'unauthorized'})
            with self.lock:
                cls = type(self)
                mode = cls.trend_mode
                cls.trend_requests.append(self.path)
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
        value = {'asOf': '2026-11-13T10:00:00Z', 'dayStart': '2026-11-12T16:00:00Z', 'timezone': 'Asia/Shanghai',
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
        # 折行合计必须与 today.failedDeliveries 相等：夹具也不允许两个数字各说各话。
        reasons = [] if mode == 'empty' else FAILURE_REASONS['alerts' if mode == 'alerts' else 'success']
        value['failureReasons'] = [{'code': code, 'count': n} for code, n in reasons]
        try:
            self.json_response(200, value)
        except (BrokenPipeError, ConnectionResetError):
            pass  # A navigation/logout deliberately aborts the delayed read.

    def do_PUT(self):
        if urlparse(self.path).path != '/api/v1/overview/thresholds':
            return super().do_PUT()
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            return self.json_response(401, {'error': 'unauthorized'})
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        wanted = body.get('thresholds') or {}
        keys = ('failureRate', 'failureRateMinSample', 'backlogPending', 'onlineMin')
        with self.lock:
            cls = type(self)
            if body.get('version') != cls.threshold_version:
                return self.json_response(409, {'error': 'alert_threshold_version_conflict'})
            if any(not isinstance(wanted.get(key), (int, float)) for key in keys):
                return self.json_response(400, {'error': 'invalid_alert_thresholds'})
            cls.thresholds = {**THRESHOLDS, **wanted}
            cls.threshold_version += 1
            cls.threshold_updated_at = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
            sequence = (cls.threshold_history[0]['sequence'] + 1) if cls.threshold_history else 1
            cls.threshold_history.insert(0, {'sequence': sequence, 'actor': 'overview-ui-fixture',
                                             'fromVersion': body['version'], 'toVersion': cls.threshold_version,
                                             **{key: cls.thresholds[key] for key in keys},
                                             'recordedAt': cls.threshold_updated_at})
            cls.threshold_updates.append({'version': body['version'], 'thresholds': dict(cls.thresholds)})
            value = {'thresholds': dict(cls.thresholds), 'version': cls.threshold_version,
                     'updatedBy': 'overview-ui-fixture', 'updatedAt': cls.threshold_updated_at,
                     'history': list(cls.threshold_history[:20]), 'historyTotal': len(cls.threshold_history)}
        return self.json_response(200, value)

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
            if 'thresholdHistory' in value:
                count = min(max(int(value['thresholdHistory']), 0), 100)
                stamp = AS_OF.strftime('%Y-%m-%dT%H:%M:%SZ')
                cls.threshold_history = [{'sequence': i, 'actor': 'overview-ui-fixture',
                                          'fromVersion': i - 1, 'toVersion': i,
                                          'failureRate': 0.1, 'failureRateMinSample': 20,
                                          'backlogPending': 100, 'onlineMin': 1, 'recordedAt': stamp}
                                         for i in range(count, 0, -1)]
                cls.threshold_version = max(count, 1)
            return self.json_response(200, {'requests': cls.overview_requests,
                                            'deliveryRequests': list(cls.delivery_requests),
                                            'deviceRequests': list(cls.device_requests),
                                            'trendRequests': list(cls.trend_requests),
                                            'auditRequests': list(cls.audit_requests),
                                            'thresholdVersion': cls.threshold_version,
                                            'thresholdUpdates': list(cls.threshold_updates)})


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
