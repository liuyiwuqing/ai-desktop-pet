#!/usr/bin/env python3
"""Generate Noto SC 16/24 px LVGL fonts; requires Node/npm only at regeneration."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "resources/fonts"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="NotoSansSC-Regular.ttf")
    args = parser.parse_args()
    # GB2312 一级/二级汉字、ASCII、常用标点。网络正文缺字由 UI 显式替换为方框。
    chars = {chr(c) for c in range(32, 127)} | set("□…—℃°·“”‘’，。！？：；（）【】《》、\u3000")
    for first in range(0xB0, 0xF8):
        for second in range(0xA1, 0xFF):
            try:
                chars.add(bytes((first, second)).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    chars = "".join(sorted(chars))
    for size in (16, 24):
        result = subprocess.run([
            "npx", "--yes", "--package=lv_font_conv@1.5.3", "lv_font_conv",
            "--font", str(args.font.resolve()), "--size", str(size), "--bpp", "2",
            "--format", "bin", "--no-compress", "--no-kerning",
            "--symbols", chars, "--output", str(OUT / f"pet_noto_sc_{size}.bin"),
        ])
        if result.returncode:
            raise SystemExit(f"Font converter failed for {size}px: {result.returncode}")
    manifest = {
        "font": "Noto Sans SC Regular", "license": "OFL-1.1",
        "source": "https://github.com/lvgl/lvgl/blob/v9.5.0/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf",
        "sourceSha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
        "converter": "lv_font_conv@1.5.3", "glyphs": len(chars), "bpp": 2,
        "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.glob("pet_noto_sc_*.bin"))},
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    print(f"Generated {len(chars)} glyphs at 16/24 px")


if __name__ == "__main__":
    main()
