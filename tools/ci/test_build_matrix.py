#!/usr/bin/env python3
"""Exercise the public build CLI; planning cannot write a config or invoke ESP-IDF."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]

class MatrixPlanTests(unittest.TestCase):
    def test_planning_is_isolated_and_does_not_touch_an_existing_config(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'output'
            result=subprocess.run([sys.executable,str(ROOT/'tools/build_matrix.py'),'--profile','tones',
                '--build-root',str(output)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            plan=json.loads(result.stdout)
            self.assertFalse(plan['executed'])
            self.assertEqual([entry['profile'] for entry in plan['builds']],['tones'])
            command=plan['builds'][0]['command']
            self.assertIn('-DSDKCONFIG='+str((output/'tones/sdkconfig').resolve()),command)
            self.assertNotIn('flash',command)
            self.assertFalse(output.exists())
    def test_all_requires_an_explicit_public_key_and_rejects_unknown_profiles(self):
        for arguments in (['--all'],['--profile','unknown']):
            result=subprocess.run([sys.executable,str(ROOT/'tools/build_matrix.py'),*arguments],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)

if __name__=='__main__':unittest.main()
