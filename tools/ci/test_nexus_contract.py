#!/usr/bin/env python3
"""Local source-contract round trip. No network, SQL, flashing, or nexus-core writes.

Compiles current DeliveryWorker/DeliveryService/DeviceText with the server's
packaged dependency jars. SQL and transport boundaries are explicit test doubles.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import schema_codegen as codegen  # noqa: E402


def run(*args: object) -> None:
    subprocess.run([str(arg) for arg in args], check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nexus-root", type=Path, default=ROOT.parent / "nexus-core")
    parser.add_argument("--runtime-jar", type=Path)
    args = parser.parse_args()
    nexus = args.nexus_root.resolve()
    contract = ROOT / "integration/contracts/nexus-v1"
    manifest = json.loads((contract / "source-manifest.json").read_text())
    for name, expected in manifest["sources"].items():
        actual = hashlib.sha256((nexus / name).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"nexus source changed: {name}; review/version the contract first")
    jars = [args.runtime_jar] if args.runtime_jar else list((nexus / "target").glob("nexus-core-*.jar"))
    if len(jars) != 1 or not jars[0].is_file():
        raise RuntimeError("provide --runtime-jar pointing to an already-built nexus-core Spring Boot jar")
    java_home = os.environ.get("JAVA_HOME")
    if not java_home and sys.platform == "darwin":
        java_home = subprocess.check_output(["/usr/libexec/java_home", "-v", "21"], text=True).strip()
    java = str(Path(java_home) / "bin/java") if java_home else shutil.which("java")
    javac = str(Path(java_home) / "bin/javac") if java_home else shutil.which("javac")
    if not java or not javac:
        raise RuntimeError("JDK 21 is required")
    build = ROOT / "firmware/build-nexus-contract"
    libs = build / "lib"
    classes = build / "classes"
    libs.mkdir(parents=True, exist_ok=True)
    classes.mkdir(parents=True, exist_ok=True)
    runtime = []
    with zipfile.ZipFile(jars[0]) as archive:
        for name in archive.namelist():
            if name.startswith("BOOT-INF/lib/") and name.endswith(".jar"):
                path = libs / Path(name).name
                path.write_bytes(archive.read(name))
                runtime.append(str(path))
    if not runtime:
        raise RuntimeError("runtime jar has no BOOT-INF/lib dependencies")
    classpath = os.pathsep.join(runtime)
    source = nexus / "src/main/java/dev/nexus/core"
    run(javac, "--release", "21", "-cp", classpath, "-d", classes,
        source / "delivery/DeliveryWorker.java", source / "delivery/DeviceText.java",
        source / "delivery/DeliveryService.java", source / "web/ApiRows.java",
        *sorted((ROOT / "integration/tests/nexus-v1").glob("*.java")))
    classpath = str(classes) + os.pathsep + classpath
    envelope = build / "server-envelope.json"
    packets = build / "firmware-packets.jsonl"
    run(java, "-cp", classpath, "dev.nexus.core.delivery.ContractProbe", "emit", envelope)
    includes = ["tests/host/support", "components/contracts/include", "components/core/include",
                "components/storage/include", "components/svc_message_hub/include",
                "components/svc_message_hub/private_include", "components/svc_net/private_include"]
    cc_args = [arg for path in includes for arg in ("-I", ROOT / "firmware" / path)]
    probe = build / "firmware-probe"
    run("cc", "-std=c17", "-Wall", "-Wextra", "-Werror",
        f'-DTEST_FIXTURE_DIR="{ROOT / "firmware/tests/host/fixtures"}"', *cc_args,
        *sorted((ROOT / "firmware/components/svc_message_hub/src").glob("*.c")),
        ROOT / "firmware/components/storage/src/storage.c",
        ROOT / "firmware/components/svc_net/src/net_policy.c",
        ROOT / "firmware/tests/host/test_message_service.c", "-o", probe)
    run(probe, "wire-fixture", envelope, packets)
    schemas = {schema["title"]: schema for schema in codegen.load_schemas().values()}
    for packet, title in [(json.loads(envelope.read_text()), "MessageEnvelope")] + [
        (json.loads(line), "DeviceState" if i >= 5 else "MessageRejection" if i == 4 else "MessageAck")
        for i, line in enumerate(packets.read_text().splitlines())
    ]:
        errors = codegen.validate_instance(packet, schemas[title])
        if errors:
            raise RuntimeError("; ".join(errors))
    run(java, "-cp", classpath, "dev.nexus.core.delivery.ContractProbe", "verify", packets)
    print("PASS: Java source -> firmware persistence/ACK -> Java source; schemas match")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"FAIL: {exc.cmd[0]} exited {exc.returncode}; see compiler/test output above", file=sys.stderr)
        sys.exit(1)
    except (OSError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
