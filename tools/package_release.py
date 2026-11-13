#!/usr/bin/env python3
"""Build or verify an offline signed PET04 release. Never connects, flashes or publishes."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

IDENTITY=re.compile(rb'PET04_ID:(nexus_pet):(dodo-s3-lcd):(none|[0-9a-f]{64}):([0-9a-f]{64}):END')
def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('duplicate JSON key')
        result[key]=value
    return result
def load(path):return json.loads(path.read_text(),object_pairs_hook=unique)
def digest(data):return hashlib.sha256(data).hexdigest()
def inspect_image(data):
    matches=list(IDENTITY.finditer(data))
    if len(matches)!=1 or len(data)<288 or len(data)>0x400000 or data[0]!=0xE9 or struct.unpack_from('<I',data,32)[0]!=0xABCD5432:
        raise ValueError('not an identified PET04 ESP32-S3 app image')
    project=data[80:112].split(b'\0',1)[0].decode('ascii')
    version=data[48:80].split(b'\0',1)[0].decode('ascii')
    if project!='nexus_pet' or not re.fullmatch(r'[A-Za-z0-9._-]{1,32}',version):raise ValueError('invalid app descriptor')
    return dict(project=project,board=matches[0][2].decode(),modelSha256=matches[0][3].decode(),partitionSha256=matches[0][4].decode(),version=version)
def openssl(*args,**kwargs):
    return subprocess.run(['openssl',*map(str,args)],check=True,capture_output=True,**kwargs).stdout
def verify(package:Path,key:Path):
    if not (package/'usb/recovery-plan.json').is_file() or not (package/'usb/recovery-plan.sig').is_file():
        raise ValueError('complete signed USB recovery plan required')
    manifest=load(package/'manifest.json')
    if set(manifest)!={'payload','signature'}:raise ValueError('invalid signed envelope')
    raw=base64.b64decode(manifest['payload'],validate=True);signature=base64.b64decode(manifest['signature'],validate=True)
    if len(raw)>2048 or len(signature)!=384:raise ValueError('invalid envelope size')
    payload=json.loads(raw,object_pairs_hook=unique)
    keys={'format','project','board','version','modelSha256','partitionSha256','imageSha256','image','imageSize','releaseSequence','configSchema','growthSchema'}
    if set(payload)!=keys or payload['format']!=1 or payload['image']!='firmware.bin' or payload['configSchema']!=1 or payload['growthSchema']!=4:
        raise ValueError('invalid manifest schema')
    if type(payload['releaseSequence']) is not int or not 1<=payload['releaseSequence']<=0xFFFFFFFF:raise ValueError('invalid release sequence')
    with tempfile.TemporaryDirectory(prefix='pet-verify-') as directory:
        temp=Path(directory);(temp/'payload').write_bytes(raw);(temp/'signature').write_bytes(signature)
        openssl('rsa','-RSAPublicKey_in','-inform','DER','-in',key,'-pubout','-out',temp/'public.pem')
        openssl('dgst','-sha256','-verify',temp/'public.pem','-signature',temp/'signature',
                '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',temp/'payload')
        if (package/'usb/recovery-plan.json').exists():
            openssl('dgst','-sha256','-verify',temp/'public.pem','-signature',package/'usb/recovery-plan.sig',
                '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',package/'usb/recovery-plan.json')
    app=(package/'firmware.bin').read_bytes();identity=inspect_image(app)
    if any(payload[k]!=value for k,value in identity.items()) or payload['imageSize']!=len(app) or payload['imageSha256']!=digest(app):
        raise ValueError('image/manifest mismatch')
    if (package/'usb/recovery-plan.json').exists():
        plan=load(package/'usb/recovery-plan.json')
        required={'bootloader.bin','partition-table.bin','ota_data_initial.bin'}
        if identity['modelSha256']!='none':required.add('model.bin')
        names=[entry['file'] for entry in plan['images']]
        if set(names)!=required or len(names)!=len(required) or plan.get('format')!=1 or plan.get('flashBytes')!=16*1024*1024 or plan.get('project')!=identity['project'] or plan.get('board')!=identity['board']:
            raise ValueError('incomplete recovery plan')
        if plan['app']!={'file':'../firmware.bin','offset':0x20000,'sha256':digest(app),'bytes':len(app)}:raise ValueError('invalid recovery app')
        offsets={'bootloader.bin':0,'partition-table.bin':0x8000,'ota_data_initial.bin':0x19000,'model.bin':0xE20000}
        for entry in plan['images']:
            if entry['file'] not in {'bootloader.bin','partition-table.bin','ota_data_initial.bin','model.bin'}:raise ValueError('invalid recovery file')
            component=(package/'usb'/entry['file']).read_bytes()
            if digest(component)!=entry['sha256'] or type(entry['bytes']) is not int or len(component)!=entry['bytes']:raise ValueError('recovery file hash/size mismatch')
            if entry['offset']!=offsets[entry['file']]:raise ValueError('invalid recovery offset')
        model=package/'usb/model.bin'
        if identity['modelSha256']!='none' and (not model.exists() or digest(model.read_bytes())!=identity['modelSha256']):raise ValueError('missing paired model')
    return payload
def create(build:Path,key:Path,sequence:int,output:Path,partitions:Path):
    if output.exists() or not 1<=sequence<=0xFFFFFFFF:raise ValueError('new output and positive uint32 sequence required')
    if os.stat(key).st_mode&0o077:raise ValueError('signing key must have owner-only permissions')
    app=(build/'nexus_pet.bin').read_bytes();identity=inspect_image(app)
    if digest(partitions.read_bytes())!=identity['partitionSha256']:raise ValueError('partition identity mismatch')
    payload=dict(format=1,**identity,image='firmware.bin',imageSize=len(app),imageSha256=digest(app),releaseSequence=sequence,configSchema=1,growthSchema=4)
    raw=json.dumps(payload,sort_keys=True,separators=(',',':')).encode()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.pet-release-',dir=output.parent) as directory:
        stage=Path(directory)/'release';stage.mkdir();(stage/'firmware.bin').write_bytes(app)
        raw_file=Path(directory)/'payload';raw_file.write_bytes(raw)
        signature=openssl('dgst','-sha256','-sign',key,'-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',raw_file)
        if len(signature)!=384:raise ValueError('RSA-3072 signing key required')
        (stage/'manifest.json').write_text(json.dumps({'payload':base64.b64encode(raw).decode(),'signature':base64.b64encode(signature).decode()},separators=(',',':'))+'\n')
        public=Path(directory)/'public.der';openssl('rsa','-in',key,'-RSAPublicKey_out','-outform','DER','-out',public)
        config=build/'sdkconfig'
        if config.is_file() and re.search(r'^CONFIG_PET_OTA_ENABLED=y$',config.read_text(),re.MULTILINE) and public.read_bytes() not in app:
            raise ValueError('signing key does not match the embedded OTA key')
        usb=stage/'usb';usb.mkdir();entries=[]
        for source,name,offset in [('bootloader/bootloader.bin','bootloader.bin',0),('partition_table/partition-table.bin','partition-table.bin',0x8000),('ota_data_initial.bin','ota_data_initial.bin',0x19000)]:
            data=(build/source).read_bytes();(usb/name).write_bytes(data);entries.append(dict(file=name,offset=offset,sha256=digest(data),bytes=len(data)))
        if identity['modelSha256']!='none':
            data=(build/'srmodels/srmodels.bin').read_bytes()
            if digest(data)!=identity['modelSha256'] or len(data)>960*1024:raise ValueError('paired model mismatch')
            (usb/'model.bin').write_bytes(data);entries.append(dict(file='model.bin',offset=0xE20000,sha256=digest(data),bytes=len(data)))
        plan=dict(format=1,project='nexus_pet',board='dodo-s3-lcd',flashBytes=16*1024*1024,
            preflight=['Confirm physical chip ESP32-S3 N16R8 and PET04 identity','Export growth and capture full flash backup before writing','Compare existing partition layout; refuse 02/03 automatic migration','Stop external actuators and use stable power'],
            app=dict(file='../firmware.bin',offset=0x20000,sha256=digest(app),bytes=len(app)),images=entries,
            preserve=['nvs 0x9000:0x10000','storage 0x820000:0x200000','assets 0xA20000:0x400000'],
            warning='Review this plan before a separately authorized USB operation. No erase-all command is provided. Never copy 02/03 NVS blobs into PET04.')
        (usb/'recovery-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
        (usb/'recovery-plan.sig').write_bytes(openssl('dgst','-sha256','-sign',key,'-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',usb/'recovery-plan.json'))
        verify(stage,public)
        os.rename(stage,output)
    return payload
def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='action',required=True)
    make=sub.add_parser('create');make.add_argument('--build',type=Path,required=True);make.add_argument('--private-key',type=Path,required=True);make.add_argument('--sequence',type=int,required=True);make.add_argument('--output',type=Path,required=True)
    make.add_argument('--partitions',type=Path,default=Path(__file__).resolve().parents[1]/'firmware/partitions/16m.csv')
    check=sub.add_parser('verify');check.add_argument('--package',type=Path,required=True);check.add_argument('--public-key',type=Path,required=True,help='RSA-3072 PKCS#1 public DER file')
    args=parser.parse_args()
    try:
        payload=create(args.build,args.private_key,args.sequence,args.output,args.partitions) if args.action=='create' else verify(args.package,args.public_key)
        print(json.dumps(dict(ok=True,action=args.action,version=payload['version'],releaseSequence=payload['releaseSequence'],imageBytes=payload['imageSize']),ensure_ascii=False));return 0
    except (OSError,ValueError,KeyError,TypeError,UnicodeError,subprocess.CalledProcessError) as error:
        parser.exit(1,f'release rejected: {type(error).__name__}\n')
if __name__=='__main__':raise SystemExit(main())
