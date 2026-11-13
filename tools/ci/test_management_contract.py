#!/usr/bin/env python3
"""Real Java/HTTP/PostgreSQL/MQTT -> firmware C config service -> Java acknowledgment."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
NEXUS = ROOT.parent / "nexus-core"

def main():
    component = ROOT / "firmware/components"
    cjson = ROOT / "firmware/managed_components/espressif__cjson/cJSON"
    output = ROOT / "firmware/build-host/config_wire_peer"
    output.parent.mkdir(exist_ok=True, parents=True)
    flags = ["-std=c17", "-Wall", "-Wextra", "-Werror", "-pthread"]
    env = os.environ.copy()
    if env.get("PET_HOST_SANITIZE") == "1":
        flags += ["-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    for path in [ROOT / "firmware/tests/host/support", component / "contracts/include", component / "storage/include", component / "svc_config/include", cjson]:
        flags += ["-I", str(path)]
    subprocess.run(["cc", *flags, "-Wno-deprecated-declarations", str(cjson / "cJSON.c"),
        *(str(component / "svc_config/src" / name) for name in ("config_service.c", "config_codec.c", "config_remote.c")),
        str(ROOT / "firmware/tests/host/config_wire_peer.c"), "-lm", "-o", str(output)], check=True)
    if Path("/usr/libexec/java_home").exists():
        env["JAVA_HOME"] = subprocess.check_output(["/usr/libexec/java_home", "-v", "21"], text=True).strip()
    env["NEXUS_PET_CONFIG_PEER"] = str(output)
    subprocess.run(["mvn", "-s", ".mvn/settings.xml", "-B", "-ntp",
        "-Dtest=DeviceManagementFlowTest#configurationEnvelopeRoundTripsThroughRealFirmwareService", "test"], cwd=NEXUS, env=env, check=True)
    print("Management interoperability passed; flash/OS boundaries are hosted, no physical ESP32 used")

if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Management interoperability failed: {type(error).__name__}", file=sys.stderr)
        sys.exit(1)
