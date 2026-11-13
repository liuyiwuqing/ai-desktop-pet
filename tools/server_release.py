#!/usr/bin/env python3
"""Package, verify and preflight a NEXUS server release; never install or start services."""
from __future__ import annotations

import argparse
import hashlib
import io
import ipaddress
import json
import os
from pathlib import Path
import platform
import re
import shutil
import ssl
import stat
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CORE_FILES = ('compose.yml', 'tools/init_local.py', 'tools/broker_policy.py',
              'tools/upgrade_broker_policy.py', 'tools/memory_mcp_stdio.py', 'tools/credential_store.py')
PET_FILES = ('deployment/core.env.example', 'deployment/memory-voice.env.example',
             'deployment/nexus-core.service', 'deployment/nexus-memory-voice.service',
             'deployment/Caddyfile', 'deployment/memory-voice.lock.json',
             'deployment/memory-voice.requirements.txt', 'tools/memory_voice.py', 'tools/server_release.py')
MEMBERS = {'README.md', 'core/nexus-core.jar', 'core-source.tar', 'core-tools/memory_mcp_stdio.py'}
MEMBERS.update('core/' + name for name in CORE_FILES if name != 'tools/memory_mcp_stdio.py')
MEMBERS.update('desktop-pet/' + name for name in PET_FILES)
LEGACY_MEMBERS=MEMBERS-{'core/tools/credential_store.py'}
MANIFEST_KEYS = {'format', 'version', 'coreCommit', 'petCommit', 'petDirty', 'files'}


class Rejected(Exception):
    def __init__(self, reason, fields=()):
        super().__init__(reason)
        self.fields = fields


def require(condition, reason):
    if not condition:
        raise Rejected(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate_json_key')
        result[key] = value
    return result


def load_json(data):
    return json.loads(data, object_pairs_hook=unique)


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True, timeout=30)
    require(result.returncode == 0, 'git_source_unavailable')
    return result.stdout


def inspect_jar(data):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = set(archive.namelist())
        required = {'META-INF/MANIFEST.MF', 'BOOT-INF/classes/dev/nexus/core/NexusCore.class',
                    'BOOT-INF/classes/static/index.html',
                    'BOOT-INF/classes/db/migration/V10__memory_copy_lifecycle.sql'}
        require(required <= names, 'incomplete_core_jar')
        require(archive.getinfo('META-INF/MANIFEST.MF').file_size <= 16384, 'invalid_jar_manifest')
        manifest = archive.read('META-INF/MANIFEST.MF').decode('utf-8')
        require(re.search(r'^Start-Class: dev\.nexus\.core\.NexusCore\r?$', manifest, re.M), 'wrong_core_application')
        version = re.search(r'^Implementation-Version: ([A-Za-z0-9._-]{1,64})\r?$', manifest, re.M)
        require(version, 'missing_core_version')
        return version[1]


def verify(bundle):
    with zipfile.ZipFile(bundle) as archive:
        entries = archive.infolist()
        names = [item.filename for item in entries]
        members=set(names)-{'manifest.json'}
        require(len(names) == len(set(names)) and 'manifest.json' in names and members in (MEMBERS,LEGACY_MEMBERS), 'unexpected_bundle_members')
        for item in entries:
            limit = 128 * 1024 * 1024 if item.filename == 'core/nexus-core.jar' else 16 * 1024 * 1024
            require(0 < item.file_size <= limit and stat.S_IFMT(item.external_attr >> 16) in (0, stat.S_IFREG),
                    'invalid_bundle_member')
        require(archive.getinfo('manifest.json').file_size <= 65536, 'invalid_bundle_manifest')
        manifest = load_json(archive.read('manifest.json'))
        require(isinstance(manifest, dict) and set(manifest) == MANIFEST_KEYS and type(manifest['format']) is int
                and manifest['format'] in (1,2) and type(manifest['petDirty']) is bool, 'invalid_bundle_manifest')
        require(members==(LEGACY_MEMBERS if manifest['format']==1 else MEMBERS),'bundle_format_member_mismatch')
        for key in ('coreCommit', 'petCommit'):
            require(isinstance(manifest[key], str) and re.fullmatch(r'[0-9a-f]{40,64}', manifest[key]),
                    'invalid_source_commit')
        require(isinstance(manifest['files'], dict) and set(manifest['files']) == members, 'incomplete_bundle_manifest')
        for name, record in manifest['files'].items():
            require(isinstance(record, dict) and set(record) == {'bytes', 'sha256'}
                    and type(record['bytes']) is int and isinstance(record['sha256'], str), 'invalid_member_record')
            data = archive.read(name)
            require(record['bytes'] == len(data) and record['sha256'] == sha(data), 'bundle_hash_mismatch')
        require(inspect_jar(archive.read('core/nexus-core.jar')) == manifest['version'], 'bundle_version_mismatch')
        return manifest


