#!/usr/bin/env python3
"""A temporary TLS voice endpoint for protocol tests, never a live provider client."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import urllib.parse

import websockets

ROOT = Path(__file__).resolve().parents[2]
VERSION = '2025-06-18'
ENDPOINT_SECRET = 'isolated-provider-endpoint-token'


class VoiceFixture:
    def __init__(self, directory, core_url, token, *, trust_certificate=True, extra_env=None, stdio_script=None):
        parts = urllib.parse.urlsplit(core_url)
        if (parts.scheme != 'http' or parts.hostname != '127.0.0.1' or not parts.port
            or parts.path != '/mcp/memory' or parts.username or parts.password or parts.query or parts.fragment):
            raise ValueError('probe requires an isolated loopback CORE endpoint')
        self.directory = Path(directory).resolve()
        self.core_url, self.token = core_url, token
        self.trust_certificate = trust_certificate
        self.extra_env = extra_env or {}
        self.stdio_script = Path(stdio_script or ROOT.parent / 'nexus-core/tools/memory_mcp_stdio.py').resolve()
        self.connections = asyncio.Queue()
        self.process = self.server = self.temporary = None
        self.group = None
        self.output = self.errors = b''

    async def __aenter__(self):
        try:
            self.temporary = tempfile.TemporaryDirectory(prefix='pet-voice-tls-')
            temporary = Path(self.temporary.name)
            certificate, key = temporary / 'certificate.pem', temporary / 'private.pem'
            generated = await asyncio.to_thread(subprocess.run, [
                'openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1',
                '-keyout', str(key), '-out', str(certificate)], capture_output=True, timeout=15)
            if generated.returncode:
                raise RuntimeError('fixture TLS certificate generation failed')
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certificate, key)

            async def connection(websocket):
                await self.connections.put(websocket)
                await websocket.wait_closed()

            self.server = await websockets.serve(connection, '127.0.0.1', 0, ssl=context, close_timeout=1)
            port = self.server.sockets[0].getsockname()[1]
            env = {'PATH': os.environ.get('PATH', os.defpath), 'PYTHONUTF8': '1',
                'MCP_ENDPOINT': f'wss://127.0.0.1:{port}/mcp?token={ENDPOINT_SECRET}',
                'NEXUS_MEMORY_MCP_URL': self.core_url, 'NEXUS_MEMORY_MCP_TOKEN': self.token}
            if self.trust_certificate:
                env['SSL_CERT_FILE'] = str(certificate)
            env.update(self.extra_env)
            self.process = await asyncio.create_subprocess_exec(sys.executable, '-I', str(ROOT / 'tools/memory_voice.py'),
                'run', '--directory', str(self.directory), '--stdio', str(self.stdio_script),
                env=env, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            line = await asyncio.wait_for(self.process.stdout.readline(), 15)
            report = json.loads(line)
            if report.get('status') != 'running':
                raise AssertionError('bridge process did not start')
            self.group = report['pid']
            self.output = line
            return self
        except BaseException:
            await self.close()
            raise

    async def connection(self, timeout=15):
        return await asyncio.wait_for(self.connections.get(), timeout)

    async def close(self):
        if self.process is not None:
            if self.process.returncode is None:
                self.process.terminate()
                try:
                    await asyncio.wait_for(self.process.wait(), 15)
                except asyncio.TimeoutError:
                    self.process.kill()
                    await self.process.wait()
                    raise AssertionError('bridge did not stop within its shutdown budget')
            self.output += await self.process.stdout.read()
            self.errors += await self.process.stderr.read()
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        if self.temporary is not None:
            self.temporary.cleanup()

    async def __aexit__(self, *_):
        await self.close()


async def request(websocket, method, identifier, params=None):
    message = {'jsonrpc': '2.0', 'id': identifier, 'method': method}
    if params is not None:
        message['params'] = params
    await websocket.send(json.dumps(message))
    result = json.loads(await asyncio.wait_for(websocket.recv(), 20))
    if result.get('jsonrpc') != '2.0' or type(result.get('id')) is not type(identifier) or result.get('id') != identifier:
        raise AssertionError('response identifier mismatch')
    return result


async def discover(websocket):
    initialized = await request(websocket, 'initialize', 0, {'protocolVersion': VERSION,
        'clientInfo': {'name': 'isolated-voice-fixture', 'version': '1'}, 'capabilities': {}})
    if initialized.get('result', {}).get('protocolVersion') != VERSION:
        raise AssertionError('protocol version mismatch')
    await websocket.send(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}))
    tools = await request(websocket, 'tools/list', 'discover')
    if [item['name'] for item in tools.get('result', {}).get('tools', [])] != ['memory.search']:
        raise AssertionError('unexpected tool exposure')


async def query(websocket, identifier='query'):
    return await request(websocket, 'tools/call', identifier,
        {'name': 'memory.search', 'arguments': {'query': 'public comment', 'limit': 1}})


async def probe(args):
    token = os.environ['NEXUS_MEMORY_MCP_TOKEN']
    async with VoiceFixture(args.bridge_directory, os.environ['NEXUS_MEMORY_MCP_URL'], token) as fixture:
        websocket = await fixture.connection()
        await discover(websocket)
        result = await query(websocket)
        payload = result.get('result', {})
        if payload.get('isError'):
            raise AssertionError('CORE returned a tool error')
        items = payload['structuredContent']['items']
        if args.expect_empty:
            if items:
                raise AssertionError('invalid source was returned')
        elif len(items) != 1 or items[0]['content'] != 'public-original-body':
            raise AssertionError('verified source was not returned')
        await websocket.close(1011, ENDPOINT_SECRET)
        reconnected = await fixture.connection()
        await discover(reconnected)
        response = await query(reconnected, 'after-reconnect')
        returned = response.get('result', {}).get('structuredContent', {}).get('items')
        # verifiedAt changes on every source revalidation; compare public content.
        if returned is None or [(item['messageId'], item['content']) for item in returned] != [(item['messageId'], item['content']) for item in items]:
            raise AssertionError('reconnected source result mismatch')
    if fixture.process.returncode != 0 or token.encode() in fixture.output + fixture.errors or ENDPOINT_SECRET.encode() in fixture.output + fixture.errors:
        raise AssertionError('bridge lifecycle or log isolation failed')
    return {'status': 'passed', 'transport': 'local-wss', 'reconnected': True, 'empty': args.expect_empty}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge-directory', type=Path, required=True)
    parser.add_argument('--expect-empty', action='store_true')
    args = parser.parse_args()
    print(json.dumps(asyncio.run(probe(args))))


if __name__ == '__main__':
    main()
