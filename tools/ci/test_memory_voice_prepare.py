#!/usr/bin/env python3
"""Verify the public preparation/launch CLI using isolated files and no live credentials."""
import json
import os
from pathlib import Path
import subprocess
import select
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/memory_voice.py'


class PreparationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='pet-voice-prepare-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name) / 'bridge'
        command = [sys.executable, str(CLI), 'prepare', '--directory', str(cls.directory)]
        if os.environ.get('PET_MEMORY_VOICE_SOURCE'):
            command += ['--source-file', os.environ['PET_MEMORY_VOICE_SOURCE']]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        if result.returncode or json.loads(result.stdout)['status'] != 'prepared':
            raise AssertionError(result.stderr)

    def invoke(self, mode, *arguments, directory=None, env=None):
        return subprocess.run([sys.executable, str(CLI), mode, '--directory', str(directory or self.directory), *arguments],
            env=env, capture_output=True, text=True, timeout=20)

    def test_preparation_pins_source_and_dependencies_and_verifies_without_network(self):
        verified = self.invoke('verify', env={'PATH': os.defpath, 'HTTPS_PROXY': 'http://127.0.0.1:9'})
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertEqual(json.loads(verified.stdout)['status'], 'verified')

    def test_unknown_directories_and_source_copies_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'user-files'; directory.mkdir()
            keep = directory / 'keep.txt'; keep.write_text('user-owned')
            result = self.invoke('prepare', directory=directory)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(keep.read_text(), 'user-owned')
            source = Path(temporary) / 'unverified.py'; source.write_text('raise SystemExit(99)')
            rejected = self.invoke('prepare', '--source-file', str(source), directory=Path(temporary) / 'new')
            self.assertEqual(rejected.returncode, 2)
            self.assertFalse((Path(temporary) / 'new/mcp_pipe.py').exists())

    def test_a_changed_bridge_is_rejected_and_not_silently_overwritten(self):
        source = self.directory / 'mcp_pipe.py'; original = source.read_bytes()
        with tempfile.TemporaryDirectory() as temporary:
            offline = Path(temporary) / 'verified.py'; offline.write_bytes((self.directory / 'mcp_pipe.upstream.py').read_bytes())
            try:
                source.write_bytes(b'# user change\n')
                self.assertEqual(self.invoke('verify').returncode, 2)
                self.assertEqual(self.invoke('prepare', '--source-file', str(offline)).returncode, 2)
                self.assertEqual(source.read_bytes(), b'# user change\n')
            finally:
                source.write_bytes(original)

    def test_plaintext_embedded_credentials_and_malformed_voice_urls_do_not_start(self):
        for endpoint in ('ws://127.0.0.1:9/mcp', 'wss://user:secret@127.0.0.1:9/mcp',
            'wss://127.0.0.1:9/mcp#secret', 'wss://127.0.0.1:invalid/mcp'):
            with self.subTest(endpoint=endpoint):
                env = {'PATH': os.defpath, 'MCP_ENDPOINT': endpoint,
                    'NEXUS_MEMORY_MCP_TOKEN': 'isolated-voice-memory-test-token-long-enough'}
                result = self.invoke('run', '--stdio', str(ROOT.parent / 'nexus-core/tools/memory_mcp_stdio.py'), env=env)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(result.stdout)
                self.assertIn('invalid_voice_endpoint', result.stderr)
                self.assertNotIn(endpoint, result.stderr)

    def test_invalid_core_endpoint_is_rejected_before_the_bridge_is_started(self):
        directory = self.directory
        stdio = ROOT.parent / 'nexus-core/tools/memory_mcp_stdio.py'
        if not stdio.is_file():
            self.skipTest('paired nexus-core checkout required for stdio preflight')
        env = {'PATH': os.environ.get('PATH', os.defpath),
            'MCP_ENDPOINT': 'wss://127.0.0.1:9/mcp?token=isolated-endpoint-secret',
            'NEXUS_MEMORY_MCP_URL': 'http://remote.example.invalid/mcp/memory',
            'NEXUS_MEMORY_MCP_TOKEN': 'isolated-voice-memory-test-token-long-enough'}
        process = subprocess.Popen([sys.executable, str(CLI), 'run', '--directory', str(directory),
            '--stdio', str(stdio)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            try:
                output, errors = process.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                process.terminate()
                output, errors = process.communicate(timeout=15)
                self.fail('invalid CORE configuration left the always-on bridge running')
            self.assertEqual(process.returncode, 2)
            self.assertFalse(output)
            self.assertIn('stdio_preflight_failed', errors)
            self.assertNotIn('isolated-endpoint-secret', errors)
        finally:
            if process.poll() is None:
                process.terminate(); process.wait(timeout=15)
    def test_reloadable_client_file_reaches_the_stdio_preflight_without_an_env_secret(self):
        stdio=ROOT.parent/'nexus-core/tools/memory_mcp_stdio.py'
        if not stdio.is_file():self.skipTest('paired nexus-core checkout required for stdio preflight')
        with tempfile.TemporaryDirectory(prefix='pet-voice-client-') as temporary:
            file=Path(temporary)/'memory.json';secret='isolated-reloadable-memory-credential-long-enough'
            file.write_text(json.dumps({'format':1,'revision':1,'id':'memory-fixture','token':secret}));file.chmod(0o600)
            env={'PATH':os.environ.get('PATH',os.defpath),'MCP_ENDPOINT':'wss://127.0.0.1:9/mcp?token=isolated-provider-secret',
                'NEXUS_MEMORY_MCP_URL':'http://127.0.0.1:9/mcp/memory','NEXUS_MEMORY_MCP_TOKEN_FILE':str(file)}
            process=subprocess.Popen([sys.executable,str(CLI),'run','--directory',str(self.directory),'--stdio',str(stdio)],
                env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                self.assertTrue(select.select([process.stdout],[],[],5)[0],'bridge did not finish preflight')
                line=process.stdout.readline();self.assertTrue(line,'bridge rejected the protected credential file')
                self.assertEqual(json.loads(line)['status'],'running')
                process.terminate();output,errors=process.communicate(timeout=15)
                self.assertEqual(process.returncode,0,errors);self.assertNotIn(secret,line+output+errors);self.assertNotIn('isolated-provider-secret',errors)
            finally:
                if process.poll() is None:process.terminate();process.communicate(timeout=15)


if __name__ == '__main__':
    unittest.main()