def create(core, jar, output):
    core = core.resolve()
    require(git(core, 'rev-parse', '--show-toplevel').decode().strip() == str(core), 'core_root_mismatch')
    require(not git(core, 'status', '--porcelain'), 'uncommitted_core_sources')
    core_commit = git(core, 'rev-parse', 'HEAD').decode().strip()
    tracked = git(core, 'ls-tree', '-rz', '--name-only', 'HEAD').decode().split('\0')
    for name in filter(None, tracked):
        require(not (name.startswith(('data/', 'target/', 'frontend/node_modules/', 'frontend/dist/'))
                     or Path(name).name.startswith('.env') or Path(name).suffix.lower() in {'.key', '.pem', '.p12', '.pfx'}),
                'private_or_generated_source_tracked')
    data = {'core/nexus-core.jar': jar.read_bytes(), 'core-source.tar': git(core, 'archive', '--format=tar', 'HEAD'),
            'README.md': (ROOT / 'deployment/server-release.md').read_bytes()}
    version = inspect_jar(data['core/nexus-core.jar'])
    for name in CORE_FILES:
        target = 'core-tools/memory_mcp_stdio.py' if name.endswith('memory_mcp_stdio.py') else 'core/' + name
        require(not (core / name).is_symlink(), 'symlink_source_rejected')
        data[target] = (core / name).read_bytes()
    for name in PET_FILES:
        require(not (ROOT / name).is_symlink(), 'symlink_source_rejected')
        data['desktop-pet/' + name] = (ROOT / name).read_bytes()
    # Recheck the source after copying, so a concurrent edit cannot acquire clean provenance.
    require(not git(core, 'status', '--porcelain') and git(core, 'rev-parse', 'HEAD').decode().strip() == core_commit,
            'core_changed_during_packaging')
    manifest = {'format': 2, 'version': version, 'coreCommit': core_commit,
                'petCommit': git(ROOT, 'rev-parse', 'HEAD').decode().strip(),
                'petDirty': bool(git(ROOT, 'status', '--porcelain')),
                'files': {name: {'bytes': len(raw), 'sha256': sha(raw)} for name, raw in sorted(data.items())}}
    require(not output.exists(), 'output_already_exists')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nexus-server-', dir=output.parent) as temporary:
        staged = Path(temporary) / 'release.zip'
        with zipfile.ZipFile(staged, 'w', compression=zipfile.ZIP_STORED) as archive:
            for name, raw in sorted({**data, 'manifest.json': (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()}.items()):
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(info, raw)
        verify(staged)
        # Publishing a hard link refuses an existing destination, including a concurrent creator.
        os.link(staged, output)
    return manifest


def read_env(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and not info.st_mode & 0o077 and info.st_size <= 65536,
            'env_requires_owner_only_regular_file')
    result = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        key, separator, value = line.partition('=')
        key, value = key.strip(), value.strip()
        require(separator and re.fullmatch(r'[A-Z][A-Z0-9_]*', key) and key not in result, 'unsupported_env_syntax')
        # This is the documented single-line, unquoted template subset, never shell evaluation.
        require(not any(c in value for c in ('\x00', '\\', '"', "'")), 'unsupported_env_syntax')
        result[key] = value
    return result


def endpoint(value, schemes, loopback_http=True, query=False):
    require(not any(ord(c) <= 32 or ord(c) == 127 for c in value), 'invalid_endpoint')
    parsed = urllib.parse.urlsplit(value)
    require(parsed.scheme in schemes and parsed.hostname and not parsed.username and not parsed.password
            and not parsed.fragment and (query or not parsed.query), 'invalid_endpoint')
    require(not parsed.hostname.lower().endswith(('.invalid', '.example')) and parsed.hostname != 'example.com',
            'placeholder_endpoint')
    if parsed.port is not None:
        require(1 <= parsed.port <= 65535, 'invalid_endpoint')
    if parsed.scheme == 'http' and loopback_http:
        try:
            local = ipaddress.ip_address(parsed.hostname).is_loopback
        except ValueError:
            local = parsed.hostname == 'localhost'
        require(local, 'remote_http_rejected')
    return parsed


def credential(value):
    require(32 <= len(value) <= 256 and all(33 <= ord(c) <= 126 for c in value), 'invalid_machine_credential')


def private_json(filename):
    path=Path(filename)
    require(path.is_absolute() and not path.parent.stat().st_mode & 0o022, 'private_token_directory_required')
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    with os.fdopen(fd,'rb') as source:
        info=os.fstat(source.fileno())
        require(stat.S_ISREG(info.st_mode) and stat.S_IMODE(info.st_mode) in (0o400,0o600), 'private_token_file_required')
        raw=source.read(16385);require(0<len(raw)<=16384,'invalid_token_file_size')
    return load_json(raw)


def token_fields(value,required,optional=()):
    require(isinstance(value,dict) and set(required)<=set(value)<=set(required)|set(optional), 'invalid_token_file_fields')


def server_tokens(env):
    """Only configuration validation: runtime owns revision history and authorization."""
    if not env.get('NEXUS_AUTH_TOKEN_FILE'):
        credential(env.get('NEXUS_ADMIN_TOKEN',''))
        return {'admin':[env['NEXUS_ADMIN_TOKEN']], 'memory':[env.get('NEXUS_MEMORY_MCP_TOKEN','')]}
    value=private_json(env['NEXUS_AUTH_TOKEN_FILE']);token_fields(value,{'format','revision','adminActor','admin'},{'memory'})
    require(type(value['format']) is int and value['format']==1 and type(value['revision']) is int and 1<=value['revision']<1<<63, 'invalid_token_file_format')
    require(isinstance(value['adminActor'],str) and re.fullmatch('[0-9a-f]{64}',value['adminActor']), 'invalid_token_file_identity')
    ids=set();secrets=set();scopes={'admin':[],'memory':[]};now=int(time.time())
    for name in scopes:
        if name not in value:continue
        scope=value[name];token_fields(scope,{'current'},{'previous'})
        for slot in ('current','previous'):
            if slot not in scope:continue
            entry=scope[slot];token_fields(entry,{'id','token'}|({'expiresAt'} if slot=='previous' else set()))
            require(isinstance(entry['id'],str) and re.fullmatch('[a-zA-Z0-9_-]{1,40}',entry['id']) and entry['id'] not in ids, 'invalid_token_file_id')
            credential(entry['token']);require(entry['token'] not in secrets,'credential_reuse')
            ids.add(entry['id']);secrets.add(entry['token'])
            if slot=='previous':require(type(entry['expiresAt']) is int and 1<=entry['expiresAt']<=now+86400,'invalid_token_overlap')
            if slot=='current' or entry['expiresAt']>now:scopes[name].append(entry['token'])
    return scopes


def voice_token(env):
    if not env.get('NEXUS_MEMORY_MCP_TOKEN_FILE'):
        credential(env.get('NEXUS_MEMORY_MCP_TOKEN',''));return env['NEXUS_MEMORY_MCP_TOKEN']
    value=private_json(env['NEXUS_MEMORY_MCP_TOKEN_FILE']);token_fields(value,{'format','revision','id','token'})
    require(type(value['format']) is int and value['format']==1 and type(value['revision']) is int and 1<=value['revision']<1<<63, 'invalid_token_file_format')
    require(isinstance(value['id'],str) and re.fullmatch('[a-zA-Z0-9_-]{1,40}',value['id']), 'invalid_token_file_id')
    credential(value['token']);return value['token']


def enabled(env, name):
    value = env.get(name, 'false')
    require(value in ('true', 'false'), 'invalid_feature_switch')
    return value == 'true'


def required_fields(env, names):
    missing = [name for name in names if not env.get(name)]
    if missing:
        raise Rejected('required_configuration_missing', missing)


def preflight(env_file, voice_file, check_host):
    env = read_env(env_file)
    required_fields(env, ('NEXUS_DB_URL', 'NEXUS_DB_USER', 'NEXUS_DB_PASSWORD',
                         'NEXUS_MQTT_URI', 'NEXUS_MQTT_USERNAME', 'NEXUS_MQTT_PASSWORD', 'NEXUS_MQTT_CA_CERT'))
    if not env.get('NEXUS_AUTH_TOKEN_FILE'):required_fields(env,('NEXUS_ADMIN_TOKEN',))
    tokens=server_tokens(env)
    require(env.get('NEXUS_BIND_ADDRESS', '127.0.0.1') in ('127.0.0.1', '::1'), 'core_must_bind_loopback')
    require(env.get('NEXUS_PORT', '8088').isdigit() and 1 <= int(env.get('NEXUS_PORT', '8088')) <= 65535, 'invalid_core_port')
    require(env['NEXUS_DB_URL'].startswith('jdbc:postgresql://'), 'invalid_database_url')
    endpoint(env['NEXUS_DB_URL'][5:], ('postgresql',), loopback_http=False, query=True)
    endpoint(env['NEXUS_MQTT_URI'], ('ssl',))
    require(not enabled(env, 'NEXUS_MQTT_ALLOW_INSECURE'), 'insecure_mqtt_rejected')
    require(Path(env['NEXUS_MQTT_CA_CERT']).is_absolute(), 'mqtt_ca_requires_absolute_path')
    require(not any(value for key, value in env.items() if key.endswith('_CONFIG_FILE')), 'development_config_reference_rejected')
    features = {name: enabled(env, 'NEXUS_' + name + '_ENABLED')
                for name in ('HALO', 'AI', 'AI_AUTO', 'MEMORY', 'MEMORY_MCP', 'BRIDGE', 'RETENTION', 'OTA', 'GROWTH_SYNC')}
    require(not features['AI_AUTO'] or features['AI'], 'automatic_ai_requires_ai')
    for feature, names in {'HALO': ('URL', 'TOKEN'), 'AI': ('BASE_URL', 'API_KEY', 'MODEL'),
                           'MEMORY': ('URL', 'API_KEY', 'ACCOUNT', 'USER', 'PEER'), 'BRIDGE': ('URL', 'TOKEN')}.items():
        if features[feature]:
            required_fields(env, ['NEXUS_' + feature + '_' + name for name in names])
            key = 'NEXUS_' + feature + ('_BASE_URL' if feature == 'AI' else '_URL')
            endpoint(env[key], ('https', 'http'))
    if features['MEMORY_MCP']:
        require(features['MEMORY'] and features['HALO'], 'memory_mcp_requires_memory_and_halo')
        require(bool(tokens['memory']),'machine_token_missing')
        for token in tokens['memory']:credential(token)
        require(not set(tokens['memory']) & set(tokens['admin']), 'machine_token_reuses_admin')
    if voice_file:
        voice = read_env(voice_file)
        require(features['MEMORY_MCP'], 'voice_requires_memory_mcp')
        require(voice_token(voice) in tokens['memory'], 'voice_token_mismatch')
        url = endpoint(voice.get('NEXUS_MEMORY_MCP_URL', ''), ('https', 'http'))
        require(url.path == '/mcp/memory', 'wrong_memory_tool_path')
        endpoint(voice.get('MCP_ENDPOINT', ''), ('wss',), query=True)
    host = {}
    if check_host:
        require(platform.system() == 'Linux', 'linux_host_required')
        require(Path('/proc/1/comm').read_text().strip() == 'systemd', 'systemd_host_required')
        require(shutil.which('systemctl') and shutil.which('systemd-analyze') and os.access('/usr/bin/java', os.X_OK),
                'host_dependency_missing')
        # Match ExecStart rather than a different JDK shadowing it on PATH.
        result = subprocess.run(['/usr/bin/java', '-version'], capture_output=True, text=True, timeout=10)
        version = re.search(r'(?:version|openjdk)\s+"?(\d+)', result.stderr + result.stdout)
        require(result.returncode == 0 and version and int(version[1]) >= 21, 'java_21_required')
        ca = Path(env['NEXUS_MQTT_CA_CERT'])
        require(ca.is_file() and os.access(ca, os.R_OK), 'mqtt_ca_unreadable')
        ssl.create_default_context(cafile=str(ca))
        host['javaMajor'] = int(version[1])
        if voice_file:
            result = subprocess.run(['/usr/bin/python3', '--version'], capture_output=True, text=True, timeout=10)
            version = re.search(r'Python (\d+)\.(\d+)', result.stdout + result.stderr)
            require(result.returncode == 0 and version and (int(version[1]), int(version[2])) >= (3, 9),
                    'service_python_39_required')
    return {'hostChecked': check_host, 'voiceConfigured': bool(voice_file), 'features': features,
            'authMode':'protected_file' if env.get('NEXUS_AUTH_TOKEN_FILE') else 'environment',
            'warnings': [name.lower() + '_enabled_requires_operational_review'
                         for name in ('RETENTION', 'BRIDGE') if features[name]], **host}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, newurl):
        return None


