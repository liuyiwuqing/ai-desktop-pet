#!/usr/bin/env python3
"""Plan or build isolated ESP-IDF presets, with evidence from actual output files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'firmware'
PROFILES=('default','full','off','tones')

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def invoke(command,**options):return subprocess.run(command,check=True,**options)
def config_values(path):
    values={}
    for line in path.read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key,value=line.split('=',1);values[key]=value
        elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
            values[line[2:-11]]='n'
    return values

def inventory():
    # PyYAML is part of the ESP-IDF Python environment, not a new project dependency.
    import yaml
    lock=yaml.safe_load((FW/'dependencies.lock').read_text())
    entries=[]
    for name,info in sorted(lock['dependencies'].items()):
        component=FW/'managed_components'/name.replace('/','__')
        manifest=component/'idf_component.yml'
        declaration=yaml.safe_load(manifest.read_text()) if manifest.is_file() else {}
        licenses=[]
        for pattern in ('LICENSE*','LICENCE*','COPYING*'):
            for path in sorted(component.glob(pattern)):
                if path.is_file() and not path.is_symlink():
                    licenses.append({'path':str(path.relative_to(FW)),'sha256':digest(path)})
        entries.append({'name':name,'version':str(info['version']),'componentHash':info.get('component_hash'),
            'declaredLicense':(declaration or {}).get('license','NOASSERTION'),'licenseFiles':licenses})
    font=json.loads((ROOT/'resources/fonts/manifest.json').read_text())
    return {'format':1,'scope':'resolved firmware dependencies and bundled fonts; not a vulnerability scan',
        'components':entries,'sdk':{'version':lock['dependencies']['idf']['version'],
        'licenseSha256':digest(Path(os.environ['IDF_PATH'])/'LICENSE')},'fonts':{'declaredLicense':font['license'],
        'licenseSha256':digest(ROOT/'resources/fonts/OFL.txt')}}

def source_manifest():
    result=invoke(['git','ls-files','--cached','--others','--exclude-standard','-z','--','.'],cwd=ROOT,capture_output=True)
    files=[]
    for name in sorted(set(result.stdout.decode().split('\0'))-{''}):
        path=ROOT/name
        if path.is_file() and not path.is_symlink() and (name.startswith(('firmware/','extensions/','resources/','config/')) or name in ('tools/board_codegen.py','tools/build_identity.py','tools/check_sr_models.py','tools/schema_codegen.py','tools/build_matrix.py')):
            files.append({'path':name,'sha256':digest(path)})
    return files

def source_state():
    """Record reviewable names/statuses, without copying working-tree contents."""
    base=invoke(['git','rev-parse','HEAD'],cwd=ROOT,capture_output=True,text=True).stdout.strip()
    fields=invoke(['git','diff','--name-status','--no-renames','-z','HEAD','--'],cwd=ROOT,capture_output=True).stdout.decode().split('\0')
    if fields[-1]=='':fields.pop()
    if len(fields)%2:raise ValueError('unexpected Git change listing')
    tracked=[{'status':fields[i],'path':fields[i+1]} for i in range(0,len(fields),2)]
    untracked=invoke(['git','ls-files','--others','--exclude-standard','-z'],cwd=ROOT,capture_output=True).stdout.decode().split('\0')
    return {'baseCommit':base,'tracked':tracked,'untracked':sorted(set(untracked)-{''})}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    selection=parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--profile',choices=PROFILES)
    selection.add_argument('--all',action='store_true')
    parser.add_argument('--build-root',type=Path,default=FW/'build-matrix')
    parser.add_argument('--ota-public-key',type=Path)
    parser.add_argument('--ota-base-url',default='https://releases.invalid/pet')
    parser.add_argument('--diagnostics',action='store_true',help='Opt in to flash core dumps and crash recovery')
    parser.add_argument('--execute',action='store_true',help='Build; the default only prints a plan')
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    names=PROFILES if args.all else (args.profile,)
    build_root=args.build_root.expanduser().resolve()
    if args.jobs<1 or args.jobs>8:parser.error('jobs must be 1..8')
    if 'full' in names and (not args.ota_public_key or not args.ota_public_key.is_file()):
        parser.error('full requires --ota-public-key with an explicit RSA-3072 PKCS#1 public DER file')
    if not re.fullmatch(r'https://[A-Za-z0-9.-]+(?::[0-9]{1,5})?(?:/[A-Za-z0-9_./-]*)?',args.ota_base_url):
        parser.error('ota-base-url must be HTTPS without credentials, query or fragment')
    builds=[]
    for name in names:
        directory=build_root/name
        if directory==FW or directory==FW/'build':parser.error('choose an isolated build root')
        defaults=[FW/'sdkconfig.defaults',FW/'profiles'/f'{name}.defaults']
        if args.diagnostics or name=='full':defaults.append(FW/'profiles/diagnostic.defaults')
        if name=='full':defaults.append(directory/'ota.defaults')
        command=['idf.py','-C',str(FW),'-B',str(directory),'-DSDKCONFIG='+str(directory/'sdkconfig'),
            '-DSDKCONFIG_DEFAULTS='+';'.join(map(str,defaults)),'build']
        builds.append({'profile':name,'command':command,'directory':str(directory)})
    if not args.execute:
        print(json.dumps({'executed':False,'builds':builds},ensure_ascii=False,indent=2));return
    executable=shutil.which('idf.py')
    if not executable:parser.error('activate ESP-IDF 6.1 before --execute')
    version=invoke([executable,'--version'],capture_output=True,text=True).stdout.strip()
    if not re.fullmatch(r'ESP-IDF v6\.1(?:\.[0-9]+)?',version):parser.error('ESP-IDF release 6.1 is required')
    if 'full' in names:
        key=invoke(['openssl','rsa','-RSAPublicKey_in','-inform','DER','-in',str(args.ota_public_key.resolve()),'-text','-noout'],capture_output=True,text=True)
        if '3072 bit' not in key.stdout:parser.error('OTA public key must be RSA-3072')
    compiler=invoke(['xtensa-esp-elf-gcc','--version'],capture_output=True,text=True).stdout.splitlines()[0]
    env=os.environ.copy();env['CMAKE_BUILD_PARALLEL_LEVEL']=str(args.jobs);env['IDF_PY_BUILD_JOBS']=str(args.jobs)
    for entry in builds:
        directory=Path(entry['directory']);stamp=directory/'matrix-inputs.json'
        inputs={'format':1,'profile':entry['profile'],'diagnostics':args.diagnostics or entry['profile']=='full','idf':version,
            'defaults':{p.name:digest(p) for p in (FW/'sdkconfig.defaults',FW/'profiles'/f"{entry['profile']}.defaults")},
            'lock':digest(FW/'dependencies.lock')}
        if inputs['diagnostics']:inputs['defaults']['diagnostic.defaults']=digest(FW/'profiles/diagnostic.defaults')
        if entry['profile']=='full':inputs.update(publicKey=digest(args.ota_public_key),otaBase=args.ota_base_url)
        if directory.exists():
            if not stamp.is_file():parser.error(f'unowned build directory: {directory}')
            saved=json.loads(stamp.read_text());expected=saved.pop('sdkconfigSha256',None)
            if saved!=inputs:parser.error(f'preset inputs changed; select a new --build-root: {directory}')
            if expected and (not (directory/'sdkconfig').is_file() or digest(directory/'sdkconfig')!=expected):
                parser.error(f'generated config was modified; select a new --build-root: {directory}')
        else:directory.mkdir(parents=True)
        stamp.write_text(json.dumps(inputs,indent=2)+'\n')
        if entry['profile']=='full':
            (directory/'ota.defaults').write_text('CONFIG_PET_OTA_PUBLIC_KEY_DER='+json.dumps(str(args.ota_public_key.resolve()))+'\nCONFIG_PET_OTA_BASE_URL='+json.dumps(args.ota_base_url)+'\n')
        sources=source_manifest()
        command=entry['command'].copy();command[0]=executable
        with (directory/'build.log').open('w') as log:
            invoke(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        if source_manifest()!=sources:raise ValueError('build inputs changed during compilation; rerun after edits settle')
        values=config_values(directory/'sdkconfig')
        if values.get('CONFIG_APP_REPRODUCIBLE_BUILD')!='y':raise ValueError('reproducible build flag was not applied')
        expected={'off':{'CONFIG_PET_MODULE_NET_ENABLED':'n','CONFIG_PET_MODULE_AUDIO_ENABLED':'n'},
            'tones':{'CONFIG_PET_MESSAGE_TONES_ON_BOOT':'y','CONFIG_PET_AI_XIAOZHI_ON_BOOT':'n'},
            'default':{'CONFIG_PET_AI_XIAOZHI_ON_BOOT':'n'},
            'full':{'CONFIG_PET_AI_ESP_SR_ENABLED':'y','CONFIG_PET_OTA_ENABLED':'y','CONFIG_PET_EXT_SERVO':'y'}}[entry['profile']]
        for default in (FW/'sdkconfig.defaults',FW/'profiles'/f"{entry['profile']}.defaults"):
            expected.update(config_values(default))
        if inputs['diagnostics']:expected.update(config_values(FW/'profiles/diagnostic.defaults'))
        if inputs['diagnostics']:expected.update(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH='y',CONFIG_PET_RECOVERY_ENABLED='y')
        for name,value in expected.items():
            if values.get(name)!=value:raise ValueError(f'preset flag was not applied: {name}')
        inputs['sdkconfigSha256']=digest(directory/'sdkconfig');stamp.write_text(json.dumps(inputs,indent=2)+'\n')
        artifacts=[]
        for relative in ('nexus_pet.bin','nexus_pet.elf','bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin','srmodels/srmodels.bin'):
            path=directory/relative
            if path.is_file():artifacts.append({'path':relative,'bytes':path.stat().st_size,'sha256':digest(path)})
        if not {'nexus_pet.bin','nexus_pet.elf','bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin'}.issubset({item['path'] for item in artifacts}):
            raise ValueError('required build outputs missing')
        evidence={'format':1,'profile':entry['profile'],'firmwareVersion':(FW/'version.txt').read_text().strip(),
            'idf':version,'compiler':compiler,'inputs':inputs,'artifacts':artifacts,'changes':source_state(),
            'sourceFiles':sources,'sourceTreeSha256':hashlib.sha256(json.dumps(sources,sort_keys=True).encode()).hexdigest(),'flashExecuted':False,'signedRelease':False}
        (directory/'build-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
        (directory/'dependency-licenses.json').write_text(json.dumps(inventory(),ensure_ascii=False,indent=2)+'\n')
        print(json.dumps({'profile':entry['profile'],'built':True,'evidence':str(directory/'build-evidence.json')}),flush=True)

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(f'build matrix failed: {error}',file=sys.stderr);sys.exit(1)
