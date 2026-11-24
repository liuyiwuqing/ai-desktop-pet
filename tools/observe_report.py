#!/usr/bin/env python3
"""Summarise a production observation window (ndjson) into a verdict; read-only, standard library only.

The collector (server side ~/nexus-observe.sh) appends one JSON object every 15 minutes plus `start`/`stop`
markers. This script turns that stream into the §10.3 checklist: coverage, gaps, container restarts,
`/api/v1/status` availability, disk/memory/connection trends, 5xx and certificate expiry — and, crucially,
separates "the service was unhealthy" from "the collector failed to authenticate", which look identical
in the raw log but mean very different things.

Exit codes: 0 = pass, 2 = needs human review, 1 = fail (or unusable input).
"""
import argparse
import json
import sys
from datetime import datetime
from pathlib import Path

RESTART_FIELDS = ('core', 'pg', 'mqtt')
READY = ('ready',)


def parse_ts(value):
    return datetime.fromisoformat(str(value).replace('Z', '+00:00'))


def load(path):
    rows = []
    for lineno, line in enumerate(Path(path).read_text(encoding='utf-8').splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError as error:
            raise ValueError(f'{path}:{lineno} 不是合法 JSON: {error}') from error
    if not rows:
        raise ValueError(f'{path} 没有任何记录')
    return rows


def split(rows):
    """Separate collector markers from measurement samples."""
    start = next((r for r in rows if r.get('event') == 'start'), None)
    stop = next((r for r in reversed(rows) if r.get('event') == 'stop'), None)
    samples = [r for r in rows if 'ts' in r and 'core' in r]
    return start, stop, samples


def container_state(value):
    """`running/healthy/0` -> ('running', 'healthy', 0); `running/0` -> ('running', None, 0).

    Mosquitto has no healthcheck, so its field is `status/restarts` and the second segment is a number.
    """
    parts = str(value or '').split('/')
    if not parts or not parts[0]:
        return None, None, None
    status, health, restarts = parts[0], None, None
    if len(parts) >= 2:
        if parts[1].lstrip('-').isdigit():
            restarts = int(parts[1])
        else:
            health = parts[1]
            if len(parts) >= 3 and parts[2].lstrip('-').isdigit():
                restarts = int(parts[2])
    return status, health, restarts


def status_channel(sample):
    """Classify the /api/v1/status channel: ready / unhealthy / unauthorized / missing."""
    value = sample.get('status')
    if not isinstance(value, dict):
        return 'missing'
    if 'error' in value:
        text = str(value.get('error'))
        return 'unauthorized' if 'unauth' in text.lower() else 'error'
    if all(value.get(key) in READY for key in ('mqtt', 'database')):
        return 'ready'
    return 'unhealthy'


def memory_mib(text):
    """`250MiB / 1GiB` -> 250.0"""
    head = str(text or '').split('/')[0].strip()
    for unit, factor in (('GiB', 1024.0), ('MiB', 1.0), ('KiB', 1 / 1024.0)):
        if head.endswith(unit):
            try:
                return round(float(head[:-len(unit)].strip()) * factor, 1)
            except ValueError:
                return None
    return None


def cert_date(text):
    if not text:
        return None
    try:
        return datetime.strptime(str(text).strip(), '%b %d %H:%M:%S %Y GMT')
    except ValueError:
        return None


def summarise(rows, interval=900):
    start, stop, samples = split(rows)
    if not samples:
        raise ValueError('窗口内没有样本行（需要包含 core 字段的记录）')
    first, last = parse_ts(samples[0]['ts']), parse_ts(samples[-1]['ts'])
    span = (last - first).total_seconds()
    gaps = []
    for before, after in zip(samples, samples[1:]):
        delta = (parse_ts(after['ts']) - parse_ts(before['ts'])).total_seconds()
        if delta > interval * 2:
            gaps.append({'from': before['ts'], 'to': after['ts'], 'seconds': int(delta)})

    channels = {}
    for sample in samples:
        kind = status_channel(sample)
        channels[kind] = channels.get(kind, 0) + 1
    degraded = [s['ts'] for s in samples if status_channel(s) in ('unauthorized', 'error', 'unhealthy')]
    missing = [s['ts'] for s in samples if status_channel(s) == 'missing']

    restarts = {field: 0 for field in RESTART_FIELDS}
    unhealthy = []
    for sample in samples:
        for field in RESTART_FIELDS:
            status, health, count = container_state(sample.get(field))
            if isinstance(count, int):
                restarts[field] += count
            if status is not None and (status != 'running' or health not in (None, 'healthy')):
                unhealthy.append({'ts': sample['ts'], 'field': field, 'state': sample.get(field)})

    disk_avail = [s['disk_avail_kb'] for s in samples if isinstance(s.get('disk_avail_kb'), int)]
    disk_pct = [s['disk_used_pct'] for s in samples if isinstance(s.get('disk_used_pct'), int)]
    pg_conn = [s['pg_conn'] for s in samples if isinstance(s.get('pg_conn'), int)]
    memory = [m for m in (memory_mib(s.get('core_mem')) for s in samples) if m is not None]
    reconnect = [s['mqtt_reconnect_16m'] for s in samples if isinstance(s.get('mqtt_reconnect_16m'), int)]
    fivexx = [(s['ts'], s['http5xx_last200']) for s in samples if isinstance(s.get('http5xx_last200'), int) and s['http5xx_last200'] > 0]
    certs = {}
    for key in ('https_cert_not_after', 'mqtt_cert_not_after'):
        dates = [d for d in (cert_date(s.get(key)) for s in samples) if d]
        if dates:
            certs[key] = {'earliest': min(dates).isoformat(), 'latest': max(dates).isoformat()}

    checks = []

    def check(name, ok, detail, hard=True):
        checks.append({'name': name, 'ok': bool(ok), 'hard': hard, 'detail': detail})

    declared = int(start.get('duration_s')) if isinstance(start, dict) and isinstance(start.get('duration_s'), int) else None
    expected = round(declared / interval) if declared else None
    check('窗口闭环', bool(start) and bool(stop),
          f"start={'有' if start else '缺'} stop={'有' if stop else '缺'}")
    if declared:
        check('窗口时长', span >= declared * 0.98,
              f'实测 {span / 3600:.2f} h / 声明 {declared / 3600:.2f} h')
        check('样本数量', len(samples) >= expected * 0.95,
              f'{len(samples)} 条 / 期望 {expected} 条')
    check('无采样断档', not gaps,
          '无断档' if not gaps else f'{len(gaps)} 处断档，最长 {max(g["seconds"] for g in gaps) // 60} 分钟')
    check('容器未重启', sum(restarts.values()) == 0,
          f"重启计数 core={restarts['core']} pg={restarts['pg']} mqtt={restarts['mqtt']}")
    check('容器状态正常', not unhealthy,
          '全部 running/healthy' if not unhealthy else f'{len(unhealthy)} 条异常，首条 {unhealthy[0]["ts"]} {unhealthy[0]["field"]}={unhealthy[0]["state"]}')
    check('status 通道可用', not degraded and not missing,
          '全部 ready' if not degraded and not missing
          else f"ready {channels.get('ready', 0)} 条；采集通道失效 {len(degraded)} 条"
               f"（unauthorized/error/unhealthy，首条 {degraded[0] if degraded else '-'}）；无 status {len(missing)} 条",
          hard=False)
    check('5xx 恒定', not fivexx,
          '近 200 条访问日志无 5xx' if not fivexx
          else f'{len(fivexx)} 条样本的近 200 行日志含 5xx（滚动计数，不是增量），峰值 {max(v for _, v in fivexx)}，'
               f'首条 {fivexx[0][0]}',
          hard=False)

    hard_failed = [c for c in checks if c['hard'] and not c['ok']]
    soft_failed = [c for c in checks if not c['hard'] and not c['ok']]
    verdict = 'FAIL' if hard_failed else ('REVIEW' if soft_failed else 'PASS')

    return {
        'verdict': verdict,
        'window': {'start': samples[0]['ts'], 'end': samples[-1]['ts'], 'span_hours': round(span / 3600, 2),
                   'samples': len(samples), 'expected_samples': expected, 'interval_s': interval,
                   'stop_marker': stop.get('ts') if isinstance(stop, dict) else None},
        'status_channel': channels,
        'degraded_samples': degraded,
        'missing_status_samples': missing,
        'gaps': gaps,
        'restarts': restarts,
        'unhealthy_samples': unhealthy,
        'resources': {
            'disk_avail_kb': {'first': disk_avail[0], 'last': disk_avail[-1], 'min': min(disk_avail)} if disk_avail else None,
            'disk_used_pct_max': max(disk_pct) if disk_pct else None,
            'pg_conn': {'min': min(pg_conn), 'max': max(pg_conn)} if pg_conn else None,
            'core_mem_mib': {'min': min(memory), 'max': max(memory)} if memory else None,
            'mqtt_reconnect_16m_total': sum(reconnect) if reconnect else None,
        },
        'fivexx_samples': fivexx,
        'certificates': certs,
        'checks': checks,
    }


def summarise_device(rows):
    samples = [r for r in rows if isinstance(r.get('state'), dict)]
    if not samples:
        raise ValueError('设备日志里没有 state 记录')
    flips = [{'ts': r['ts'], 'online': r.get('event')} for r in samples if r.get('event') in ('true', 'false')]
    offline = [r['ts'] for r in samples if not r['state'].get('fresh_online')]
    rssi = [r['state']['last_rssi'] for r in samples if isinstance(r['state'].get('last_rssi'), int)]
    gaps = []
    for before, after in zip(samples, samples[1:]):
        delta = (parse_ts(after['ts']) - parse_ts(before['ts'])).total_seconds()
        if delta > 600:
            gaps.append({'from': before['ts'], 'to': after['ts'], 'seconds': int(delta)})
    span = (parse_ts(samples[-1]['ts']) - parse_ts(samples[0]['ts'])).total_seconds() / 3600
    return {
        'samples': len(samples),
        'span_hours': round(span, 2),
        'query_failures': sum(1 for r in rows if r.get('event') == 'query_failed'),
        'flips': flips,
        'offline_samples': len(offline),
        'offline_hours': round(len(offline) * span / len(samples), 2) if samples else 0,
        'rssi': {'min': min(rssi), 'max': max(rssi)} if rssi else None,
        'gaps': gaps,
        'verdict': 'PASS' if not offline and not gaps else 'REVIEW',
    }


def render_markdown(report, device=None, source=''):
    window = report['window']
    resources = report['resources']
    lines = [f"## 观测结论：{report['verdict']}", '']
    if source:
        lines += [f"数据源：`{source}`", '']
    lines += [
        '| 项 | 值 |',
        '| --- | --- |',
        f"| 窗口 | {window['start']} → {window['end']}（{window['span_hours']} 小时） |",
        f"| 收尾标记 | {window.get('stop_marker') or '缺（窗口未闭环）'} |",
        f"| 样本 | {window['samples']} 条 / 期望 {window['expected_samples']} 条，间隔 {window['interval_s']} 秒 |",
        f"| status 通道 | {report['status_channel']} |",
        f"| 断档 | {len(report['gaps'])} |",
        f"| 重启计数 | core={report['restarts']['core']} pg={report['restarts']['pg']} mqtt={report['restarts']['mqtt']} |",
    ]
    if resources['disk_avail_kb']:
        lines.append(f"| 磁盘可用 | {resources['disk_avail_kb']['first']:,} kB → {resources['disk_avail_kb']['last']:,} kB"
                     f"（最低 {resources['disk_avail_kb']['min']:,} kB，最高使用率 {resources['disk_used_pct_max']}%） |")
    if resources['pg_conn']:
        lines.append(f"| PostgreSQL 连接 | {resources['pg_conn']['min']}–{resources['pg_conn']['max']} |")
    if resources['core_mem_mib']:
        lines.append(f"| CORE 内存 | {resources['core_mem_mib']['min']}–{resources['core_mem_mib']['max']} MiB |")
    if resources['mqtt_reconnect_16m_total'] is not None:
        lines.append(f"| MQTT 重连（16 分钟窗合计） | {resources['mqtt_reconnect_16m_total']} |")
    for key, value in report['certificates'].items():
        label = 'HTTPS 证书' if key.startswith('https') else 'MQTT 证书'
        lines.append(f"| {label} | 最早到期 {value['earliest']} |")
    lines += ['', '### 检查项', '', '| 检查 | 结果 | 说明 |', '| --- | --- | --- |']
    for check in report['checks']:
        mark = '通过' if check['ok'] else ('**不通过**' if check['hard'] else '待复核')
        lines.append(f"| {check['name']} | {mark} | {check['detail']} |")
    if device:
        lines += ['', '### 设备侧同期', '', '| 项 | 值 |', '| --- | --- |',
                  f"| 样本 | {device['samples']} 条 / {device['span_hours']} 小时，查询失败 {device['query_failures']} |",
                  f"| 翻转 | {device['flips'] or '无'} |",
                  f"| 离线样本 | {device['offline_samples']} 条（约 {device['offline_hours']} 小时） |",
                  f"| RSSI | {device['rssi']} |",
                  f"| 断档 | {len(device['gaps'])} |"]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', type=Path, help='服务端观测 ndjson（~/nexus-observe/observe-*.ndjson）')
    parser.add_argument('--device', type=Path, help='可选：设备观测 ndjson（~/nexus-observe/device-*.ndjson）')
    parser.add_argument('--interval', type=int, default=900, help='采样间隔秒数，默认 900')
    parser.add_argument('--format', choices=('md', 'json'), default='md')
    args = parser.parse_args()
    try:
        report = summarise(load(args.log), args.interval)
        device = summarise_device(load(args.device)) if args.device else None
    except (OSError, ValueError) as error:
        print(f'观测日志无法解析: {error}', file=sys.stderr)
        return 1
    if args.format == 'json':
        print(json.dumps({'report': report, 'device': device}, ensure_ascii=False, indent=2))
    else:
        print(render_markdown(report, device, str(args.log)), end='')
    return {'PASS': 0, 'REVIEW': 2}.get(report['verdict'], 1)


if __name__ == '__main__':
    sys.exit(main())