def probe(env_file, url, ca):
    env = read_env(env_file)
    token = server_tokens(env)['admin'][0]
    credential(token)
    parsed = endpoint(url, ('https', 'http'))
    require(parsed.path in ('', '/'), 'core_base_url_required')
    context = ssl.create_default_context(cafile=str(ca) if ca else None)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect(),
                                        urllib.request.HTTPSHandler(context=context))

    def request(auth):
        headers = {'Authorization': 'Bearer ' + token} if auth else {}
        req = urllib.request.Request(url.rstrip('/') + '/api/v1/status', headers=headers)
        try:
            response = opener.open(req, timeout=8)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            data = response.read(2049)
            require(len(data) <= 2048, 'status_response_too_large')
            return response.code, data

    status, _ = request(False)
    require(status == 401, 'authentication_boundary_failed')
    status, raw = request(True)
    require(status == 200, 'authenticated_status_unavailable')
    body = load_json(raw)
    require(isinstance(body, dict) and type(body.get('schemaVersion')) is int and body['schemaVersion'] == 1,
            'invalid_status_response')
    require(body.get('database') == 'ready' and body.get('mqtt') == 'ready', 'service_dependencies_unavailable')
    return {'unauthenticatedStatus': 401, 'authenticatedStatus': 200, 'database': 'ready', 'mqtt': 'ready'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    make = sub.add_parser('create')
    make.add_argument('--core', type=Path, required=True)
    make.add_argument('--jar', type=Path)
    make.add_argument('--output', type=Path, required=True)
    check = sub.add_parser('verify')
    check.add_argument('--bundle', type=Path, required=True)
    ready = sub.add_parser('preflight')
    ready.add_argument('--env-file', type=Path, required=True)
    ready.add_argument('--voice-env-file', type=Path)
    ready.add_argument('--check-host', action='store_true')
    live = sub.add_parser('probe')
    live.add_argument('--env-file', type=Path, required=True)
    live.add_argument('--url', required=True)
    live.add_argument('--ca', type=Path)
    args = parser.parse_args()
    fields = ()
    try:
        if args.action in ('create', 'verify'):
            bundle = args.output if args.action == 'create' else args.bundle
            manifest = create(args.core, args.jar or args.core / 'target/nexus-core-0.1.0-SNAPSHOT.jar', bundle) \
                if args.action == 'create' else verify(bundle)
            result = {'version': manifest['version'], 'coreCommit': manifest['coreCommit'],
                      'petCommit': manifest['petCommit'], 'petDirty': manifest['petDirty'],
                      'files': len(manifest['files']), 'bundleSha256': sha(bundle.read_bytes())}
        elif args.action == 'preflight':
            result = preflight(args.env_file, args.voice_env_file, args.check_host)
        else:
            result = probe(args.env_file, args.url, args.ca)
        print(json.dumps({'ok': True, 'action': args.action, **result}, sort_keys=True))
        return 0
    except Rejected as error:
        reason = str(error)
        fields = error.fields
    except (urllib.error.URLError, TimeoutError):
        reason = 'endpoint_unavailable'
    except (OSError, ValueError, TypeError, KeyError, UnicodeError, zipfile.BadZipFile, subprocess.SubprocessError):
        reason = 'invalid_input_or_io_failure'
    print(json.dumps({'ok': False, 'action': args.action, 'reason': reason,
                      **({'fields': fields} if fields else {})}, sort_keys=True))
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
