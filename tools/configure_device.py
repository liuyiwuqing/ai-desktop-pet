#!/usr/bin/env python3
"""Prepare physical PET configuration commands; transmission requires --send and an explicit port.

Input uses stdin or a private JSON file. Credentials never appear in command arguments or output.
The tool does not reset, flash or erase the device. Settings are applied live; credentials need reboot.
"""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
import sys
import time


def prepare(action: str, value: object, revision: int, *, release: str|None=None, calibrated_power: bool=False) -> bytes:
    if action=="ota-apply":
        if not isinstance(release,str) or not re.fullmatch(r'[a-zA-Z0-9_-]{1,64}',release):
            raise ValueError('safe --release identifier required')
        return f'PET_OTA_APPLY {release}\n'.encode()
    if action=="recovery-clear":return b'PET_RECOVERY_CLEAR\n'
    if action=="ota-reboot":return b'PET_OTA_REBOOT\n'
    if action=="servo-estop":return b'PET_SERVO_ESTOP\n'
    if action=="servo-arm":
        if not calibrated_power:raise ValueError('--calibrated-power required after checking profile, limits and independent power')
        return b'PET_SERVO_ARM CALIBRATED_POWER\n'
    if action == "get":
        return b"PET_SETTINGS_GET\n"
    if action=="diagnostics":
        return b"PET_DIAGNOSTICS\n"
    if action == "status":
        return b"PET_PROVISION_STATUS\n"
    if action == "backup":
        return b"PET_GROWTH_EXPORT\n"
    if action not in ('settings','provision','restore') or not isinstance(value, dict) or type(revision) is not int or not 0 <= revision < 0xFFFFFFFF:
        raise ValueError("invalid JSON object or revision")
    if action == "restore":
        backup = value.get("backup", value)
        if not isinstance(backup, dict) or set(backup) != {"format", "deviceId", "revision", "payload"}:
            raise ValueError("invalid backup envelope")
        if backup["format"] != 1 or not isinstance(backup["payload"], str) or not 1 <= revision < 0xFFFFFFFF:
            raise ValueError("unsupported backup or expected revision")
        if len(bytes.fromhex(backup["payload"])) > 256:
            raise ValueError("backup too large")
        envelope = {k: backup[k] for k in ("format", "deviceId", "payload")}
        envelope["expectedRevision"] = revision
        prefix = "PET_GROWTH_RESTORE "
    else:
        key = "credentials" if action == "provision" else "config"
        prefix = "PET_PROVISION " if action == "provision" else "PET_SETTINGS "
        envelope = {"expectedRevision": revision, key: value}
    command = prefix + json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    if len(command.encode()) > 10128:
        raise ValueError("configuration exceeds device limit")
    return command.encode() + b"\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("get", "status", "settings", "provision", "diagnostics", "backup", "restore", "ota-apply", "ota-reboot", "servo-arm", "servo-estop", "recovery-clear"))
    parser.add_argument('--release',help='signed release directory identifier for ota-apply')
    parser.add_argument('--calibrated-power',action='store_true',help='acknowledge calibrated limits and independent servo power')
    parser.add_argument("--file", type=Path, help="JSON input; otherwise stdin (write actions only)")
    parser.add_argument("--revision", type=int, default=0)
    parser.add_argument("--send", action="store_true", help="transmit to the specified physical serial port")
    parser.add_argument("--port")
    parser.add_argument("--output", type=Path, help="save a successful backup; creates a new owner-only file")
    args = parser.parse_args()
    try:
        value = None
        if args.output and args.action != "backup":
            raise ValueError("--output is only valid for backup")
        if args.action in ("settings", "provision", "restore"):
            if args.file and args.action == "provision" and os.stat(args.file).st_mode & 0o077:
                raise ValueError("credential file must have owner-only permissions (chmod 600)")
            raw = args.file.read_text() if args.file else sys.stdin.read(10129)
            value = json.loads(raw)
        command = prepare(args.action, value, args.revision,release=args.release,calibrated_power=args.calibrated_power)
        if not args.send:
            print(json.dumps({"prepared": True, "sent": False, "action": args.action, "bytes": len(command)}))
            return 0
        if not args.port:
            raise ValueError("--send requires --port")
        import serial
        # Opening with DTR/RTS disabled avoids the common automatic boot/reset sequence.
        port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        port.dtr = False
        port.rts = False
        port.port = args.port
        with port:
            port.reset_input_buffer()
            port.write(command)
            port.flush()
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                line = port.readline()
                if line.startswith(b"PET_RESULT "):
                    result = json.loads(line[len(b"PET_RESULT "):])
                    # Only allow status/config values. Never forward arbitrary serial output.
                    allowed = {k: result[k] for k in ("ok", "code", "revision", "writable", "restartRequired", "queued", "config", "diagnostics", "backup") if k in result}
                    if args.output and result.get("ok") and "backup" in allowed:
                        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                        with os.fdopen(fd, "w") as stream:
                            json.dump(allowed["backup"], stream, ensure_ascii=False, indent=2)
                    print(json.dumps(allowed, ensure_ascii=False))
                    return 0 if result.get("ok", result.get("writable", False)) else 1
            raise TimeoutError("device did not return a result; do not assume the write failed or retry blindly")
    except (OSError, ValueError, ImportError, TimeoutError) as error:
        print(f"configuration failed: {type(error).__name__}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
