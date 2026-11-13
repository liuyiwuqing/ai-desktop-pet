#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class CrashDumpTests(unittest.TestCase):
    def test_flash_read_plan_cannot_reset_erase_or_write(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'core.bin'
            result=subprocess.run([sys.executable,str(ROOT/'tools/crash_dump.py'),'read','--port','/dev/example','--output',str(output)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            plan=json.loads(result.stdout);self.assertFalse(plan['executed'])
            self.assertIn('read-flash',plan['command']);self.assertIn('0xf20000',plan['command']);self.assertIn('0x10000',plan['command'])
            self.assertEqual(plan['command'].count('no-reset'),2)
            self.assertNotIn('write-flash',plan['command']);self.assertFalse(output.exists())
    def test_invalid_local_inputs_never_create_a_decoded_report(self):
        with tempfile.TemporaryDirectory() as directory:
            core=Path(directory)/'core.bin';elf=Path(directory)/'app.elf';report=Path(directory)/'report.txt'
            core.write_bytes(b'\xff'*64);elf.write_bytes(b'not an ELF')
            result=subprocess.run([sys.executable,str(ROOT/'tools/crash_dump.py'),'decode','--core',str(core),'--elf',str(elf),'--output',str(report),'--execute'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertFalse(report.exists())
if __name__=='__main__':unittest.main()
