#!/usr/bin/env python3
"""Generate the compile-time board/model/partition identity from build inputs."""
import argparse
import hashlib
from pathlib import Path
import re

def main():
    p=argparse.ArgumentParser();p.add_argument('--model-header',type=Path);p.add_argument('--partition-table',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();model='none'
    if args.model_header:
        match=re.search(r'pet_sr_model_sha256\[32\]\s*=\s*\{([^}]+)',args.model_header.read_text())
        if not match:raise ValueError('missing checked model digest')
        model=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',match[1])).hex()
        if len(model)!=64:raise ValueError('invalid model digest')
    partitions=hashlib.sha256(args.partition_table.read_bytes()).hexdigest()
    source=f'#pragma once\n#define PET_ID_MODEL "{model}"\n#define PET_ID_PARTITIONS "{partitions}"\n#define PET_ID_MARKER "PET04_ID:nexus_pet:dodo-s3-lcd:{model}:{partitions}:END"\n'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    if not args.output.exists() or args.output.read_text()!=source:args.output.write_text(source)
if __name__=='__main__':main()
