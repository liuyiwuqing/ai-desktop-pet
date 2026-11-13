#!/usr/bin/env python3
"""Actual HTTP/PostgreSQL/MQTT -> firmware remote OTA parser/journal -> status reconciliation.

Flash, image validation and restart are explicit hosted boundaries, not hardware evidence.
"""
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
def main():
    components=ROOT/'firmware/components';cjson=ROOT/'firmware/managed_components/espressif__cjson/cJSON'
    output=ROOT/'firmware/build-ota-remote-host/ota_wire_peer';output.parent.mkdir(parents=True,exist_ok=True)
    flags=['-std=c17','-Wall','-Wextra','-Werror'];env=os.environ.copy()
    if env.get('PET_HOST_SANITIZE')=='1':
        flags+=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'];env.setdefault('ASAN_OPTIONS','detect_leaks=0')
    for path in (ROOT/'firmware/tests/host/support',components/'contracts/include',components/'svc_ota/include',cjson):flags+=['-I',str(path)]
    subprocess.run(['cc',*flags,'-Wno-deprecated-declarations',str(cjson/'cJSON.c'),str(components/'svc_ota/src/remote.c'),
        str(ROOT/'firmware/tests/host/ota_wire_peer.c'),'-lm','-o',str(output)],check=True)
    if Path('/usr/libexec/java_home').exists():env['JAVA_HOME']=subprocess.check_output(['/usr/libexec/java_home','-v','21'],text=True).strip()
    env['NEXUS_PET_OTA_PEER']=str(output)
    subprocess.run(['mvn','-s','.mvn/settings.xml','-B','-ntp','-Dtest=RemoteOtaFlowTest#realFirmwareTransactionRoundTripsInstallAndReboot','test'],cwd=ROOT.parent/'nexus-core',env=env,check=True)
    print('Remote OTA interop passed; OS/Flash/SDK boundaries hosted, no physical device')
if __name__=='__main__':
    try:main()
    except (OSError,subprocess.CalledProcessError) as error:
        print('Remote OTA interop failed: '+type(error).__name__,file=sys.stderr);sys.exit(1)
