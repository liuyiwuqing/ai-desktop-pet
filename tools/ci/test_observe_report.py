#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/observe_report.py'
READY = {'schemaVersion': 1, 'mqtt': 'ready', 'mqttError': '', 'database': 'ready'}


def sample(ts, status=None, core='running/healthy/0', mqtt='running/0', fivexx=0, disk=16_000_000):
    return {'ts': ts, 'core': core, 'pg': 'running/healthy/0', 'mqtt': mqtt,
            'status': READY if status is None else status,
            'disk_avail_kb': disk, 'disk_used_pct': 81, 'pg_conn': 12, 'core_mem': '250MiB / 1GiB',
            'mqtt_reconnect_16m': 0, 'http5xx_last200': fivexx,
            'https_cert_not_after': 'Feb 13 14:59:31 2027 GMT', 'mqtt_cert_not_after': 'Feb 17 17:11:14 2029 GMT'}


def write(path, rows):
    path.write_text(''.join(json.dumps(r, ensure_ascii=False) + '\n' for r in rows), encoding='utf-8')


def run(path, *extra):
    return subprocess.run([sys.executable, str(SCRIPT), str(path), *extra], capture_output=True, text=True)


class ObserveReportTests(unittest.TestCase):
    def test_complete_window_passes_without_restarts(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'observe.ndjson'
            write(log, [{'ts': '2026-11-24T00:00:00Z', 'event': 'start', 'duration_s': 1800, 'interval_s': 900},
                        sample('2026-11-24T00:00:00Z'),
                        sample('2026-11-24T00:15:00Z'),
                        sample('2026-11-24T00:30:00Z'),
                        {'ts': '2026-11-24T00:30:05Z', 'event': 'stop'}])
            result = run(log, '--format', 'json')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)['report']
            self.assertEqual(report['verdict'], 'PASS')
            self.assertEqual(report['restarts'], {'core': 0, 'pg': 0, 'mqtt': 0})
            # Mosquitto 没有 healthcheck，`running/0` 只表示 0 次重启，不是异常。
            self.assertEqual(report['unhealthy_samples'], [])
            self.assertEqual(report['window']['samples'], 3)

    def test_unauthorized_channel_is_a_collector_fault_not_an_outage(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'observe.ndjson'
            write(log, [{'ts': '2026-11-24T00:00:00Z', 'event': 'start', 'duration_s': 1800, 'interval_s': 900},
                        sample('2026-11-24T00:00:00Z'),
                        sample('2026-11-24T00:15:00Z', status={'error': 'unauthorized'}),
                        sample('2026-11-24T00:30:00Z', status={'error': 'unauthorized'}),
                        {'ts': '2026-11-24T00:30:05Z', 'event': 'stop'}])
            result = run(log)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('REVIEW', result.stdout)
            self.assertIn('采集通道失效 2 条', result.stdout)
            # 服务本身没被判成不可用：容器与重启检查仍然通过。
            self.assertIn('| 容器状态正常 | 通过 |', result.stdout)

    def test_gap_restart_and_missing_stop_fail_the_window(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'observe.ndjson'
            write(log, [{'ts': '2026-11-24T00:00:00Z', 'event': 'start', 'duration_s': 1800, 'interval_s': 900},
                        sample('2026-11-24T00:00:00Z'),
                        sample('2026-11-24T01:30:00Z', core='running/healthy/2', mqtt='running/1', fivexx=7)])
            result = run(log, '--format', 'json')
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads(result.stdout)['report']
            self.assertEqual(report['verdict'], 'FAIL')
            failed = {c['name'] for c in report['checks'] if not c['ok'] and c['hard']}
            self.assertEqual(failed, {'窗口闭环', '无采样断档', '容器未重启'})
            # 5xx 是滚动计数、不是增量，只降级为待复核，不单独判失败。
            soft = {c['name'] for c in report['checks'] if not c['ok'] and not c['hard']}
            self.assertEqual(soft, {'5xx 恒定'})
            self.assertEqual(len(report['gaps']), 1)
            self.assertEqual(report['restarts'], {'core': 2, 'pg': 0, 'mqtt': 1})

    def test_unreadable_log_is_rejected_without_a_report(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'observe.ndjson'
            log.write_text('{"ts": "2026-11-24T00:00:00Z"}\n不是 JSON\n', encoding='utf-8')
            result = run(log)
            self.assertEqual(result.returncode, 1)
            self.assertIn('不是合法 JSON', result.stderr)
            self.assertEqual(result.stdout, '')
            empty = Path(directory) / 'empty.ndjson'
            empty.write_text('', encoding='utf-8')
            self.assertEqual(run(empty).returncode, 1)

    def test_device_log_reports_offline_stretch(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'observe.ndjson'
            device = Path(directory) / 'device.ndjson'
            write(log, [{'ts': '2026-11-24T00:00:00Z', 'event': 'start', 'duration_s': 1800, 'interval_s': 900},
                        sample('2026-11-24T00:00:00Z'), sample('2026-11-24T00:30:00Z'),
                        {'ts': '2026-11-24T00:30:05Z', 'event': 'stop'}])
            write(device, [{'ts': '2026-11-24T00:00:00Z', 'event': 'start', 'device': 'dodo-1'},
                           {'ts': '2026-11-24T00:05:00Z', 'event': None,
                            'state': {'fresh_online': False, 'last_rssi': -41}},
                           {'ts': '2026-11-24T00:10:00Z', 'event': 'true',
                            'state': {'fresh_online': True, 'last_rssi': -43}},
                           {'ts': '2026-11-24T00:15:00Z', 'event': 'query_failed', 'device': 'dodo-1'}])
            result = run(log, '--device', str(device))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('### 设备侧同期', result.stdout)
            self.assertIn('查询失败 1', result.stdout)
            self.assertIn("'online': 'true'", result.stdout)
            self.assertIn('离线样本 | 1 条', result.stdout)


if __name__ == '__main__':
    unittest.main()
