#!/usr/bin/env python3
"""Check generated audio wiring and reject GPIO conflicts through the generator."""
import copy
import contextlib
import importlib.util
import io
import json
import tempfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("board_codegen", ROOT / "tools/board_codegen.py")
codegen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codegen)
board = json.loads(codegen.SOURCE.read_text())
board["audio"] = {
    "mic": {"i2s": 0, "bclk": 16, "ws": 17, "din": 18},
    "speaker": {"i2s": 1, "bclk": 21, "ws": 38, "dout": 39},
}


def generate(value):
    with tempfile.TemporaryDirectory(prefix="pet-board-") as folder:
        root = Path(folder)
        source, header, kconfig = root / "board.json", root / "board.h", root / "Kconfig"
        source.write_text(json.dumps(value))
        with patch.multiple(codegen, SOURCE=source, HEADER=header, KCONFIG=kconfig), contextlib.redirect_stdout(io.StringIO()):
            codegen.generate()
        return header.read_text()


for device, field, pin in [("mic", "din", 8), ("speaker", "bclk", 15), ("speaker", "dout", 18)]:
    invalid = copy.deepcopy(board)
    invalid["audio"][device][field] = pin
    try:
        generate(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError(f"accepted overlapping {device}.{field}={pin}")

header = generate(board)
assert "#define PET_BOARD_MIC_DIN 18\n" in header
assert "#define PET_BOARD_SPEAKER_BCLK 21\n" in header
for field, value in [("i2s", 0), ("dout", 49), ("dout", True)]:
    invalid = copy.deepcopy(board)
    invalid["audio"]["speaker"][field] = value
    try:
        generate(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError(f"accepted invalid speaker.{field}={value}")
print("board codegen: audio wiring and cross-peripheral conflicts passed")
