#!/usr/bin/env python3
"""Actual C transaction -> MQTT -> PostgreSQL -> HTTP, with no real device/production state."""
import os
import sys
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    c=ROOT/'firmware/components';out=ROOT/'firmware/build-companion-host';out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c17','-Wall','-Wextra','-Werror','-pthread']
    env=os.environ.copy()
    if env.get('PET_HOST_SANITIZE')=='1':
        flags+=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'];env.setdefault('ASAN_OPTIONS','detect_leaks=0')
    for path in ('contracts/include','core/include','storage/include','svc_growth/include','svc_growth/private_include'):
        flags+=['-I',str(c/path)]
    flags+=['-I',str(ROOT/'firmware/tests/host/support'),'-I',str(ROOT/'firmware/managed_components/espressif__cjson/cJSON')]
    subprocess.run(['cc',*flags,'-Wno-deprecated-declarations','-c',str(ROOT/'firmware/managed_components/espressif__cjson/cJSON/cJSON.c'),'-o',str(out/'cjson.o')],check=True)
    sources=[str(c/'svc_growth/src'/name) for name in ('growth_engine.c','companion_model.c','growth_store.c','growth_service.c','growth_environment.c','companion_sync.c')]
    peer=out/'companion_wire_peer'
    subprocess.run(['cc',*flags,*sources,str(ROOT/'firmware/tests/host/companion_wire_peer.c'),str(out/'cjson.o'),'-lm','-o',str(peer)],check=True)
    if '--build-only' in sys.argv:
        print(peer);return
    env['NEXUS_COMPANION_PEER']=str(peer)
    env['MAVEN_ARGS']='-Dtest=CompanionFlowTest'
    subprocess.run(['python3','tools/run_local.py','test'],cwd=ROOT.parent/'nexus-core',env=env,check=True)
    print('Companion contract: real C persistence, MQTT, PostgreSQL and HTTP passed; no physical NVS/device')
if __name__=='__main__':main()
