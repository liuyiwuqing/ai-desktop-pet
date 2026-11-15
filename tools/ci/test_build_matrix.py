#!/usr/bin/env python3
"""Exercise the public build CLI; planning cannot write a config or invoke ESP-IDF."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]

class MatrixPlanTests(unittest.TestCase):
    def test_source_evidence_works_with_an_explicitly_owned_container_checkout(self):
        # Exercise the exact provenance boundary with real Git, not a simulated successful build.
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve();(root/'firmware').mkdir()
            (root/'firmware/ownership.c').write_text('int container_checkout;\n')
            for args in (['init','-q'],['add','.'],['-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-qm','fixture']):
                subprocess.run(['git','-C',str(root),*args],check=True,capture_output=True)
            env=os.environ.copy();env.update(GIT_TEST_ASSUME_DIFFERENT_OWNER='1',GIT_CONFIG_COUNT='1',
                GIT_CONFIG_KEY_0='safe.directory',GIT_CONFIG_VALUE_0='')
            control=subprocess.run(['git','-C',str(root),'ls-files'],env=env,capture_output=True,text=True)
            self.assertEqual(control.returncode,128,control.stderr)
            self.assertIn('dubious ownership',control.stderr)
            script='import sys,json,importlib.util;from pathlib import Path;'+\
                'spec=importlib.util.spec_from_file_location("matrix",sys.argv[1]);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);'+\
                'm.ROOT=Path(sys.argv[2]);print(json.dumps({"files":m.source_manifest(),"state":m.source_state()}))'
            checked=subprocess.run([sys.executable,'-c',script,str(ROOT/'tools/build_matrix.py'),str(root)],env=env,capture_output=True,text=True)
            self.assertEqual(checked.returncode,0,checked.stderr)
            evidence=json.loads(checked.stdout)
            self.assertEqual([p['path'] for p in evidence['files']],['firmware/ownership.c'])
            self.assertEqual(evidence['state']['tracked'],[])
            self.assertEqual(evidence['state']['untracked'],[])

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

    def test_duplex_plan_is_an_explicit_isolated_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'output'
            result=subprocess.run([sys.executable,str(ROOT/'tools/build_matrix.py'),'--profile','duplex',
                '--build-root',str(output)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            plan=json.loads(result.stdout)
            self.assertFalse(plan['executed'])
            self.assertEqual([entry['profile'] for entry in plan['builds']],['duplex'])
            command=plan['builds'][0]['command']
            self.assertIn('-DSDKCONFIG='+str((output/'duplex/sdkconfig').resolve()),command)
            self.assertIn(str(ROOT/'firmware/profiles/duplex.defaults'),next(
                value for value in command if value.startswith('-DSDKCONFIG_DEFAULTS=')))
            self.assertNotIn('flash',command)
            self.assertFalse(output.exists())

    def test_voice_plan_needs_no_ota_key_and_never_flashes(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'output'
            result=subprocess.run([sys.executable,str(ROOT/'tools/build_matrix.py'),'--profile','voice',
                '--build-root',str(output)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            plan=json.loads(result.stdout)
            self.assertFalse(plan['executed'])
            command=plan['builds'][0]['command']
            self.assertIn('-DSDKCONFIG='+str((output/'voice/sdkconfig').resolve()),command)
            defaults=next(v for v in command if v.startswith('-DSDKCONFIG_DEFAULTS='))
            self.assertIn(str(ROOT/'firmware/profiles/voice.defaults'),defaults)
            self.assertNotIn('full.defaults',defaults)
            self.assertNotIn('ota.defaults',defaults)
            self.assertNotIn('flash',command)
            self.assertFalse(output.exists())

if __name__=='__main__':unittest.main()
