#!/usr/bin/env python3
"""Exercise the stdio process against an isolated HTTP endpoint; no voice cloud or secrets."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import threading
import unittest

SCRIPT=Path(__file__).resolve().parents[3]/'nexus-core/tools/memory_mcp_stdio.py'
TOKEN='isolated-voice-memory-test-token-long-enough'
EVENTS=[]
MODE='ok'

class Endpoint(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        EVENTS.append((self.path,dict(self.headers),body))
        if MODE=='http-invalid-status':self.wfile.write(b'NOT HTTP\r\n\r\n');return
        if MODE=='redirect':
            self.send_response(302);self.send_header('Location',f'http://127.0.0.1:{self.server.server_port}/wrong-destination');self.end_headers();return
        if MODE=='unavailable':
            self.send_response(401);self.end_headers();self.wfile.write(('untrusted-error-'+TOKEN).encode());return
        if 'id' not in body:
            self.send_response(202);self.end_headers();return
        result={'protocolVersion':'2025-06-18','capabilities':{'tools':{}},'serverInfo':{'name':'fixture','version':'1'}} if body['method']=='initialize' else {}
        data=json.dumps({'jsonrpc':'2.0','id':body['id'],'result':result}).encode()
        if MODE=='malformed-result':data=json.dumps({'jsonrpc':'2.0','id':body['id'],'result':[]}).encode()
        if MODE=='oversize':data=b' '*65537
        self.send_response(200);self.send_header('Content-Type','application/json')
        if MODE=='http-truncated':self.send_header('Content-Length',str(len(data)+10))
        self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass

class StdioTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Endpoint)
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.server.server_close();cls.thread.join()
    def run_stdio(self,text,**overrides):
        env=os.environ.copy();env.update(NEXUS_MEMORY_MCP_TOKEN=TOKEN,NEXUS_MEMORY_MCP_URL=f'http://127.0.0.1:{self.server.server_port}/mcp/memory');env.update(overrides)
        return subprocess.run(['python3',str(SCRIPT)],input=text,capture_output=True,text=True,env=env,timeout=10)
    def setUp(self):
        global MODE
        EVENTS.clear();MODE='ok'
    def test_initialize_notification_and_ping_keep_stdio_clean(self):
        messages=[{'jsonrpc':'2.0','id':'voice-init','method':'initialize','params':{'protocolVersion':'2025-06-18','clientInfo':{'name':'test','version':'1'},'capabilities':{}}},
            {'jsonrpc':'2.0','method':'notifications/initialized'}, {'jsonrpc':'2.0','id':0,'method':'ping'}]
        result=self.run_stdio(''.join(json.dumps(m)+'\n' for m in messages))
        self.assertEqual(result.returncode,0,result.stderr)
        output=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([m['id'] for m in output],['voice-init',0]);self.assertEqual(len(EVENTS),3)
        self.assertTrue(all(event[0]=='/mcp/memory' and event[1]['Authorization']=='Bearer '+TOKEN for event in EVENTS))
        self.assertEqual(EVENTS[-1][1]['Mcp-Protocol-Version'],'2025-06-18')
        self.assertNotIn(TOKEN,result.stdout+result.stderr)
    def initialize(self):return json.dumps({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-06-18','clientInfo':{'name':'test','version':'1'},'capabilities':{}}})+'\n'
    def test_redirects_upstream_failures_and_invalid_responses_are_not_exposed_or_retried(self):
        global MODE
        for mode in ('redirect','unavailable','oversize','malformed-result','http-truncated','http-invalid-status'):
            with self.subTest(mode=mode):
                MODE=mode;EVENTS.clear();result=self.run_stdio(self.initialize())
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(json.loads(result.stdout)['error']['message'],'memory_endpoint_unavailable')
                self.assertEqual(len(EVENTS),1)
                self.assertNotIn(TOKEN,result.stdout+result.stderr)
                self.assertNotIn('Traceback',result.stderr)
    def test_private_or_malformed_endpoint_configurations_fail_before_network(self):
        for overrides in ({'NEXUS_MEMORY_MCP_URL':'http://remote.example.invalid/mcp/memory'},
            {'NEXUS_MEMORY_MCP_URL':'https://user:secret@example.invalid/mcp/memory'},
            {'NEXUS_MEMORY_MCP_URL':'https://example.invalid/api/v1/memory/search'},
            {'NEXUS_MEMORY_MCP_TOKEN':'short'}):
            with self.subTest(overrides=list(overrides)):
                result=self.run_stdio(self.initialize(),**overrides)
                self.assertEqual(result.returncode,2);self.assertFalse(result.stdout);self.assertFalse(EVENTS)
                self.assertNotIn(TOKEN,result.stderr)
    def test_bad_rpc_and_requests_before_initialization_never_reach_core(self):
        result=self.run_stdio('{\n'+json.dumps({'jsonrpc':'2.0','id':'early','method':'tools/list'})+'\n')
        self.assertEqual(result.returncode,0,result.stderr);output=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([m['error']['code'] for m in output],[-32700,-32002]);self.assertFalse(EVENTS)
    def test_oversized_line_is_not_forwarded(self):
        result=self.run_stdio('x'*32769+'\n');self.assertEqual(result.returncode,2);self.assertFalse(EVENTS);self.assertFalse(result.stdout)
    def test_invalid_unicode_identifier_returns_json_without_a_traceback(self):
        result=self.run_stdio(self.initialize()+'{"jsonrpc":"2.0","id":"\\ud800","method":"ping"}\n')
        self.assertEqual(result.returncode,0,result.stderr);self.assertNotIn('Traceback',result.stderr)
        responses=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(responses[-1]['error']['code'],-32600);self.assertEqual(len(EVENTS),1)

if __name__=='__main__':unittest.main()
