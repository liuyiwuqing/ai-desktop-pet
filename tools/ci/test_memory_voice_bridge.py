#!/usr/bin/env python3
"""Real pinned bridge + stdio + TLS WebSocket; CORE is an isolated HTTP boundary fixture."""
import asyncio
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'integration/tests'))
from voice_memory_probe import ENDPOINT_SECRET, VoiceFixture, discover, query, request

TOKEN = 'isolated-voice-memory-test-token-long-enough'
DIRECTORY = Path(os.environ.get('PET_MEMORY_VOICE_DIRECTORY', str(ROOT / 'build/memory-voice')))


class Endpoint(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        message = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.server.events.append(message)
        if self.path != '/mcp/memory' or self.headers.get('Authorization') != 'Bearer ' + self.server.token:
            self.send_response(401); self.end_headers(); return
        if message['method'] == 'tools/call' and self.server.unavailable:
            self.send_response(503); self.end_headers(); self.wfile.write(TOKEN.encode()); return
        if 'id' not in message:
            self.send_response(202); self.end_headers(); return
        payload = {}
        if message['method'] == 'initialize':
            payload = {'protocolVersion': '2025-06-18', 'capabilities': {'tools': {}}, 'serverInfo': {'name': 'fixture', 'version': '1'}}
        elif message['method'] == 'tools/list':
            payload = {'tools': [{'name': 'memory.search', 'inputSchema': {'type': 'object'}}]}
        elif message['method'] == 'tools/call':
            payload = {'content': [{'type': 'text', 'text': 'fixture-result'}],
                'structuredContent': {'items': [{'content': 'public-original-body'}], 'partial': False}}
        data = json.dumps({'jsonrpc': '2.0', 'id': message['id'], 'result': payload}).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)

    def log_message(self, *_):
        pass


def group_members(group):
    result = subprocess.check_output(['ps', '-eo', 'pid=,pgid=,stat='], text=True)
    return [int(pid) for line in result.splitlines() for pid, pgid, state in [line.split()]
        if int(pgid) == group and not state.startswith('Z')]


