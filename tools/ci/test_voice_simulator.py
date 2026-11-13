#!/usr/bin/env python3
"""Exercise the desktop voice CLI; all speech and response inputs are synthetic."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/simulate_voice.sh"
scenario = """wake
voice 300
silence 280
status
silence 20
tts
press
tts 1
voice 300
release
mute
tts 2
unmute
wake
wait 2100
wake
silence 15000
quit
"""
result = subprocess.run(["bash", str(SCRIPT)], input=scenario, text=True,
                        capture_output=True, cwd=ROOT, check=True)
rows = [dict(token.split("=", 1) for token in line.split())
        for line in result.stdout.splitlines() if line.startswith("t=")]


def observed(**expected):
    return any(all(row.get(key) == str(value) for key, value in expected.items()) for row in rows)


assert observed(t=580, turn=1, state="listening", silence=280)
assert observed(t=600, turn=1, state="thinking", reason="silence")
assert observed(t=600, turn=1, state="speaking", reason="response")
assert observed(t=600, turn=2, state="listening", cancelled=1)
assert observed(t=600, turn=2, state="listening", result="stale")
assert observed(t=900, turn=2, state="muted", cancelled=2)
assert observed(t=900, turn=2, state="muted", result="ignored")
assert observed(t=900, state="idle", reason="unmuted")
assert observed(t=3000, turn=3, state="listening")
assert observed(t=18000, turn=3, state="idle", reason="no_speech", cancelled=3)
invalid = subprocess.run(["bash", str(SCRIPT)], input="voice 21\nwait -1\nunknown\nquit\n",
                         text=True, capture_output=True, cwd=ROOT)
assert invalid.returncode == 2 and invalid.stderr
print("voice simulator: desktop cycle, stale response, mute, cooldown and invalid commands passed")
