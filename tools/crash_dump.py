#!/usr/bin/env python3
"""Plan a non-resetting flash read, or decode an existing dump using the activated SDK."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]

def partition():
    rows=csv.reader(line for line in (ROOT/'firmware/partitions/16m.csv').read_text().splitlines() if line.strip() and not line.lstrip().startswith('#'))
    found=[row for row in rows if row[0].strip()=='coredump']
    if len(found)!=1 or found[0][1].strip()!='data' or found[0][2].strip()!='coredump':raise ValueError('invalid coredump partition')
    return int(found[0][3].strip(),0),int(found[0][4].strip(),0)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('read','decode'))
    parser.add_argument('--port')
    parser.add_argument('--core',type=Path)
    parser.add_argument('--elf',type=Path)
    parser.add_argument('--elf-sha256',help='Expected ELF SHA256 from the device diagnostics or matching release evidence')
    parser.add_argument('--output',type=Path,required=True,help='New private raw dump or decoded report; never overwritten')
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args();offset,size=partition()
    if args.output.exists():parser.error('output already exists; select a new file')
    if args.action=='read':
        if not args.port or args.core or args.elf:parser.error('read requires --port and accepts no local dump/ELF')
        command=[sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'--no-stub',
            '--before','no-reset','--after','no-reset','read-flash',hex(offset),hex(size),str(args.output.resolve())]
    else:
        if args.port or not args.core or not args.elf:parser.error('decode requires --core and --elf; it cannot open a serial port')
        data=args.core.read_bytes()
        if not 20<=len(data)<=size or not 20<=struct.unpack_from('<I',data)[0]<=len(data):raise ValueError('empty, oversized or truncated raw dump')
        with args.elf.open('rb') as stream:
            if stream.read(4)!=b'\x7fELF':raise ValueError('invalid ELF')
        if args.elf_sha256:
            if hashlib.sha256(args.elf.read_bytes()).hexdigest()!=args.elf_sha256.lower():raise ValueError('ELF SHA256 mismatch')
        elif args.execute:parser.error('decode --execute requires --elf-sha256 to bind the chosen ELF')
        command=[sys.executable,'-m','esp_coredump','--chip','esp32s3','info_corefile',
            '--core',str(args.core.resolve()),'--core-format','raw',str(args.elf.resolve())]
    if not args.execute:
        print(json.dumps({'executed':False,'action':args.action,'command':command,'sensitiveOutput':True},ensure_ascii=False));return
    # Exclusive owner-only creation applies to reports as well as raw stack memory.
    fd=os.open(args.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    try:
        with os.fdopen(fd,'wb') as output:
            if args.action=='decode':subprocess.run(command,check=True,stdout=output,stderr=subprocess.STDOUT,timeout=90)
            else:subprocess.run(command,check=True,stdout=subprocess.DEVNULL,timeout=90)
        if args.action=='read' and args.output.stat().st_size!=size:raise ValueError('incomplete flash read')
    except BaseException:
        args.output.unlink(missing_ok=True);raise
    print(json.dumps({'executed':True,'action':args.action,'output':str(args.output.resolve()),
        'bytes':args.output.stat().st_size,'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()}))

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,subprocess.SubprocessError) as failure:
        print(f'crash dump failed: {type(failure).__name__}; input files were preserved',file=sys.stderr);sys.exit(1)
