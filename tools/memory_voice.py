#!/usr/bin/env python3
"""Prepare and supervise a pinned upstream voice bridge. Never load a private env file."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

DEPLOYMENT = Path(__file__).resolve().parents[1] / 'deployment'
LOCK_PATH = DEPLOYMENT / 'memory-voice.lock.json'
REQUIREMENTS = DEPLOYMENT / 'memory-voice.requirements.txt'
MARKER = 'prepared.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def lock_files():
    source = LOCK_PATH.read_bytes()
    requirements = REQUIREMENTS.read_bytes()
    return json.loads(source), digest(source + b'\0' + requirements)


def clean_environment():
    # The upstream process must never inherit an administrator/provider key or a
    # config that can replace the explicit stdio target with an arbitrary command.
    result = {key: os.environ[key] for key in ('PATH', 'SYSTEMROOT', 'TMPDIR', 'SSL_CERT_FILE', 'SSL_CERT_DIR') if key in os.environ}
    result.update(PYTHONUTF8='1', PYTHON_DOTENV_DISABLED='1')
    return result


def command(arguments, *, timeout=120):
    result = subprocess.run(arguments, env=clean_environment(), stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise ValueError('dependency_command_failed')
    return result.stdout


def owned_directory(directory, fingerprint, create=False):
    marker = directory / MARKER
    expected = {'schemaVersion': 1, 'lockSha256': fingerprint}
    if marker.is_file() and not marker.is_symlink():
        if json.loads(marker.read_bytes()) == expected:
            return
        raise ValueError('prepared_lock_mismatch')
    if not create or directory.exists() and any(directory.iterdir()):
        raise ValueError('directory_not_prepared')
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    marker.write_text(json.dumps(expected) + '\n')


def verify(directory):
    lock, fingerprint = lock_files()
    owned_directory(directory, fingerprint)
    source = directory / 'mcp_pipe.py'
    if source.is_symlink() or digest(source.read_bytes()) != lock['runtimeSha256']:
        raise ValueError('bridge_source_mismatch')
    original = directory / 'mcp_pipe.upstream.py'
    if original.is_symlink() or digest(original.read_bytes()) != lock['sourceSha256']:
        raise ValueError('bridge_source_mismatch')
    config = directory / 'empty-config.json'
    if config.is_symlink() or config.read_bytes() != b'{}\n':
        raise ValueError('bridge_config_mismatch')
    python = directory / '.venv/bin/python'
    versions = json.loads(command([str(python), '-I', '-c',
        'import importlib.metadata,json;print(json.dumps({name:importlib.metadata.version(name) for name in ("websockets","python-dotenv")}))'], timeout=15))
    if versions != lock['packages']:
        raise ValueError('bridge_dependency_mismatch')
    return python


def prepare(directory, source_file):
    lock, fingerprint = lock_files()
    owned_directory(directory, fingerprint, create=True)
    if source_file:
        data = source_file.read_bytes()
    else:
        # Public, immutable source; no tool credential is sent during preparation.
        with urllib.request.urlopen(lock['sourceUrl'], timeout=20) as response:
            data = response.read(1024 * 1024 + 1)
    if digest(data) != lock['sourceSha256']:
        raise ValueError('bridge_source_mismatch')
    original = directory / 'mcp_pipe.upstream.py'
    if original.is_symlink() or original.exists() and original.read_bytes() != data:
        raise ValueError('existing_bridge_changed')
    original.write_bytes(data)
    # Pinned upstream otherwise leaves WebSocket receive waiting forever after
    # stdout EOF. Raise through its existing reconnect path; change no RPC data.
    before = b'logger.info(f"[{target}] Process has ended output")\n                break'
    after = b'logger.info(f"[{target}] Process has ended output")\n                raise RuntimeError("stdio_output_closed")'
    if data.count(before) != 1:
        raise ValueError('bridge_source_mismatch')
    data = data.replace(before, after)
    if digest(data) != lock['runtimeSha256']:
        raise ValueError('bridge_source_mismatch')
    source = directory / 'mcp_pipe.py'
    if source.is_symlink() or source.exists() and source.read_bytes() != data:
        raise ValueError('existing_bridge_changed')
    source.write_bytes(data)
    config = directory / 'empty-config.json'
    if config.is_symlink() or config.exists() and config.read_bytes() != b'{}\n':
        raise ValueError('existing_config_changed')
    config.write_bytes(b'{}\n')
    python = directory / '.venv/bin/python'
    if (directory / '.venv').is_symlink():
        raise ValueError('bridge_dependency_mismatch')
    if not python.exists():
        command([sys.executable, '-m', 'venv', str(directory / '.venv')])
    command([str(python), '-I', '-m', 'pip', '--isolated', '--disable-pip-version-check',
        'install', '--require-hashes', '--no-deps', '--only-binary=:all:', '-r', str(REQUIREMENTS)])
    verify(directory)


def runtime_environment():
    endpoint = os.environ.get('MCP_ENDPOINT', '')
    parts = urllib.parse.urlsplit(endpoint)
    if (parts.scheme != 'wss' or not parts.hostname or parts.username is not None
        or parts.password is not None or parts.fragment
        or any(character.isspace() or ord(character) < 33 for character in endpoint)):
        raise ValueError('invalid_voice_endpoint')
    try:
        parts.port
    except ValueError:
        raise ValueError('invalid_voice_endpoint') from None
    token = os.environ.get('NEXUS_MEMORY_MCP_TOKEN', '')
    if not 32 <= len(token) <= 256 or any(ord(character) < 33 or ord(character) > 126 for character in token):
        raise ValueError('invalid_memory_token')
    url = os.environ.get('NEXUS_MEMORY_MCP_URL', 'http://127.0.0.1:8088/mcp/memory')
    # The stdio adapter performs its own complete HTTP endpoint validation.
    result = clean_environment()
    result.update(MCP_ENDPOINT=endpoint, NEXUS_MEMORY_MCP_URL=url, NEXUS_MEMORY_MCP_TOKEN=token)
    return result


def signal_group(process, number):
    try:
        os.killpg(process.pid, number)
    except ProcessLookupError:
        pass
    except PermissionError:
        # Darwin can retain an unsignalable group after the parent has been
        # reaped and its last child has exited. Verify that no live member
        # remains; an actual permission failure must still fail shutdown.
        if sys.platform != 'darwin' or process.poll() is None:
            raise
        states = subprocess.run(['ps', '-axo', 'pgid=,stat='], capture_output=True,
            text=True, env=clean_environment(), timeout=3, check=True).stdout
        for line in states.splitlines():
            group, state = line.split()
            if int(group) == process.pid and not state.startswith('Z'):
                raise


def run(directory, stdio):
    if os.name != 'posix':
        raise ValueError('posix_process_groups_required')
    python = verify(directory)
    env = runtime_environment()
    if not stdio.is_file():
        raise ValueError('stdio_script_missing')
    env['MCP_CONFIG'] = str(directory / 'empty-config.json')
    # EOF exercises the adapter's actual configuration validation without HTTP.
    # Fail before opening the provider WebSocket if CORE configuration is invalid.
    checked = subprocess.run([str(python), '-I', str(stdio)], env=env,
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
    if checked.returncode or checked.stdout:
        raise ValueError('stdio_preflight_failed')
    # No raw upstream logs: exception and close reason text can contain provider
    # URLs, credentials or tool content. These messages describe process state only.
    stopped = False

    def stop(_signal, _frame):
        nonlocal stopped
        stopped = True

    previous = {number: signal.signal(number, stop) for number in (signal.SIGINT, signal.SIGTERM)}
    process = None
    try:
        # A new group lets CLI users stop the bridge and its stdio child together;
        # systemd additionally provides KillMode=control-group for crash cleanup.
        with tempfile.TemporaryDirectory(prefix='nexus-memory-voice-') as temporary:
            process = subprocess.Popen([str(python), '-I', str(directory / 'mcp_pipe.py'), str(stdio)],
                cwd=temporary, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, start_new_session=True)
            print(json.dumps({'status': 'running', 'pid': process.pid}), flush=True)
            while not stopped:
                try:
                    result = process.wait(timeout=0.5)
                    return result if result else 1  # An always-on bridge exiting needs supervisor attention.
                except subprocess.TimeoutExpired:
                    pass
            return 0
    finally:
        if process is not None:
            signal_group(process, signal.SIGTERM)
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                pass
            # The parent may exit first; still stop any child left in the group.
            signal_group(process, signal.SIGKILL)
            process.wait(timeout=3)
        for number, handler in previous.items():
            signal.signal(number, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('prepare', 'verify', 'run'))
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--source-file', type=Path, help='Offline source copy; SHA256 must match the pinned upstream')
    parser.add_argument('--stdio', type=Path, help='Explicit memory_mcp_stdio.py path for run')
    args = parser.parse_args()
    try:
        directory = args.directory.expanduser().resolve()
        if args.mode == 'prepare':
            prepare(directory, args.source_file)
            print(json.dumps({'status': 'prepared'}))
        elif args.mode == 'verify':
            verify(directory)
            print(json.dumps({'status': 'verified'}))
        else:
            if not args.stdio:
                raise ValueError('stdio_script_required')
            return run(directory, args.stdio.expanduser().resolve())
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as failure:
        # Whitelist our own reasons; never print an OS/network exception string.
        reasons = {'dependency_command_failed', 'prepared_lock_mismatch', 'directory_not_prepared',
            'bridge_source_mismatch', 'bridge_config_mismatch', 'bridge_dependency_mismatch',
            'existing_bridge_changed', 'existing_config_changed', 'invalid_voice_endpoint',
            'invalid_memory_token', 'posix_process_groups_required', 'stdio_script_missing',
            'stdio_script_required', 'stdio_preflight_failed'}
        reason = str(failure) if isinstance(failure, ValueError) and str(failure) in reasons else type(failure).__name__
        print('Voice bridge operation failed: ' + reason, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
