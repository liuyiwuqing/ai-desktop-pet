#!/usr/bin/env python3
"""Actual growth engine/formatter -> real MQTT/PostgreSQL/HTTP; no physical NVS or device."""
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
def main():
    components=ROOT/'firmware/components'
    output=ROOT/'firmware/build-growth-sync-host/growth_wire_peer';output.parent.mkdir(parents=True,exist_ok=True)
    flags=['-std=c17','-Wall','-Wextra','-Werror'];env=os.environ.copy()
    if env.get('PET_HOST_SANITIZE')=='1':
        flags+=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'];env.setdefault('ASAN_OPTIONS','detect_leaks=0')
    for path in ('contracts/include','svc_growth/include','svc_growth/private_include'):flags+=['-I',str(components/path)]
    subprocess.run(['cc',*flags,str(components/'svc_growth/src/growth_engine.c'),str(components/'svc_growth/src/growth_sync.c'),
        str(ROOT/'firmware/tests/host/growth_wire_peer.c'),'-o',str(output)],check=True)
    if Path('/usr/libexec/java_home').exists():env['JAVA_HOME']=subprocess.check_output(['/usr/libexec/java_home','-v','21'],text=True).strip()
    env['NEXUS_PET_GROWTH_PEER']=str(output)
    subprocess.run(['mvn','-s','.mvn/settings.xml','-B','-ntp','-Dtest=DeviceGrowthFlowTest#actualFirmwareGrowthSnapshotRoundTripsWithoutChangingEitherRewardRule','test'],cwd=ROOT.parent/'nexus-core',env=env,check=True)
    print('Growth snapshot interop passed; local engine/JSON and real CORE, no physical device')
if __name__=='__main__':
    try:main()
    except (OSError,subprocess.CalledProcessError) as error:
        print('Growth snapshot interop failed: '+type(error).__name__,file=sys.stderr);sys.exit(1)