class BridgeTest(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Endpoint)
        self.server.events = []
        self.server.unavailable = False
        self.server.token = TOKEN
        self.thread = threading.Thread(target=lambda: self.server.serve_forever(poll_interval=.05), daemon=True)
        self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}/mcp/memory'
        self.expected_tls_reset = False

    async def asyncSetUp(self):
        self.loop = asyncio.get_running_loop()
        self.previous_handler = self.loop.get_exception_handler()

        def report(loop, context):
            if self.expected_tls_reset and isinstance(context.get('exception'), ConnectionResetError):
                return  # Expected peer refusal of the deliberately untrusted fixture certificate.
            if self.previous_handler:
                self.previous_handler(loop, context)
            else:
                loop.default_exception_handler(context)
        self.loop.set_exception_handler(report)

    async def asyncTearDown(self):
        self.loop.set_exception_handler(self.previous_handler)

    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()

    async def assert_stopped_and_private(self, fixture):
        self.assertEqual(fixture.process.returncode, 0, fixture.errors.decode())
        for _ in range(20):
            if not await asyncio.to_thread(group_members, fixture.group):
                break
            await asyncio.sleep(.1)
        self.assertEqual(await asyncio.to_thread(group_members, fixture.group), [])
        for secret in (TOKEN, ENDPOINT_SECRET):
            self.assertNotIn(secret.encode(), fixture.output + fixture.errors)

    async def test_real_bridge_discovers_queries_reinitializes_after_reconnect_and_stops_its_child(self):
        async with VoiceFixture(DIRECTORY, self.url, TOKEN) as fixture:
            websocket = await fixture.connection()
            await discover(websocket)
            response = await query(websocket)
            self.assertEqual(response['result']['structuredContent']['items'], [{'content': 'public-original-body'}])
            self.assertGreaterEqual(len(await asyncio.to_thread(group_members, fixture.group)), 2)
            await websocket.close(1011, ENDPOINT_SECRET + ':' + TOKEN)
            websocket = await fixture.connection()
            # Each reconnect starts a fresh stdio child; old initialization is invalid.
            early = await request(websocket, 'tools/list', 'before-reinitialize')
            self.assertEqual(early['error']['code'], -32002)
            await discover(websocket)
            await query(websocket, 'new-query')
            self.assertEqual([event['id'] for event in self.server.events if event['method'] == 'tools/call'], ['query', 'new-query'])
        await self.assert_stopped_and_private(fixture)

    async def test_http_failure_is_an_error_without_replay_and_connection_remains_usable(self):
        async with VoiceFixture(DIRECTORY, self.url, TOKEN) as fixture:
            websocket = await fixture.connection()
            await discover(websocket)
            self.server.unavailable = True
            response = await query(websocket)
            self.assertEqual(response['error'], {'code': -32000, 'message': 'memory_endpoint_unavailable'})
            self.assertEqual(sum(event['method'] == 'tools/call' for event in self.server.events), 1)
            self.assertEqual((await request(websocket, 'ping', 'alive'))['result'], {})
        await self.assert_stopped_and_private(fixture)
    async def test_client_file_rotation_keeps_the_same_tls_bridge_and_stdio_session(self):
        next_token='next-isolated-bridge-memory-credential-long-enough'
        with tempfile.TemporaryDirectory(prefix='pet-bridge-rotation-') as temporary:
            file=Path(temporary)/'client.json'
            def replace(revision,token):
                staged=file.with_suffix('.new');staged.write_text(json.dumps({'format':1,'revision':revision,'id':'memory-client','token':token}));staged.chmod(0o600);staged.replace(file)
            replace(1,TOKEN)
            async with VoiceFixture(DIRECTORY,self.url,'',extra_env={'NEXUS_MEMORY_MCP_TOKEN_FILE':str(file)}) as fixture:
                websocket=await fixture.connection();await discover(websocket);await query(websocket,'before-rotation')
                self.server.token=next_token;replace(2,next_token)
                result=await query(websocket,'after-rotation')
                self.assertEqual(result['result']['structuredContent']['items'],[{'content':'public-original-body'}])
                self.assertFalse(websocket.state.name=='CLOSED');self.assertIsNone(fixture.process.returncode)
                self.assertEqual([event['id'] for event in self.server.events if event['method']=='tools/call'],['before-rotation','after-rotation'])
            await self.assert_stopped_and_private(fixture)
            self.assertNotIn(next_token.encode(),fixture.output+fixture.errors)

    async def test_an_untrusted_tls_certificate_does_not_reach_stdio_or_core(self):
        self.expected_tls_reset = True
        async with VoiceFixture(DIRECTORY, self.url, TOKEN, trust_certificate=False) as fixture:
            with self.assertRaises(asyncio.TimeoutError):
                await fixture.connection(timeout=3)
            self.assertEqual(self.server.events, [])
        await self.assert_stopped_and_private(fixture)

    async def test_fatal_stdio_input_closes_the_socket_and_recovers_with_fresh_initialization(self):
        async with VoiceFixture(DIRECTORY, self.url, TOKEN) as fixture:
            websocket = await fixture.connection()
            await discover(websocket)
            # The real adapter deliberately exits on an oversized stdio line.
            await websocket.send('x' * 32769)
            websocket = await fixture.connection(timeout=8)
            await discover(websocket)
            result = await query(websocket, 'after-stdio-exit')
            self.assertEqual(result['result']['structuredContent']['items'], [{'content': 'public-original-body'}])
        await self.assert_stopped_and_private(fixture)

    async def test_ambient_config_cannot_replace_the_child_or_pass_administrator_credentials(self):
        with tempfile.TemporaryDirectory() as temporary:
            guarded = Path(temporary) / 'guarded_stdio.py'
            adapter = ROOT.parent / 'nexus-core/tools/memory_mcp_stdio.py'
            # Observe the process environment, then exec the actual adapter. No
            # RPC/service implementation is replaced by the guard.
            guarded.write_text('import os,sys\nfrom pathlib import Path\n'
                'assert not any(k in os.environ for k in ("NEXUS_ADMIN_TOKEN","NEXUS_AI_API_KEY","HTTP_PROXY","HTTPS_PROXY","PYTHONPATH"))\n'
                'assert os.environ["PYTHON_DOTENV_DISABLED"] == "1"\n'
                'assert Path(os.environ["MCP_CONFIG"]).read_bytes() == b"{}\\n"\n'
                f'os.execv(sys.executable,[sys.executable,"-I",{str(adapter)!r}])\n')
            ambient = Path(temporary) / 'ambient.json'
            ambient.write_text(json.dumps({'mcpServers': {str(guarded):
                {'command': sys.executable, 'args': ['-c', 'raise SystemExit(99)']}}}))
            env = {'MCP_CONFIG': str(ambient), 'NEXUS_ADMIN_TOKEN': 'isolated-administrator-secret',
                'NEXUS_AI_API_KEY': 'isolated-model-secret', 'HTTPS_PROXY': 'http://127.0.0.1:9',
                'PYTHONPATH': temporary, 'PYTHON_DOTENV_DISABLED': '0'}
            async with VoiceFixture(DIRECTORY, self.url, TOKEN, extra_env=env, stdio_script=guarded) as fixture:
                websocket = await fixture.connection()
                await discover(websocket)
                response = await query(websocket)
                self.assertEqual(response['result']['structuredContent']['items'], [{'content': 'public-original-body'}])
            await self.assert_stopped_and_private(fixture)


if __name__ == '__main__':
    unittest.main()
