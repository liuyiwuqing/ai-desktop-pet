#!/usr/bin/env python3
"""模型打包契约的 CLI 测试；合成文件仅用于损坏/边界输入。"""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def image(wake_model='wn9_xiaoaitongxue'):
    result = struct.pack('<I', 2)
    for index, name in enumerate((wake_model, 'vadnet1_medium')):
        result += struct.pack('<32sI32sII', name.encode(), 1, b'coeff', 156 + index * 4, 4)
    return result + b'12345678'


class ModelPackageTest(unittest.TestCase):
    def test_exact_pair_produces_firmware_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            binary, header = Path(directory) / 'srmodels.bin', Path(directory) / 'model.h'
            binary.write_bytes(image())
            result = subprocess.run([sys.executable, str(ROOT / 'tools/check_sr_models.py'),
                                     '--image', str(binary), '--header', str(header),
                                     '--partition-table', str(ROOT / 'firmware/partitions/16m.csv')],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('164u', header.read_text())
            self.assertIn(hashlib.sha256(image()).hexdigest(), result.stdout)

    def test_bad_packages_do_not_replace_firmware_digest(self):
        wrong_name = image().replace(b'vadnet1_medium', b'vadnet1_otherx')
        wrong_range = bytearray(image())
        struct.pack_into('<I', wrong_range, 72, 0x100000)
        overlap = bytearray(image())
        struct.pack_into('<I', overlap, 148, 156)  # 第二个文件与第一个文件使用相同范围。
        wrong_counts = [struct.pack('<I', count) + image()[4:] for count in (1, 3)]
        for data in (b'', image()[:40], image() + b'extra', image() * 6000,
                     wrong_name, wrong_range, overlap, *wrong_counts, image('wn9_nihaoxiaozhi_tts')):
            with self.subTest(size=len(data)), tempfile.TemporaryDirectory() as directory:
                binary, header = Path(directory) / 'srmodels.bin', Path(directory) / 'model.h'
                binary.write_bytes(data)
                header.write_text('previous verified digest')
                result = subprocess.run([sys.executable, str(ROOT / 'tools/check_sr_models.py'),
                                         '--image', str(binary), '--header', str(header),
                                         '--partition-table', str(ROOT / 'firmware/partitions/16m.csv')],
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('ESP-SR package rejected', result.stderr)
                self.assertEqual(header.read_text(), 'previous verified digest')


if __name__ == '__main__':
    unittest.main()
