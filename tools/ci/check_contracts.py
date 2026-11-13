#!/usr/bin/env python3
"""Validate schemas, profiles, examples, extension manifests and generated code."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import schema_codegen as codegen  # noqa: E402

CONFIG_DIR = ROOT / "config"
EXTENSIONS_DIR = ROOT / "extensions"
EXT_CONTRACT_HEADER = ROOT / "firmware" / "components" / "contracts" / "include" / "pet" / "extension.h"
KCONFIG = ROOT / "firmware" / "main" / "Kconfig.projbuild"

EXAMPLE_SCHEMAS = {
    "device-config.example.json": "DeviceConfig",
    "message.example.json": "MessageEnvelope",
    "message-ack.example.json": "MessageAck",
    "message-rejection.example.json": "MessageRejection",
    "device-state.example.json": "DeviceState",
    "telemetry.example.json": "PetTelemetry",
    "config-request.example.json": "PetConfigRequest",
    "config-result.example.json": "PetConfigResult",
    "ota-command.example.json": "PetOtaCommand",
    "ota-ack.example.json": "PetOtaAck",
    "ota-status.example.json": "PetOtaStatus",
    "growth-status.example.json": "PetGrowthStatus",
}


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise codegen.SchemaError(f"{path.relative_to(ROOT)}: invalid JSON: {exc}") from exc


def schemas_by_title() -> dict[str, dict]:
    return {data["title"]: data for data in codegen.load_schemas().values()}


def validate_file(path: Path, schema: dict, title: str) -> list[str]:
    instance = load_json(path)
    return [
        f"{path.relative_to(ROOT)}: {message}"
        for message in codegen.validate_instance(instance, schema, title)
    ]


def check_profiles(schemas: dict[str, dict]) -> list[str]:
    errors: list[str] = []
    profile_schema = schemas["DeviceProfile"]
    profiles_dir = CONFIG_DIR / "profiles"
    profiles = sorted(profiles_dir.glob("*.json"))
    if not profiles:
        errors.append("config/profiles: no profiles found")
    for path in profiles:
        errors.extend(validate_file(path, profile_schema, "DeviceProfile"))
        instance = load_json(path)
        board_id = instance.get("boardId")
        if isinstance(board_id, str):
            board_dir = ROOT / "firmware" / "boards" / board_id
            if not board_dir.is_dir():
                errors.append(
                    f"{path.relative_to(ROOT)}: boardId '{board_id}' has no firmware/boards/{board_id}"
                )
    return errors


def check_examples(schemas: dict[str, dict]) -> list[str]:
    errors: list[str] = []
    examples_dir = CONFIG_DIR / "examples"
    for filename, title in EXAMPLE_SCHEMAS.items():
        path = examples_dir / filename
        if not path.is_file():
            errors.append(f"missing example: config/examples/{filename}")
            continue
        errors.extend(validate_file(path, schemas[title], title))
    return errors


def check_nexus_snapshots() -> list[str]:
    """线上契约由版本化快照固定，config 镜像和可读示例不得各改各的。"""
    errors: list[str] = []
    versioned = ROOT / "integration" / "contracts" / "nexus-v1"
    for path in sorted(versioned.glob("*.json")):
        if path.name == "source-manifest.json":
            continue
        kind = "schemas" if path.name.endswith(".schema.json") else "examples"
        mirror = CONFIG_DIR / kind / path.name
        if not mirror.is_file() or mirror.read_bytes() != path.read_bytes():
            errors.append(f"{mirror.relative_to(ROOT)}: differs from nexus-v1 snapshot")
    packet = load_json(versioned / "message.example.json")
    if len((versioned / "message.example.json").read_bytes()) > 1024:
        errors.append("nexus-v1 message example exceeds 1024 UTF-8 bytes")
    for field, maximum in (("title", 96), ("summary", 180)):
        if len(packet["payload"][field].encode("utf-8")) > maximum:
            errors.append(f"nexus-v1 payload.{field} exceeds {maximum} UTF-8 bytes")
    if packet["expiresAt"] <= packet["sentAt"]:
        errors.append("nexus-v1 expiresAt must be later than sentAt")
    return errors


def read_contract_version() -> int:
    text = EXT_CONTRACT_HEADER.read_text(encoding="utf-8")
    match = re.search(r"#define\s+PET_EXT_CONTRACT_VERSION\s+(\d+)", text)
    if match is None:
        raise codegen.SchemaError(f"{EXT_CONTRACT_HEADER.relative_to(ROOT)}: contract version not found")
    return int(match.group(1))


def check_extensions(schemas: dict[str, dict]) -> list[str]:
    errors: list[str] = []
    manifest_schema = schemas["ExtensionManifest"]
    contract_version = read_contract_version()
    kconfig_text = KCONFIG.read_text(encoding="utf-8")

    if not EXTENSIONS_DIR.is_dir():
        return ["missing extensions directory"]

    extension_dirs = sorted(path for path in EXTENSIONS_DIR.iterdir() if path.is_dir())
    if not extension_dirs:
        return ["extensions: no extension packages found"]

    for ext_dir in extension_dirs:
        manifest_path = ext_dir / "extension.json"
        if not manifest_path.is_file():
            errors.append(f"{ext_dir.relative_to(ROOT)}: missing extension.json")
            continue
        errors.extend(validate_file(manifest_path, manifest_schema, "ExtensionManifest"))

        manifest = load_json(manifest_path)
        extension_id = manifest.get("id")
        if extension_id != ext_dir.name:
            errors.append(
                f"{manifest_path.relative_to(ROOT)}: id '{extension_id}' != directory '{ext_dir.name}'"
            )
            continue
        if manifest.get("contractVersion") != contract_version:
            errors.append(
                f"{manifest_path.relative_to(ROOT)}: contractVersion "
                f"{manifest.get('contractVersion')} != core {contract_version}"
            )

        required_files = [
            ext_dir / "CMakeLists.txt",
            ext_dir / "README.md",
            ext_dir / "include" / "pet_ext" / f"{extension_id}.h",
            ext_dir / "src" / f"{extension_id}.c",
            ext_dir / "test" / "host" / f"test_{extension_id}_contract.c",
        ]
        for path in required_files:
            if not path.is_file():
                errors.append(f"{ext_dir.relative_to(ROOT)}: missing {path.relative_to(ext_dir)}")

        switch = f"PET_EXT_{extension_id.upper()}"
        if switch not in kconfig_text:
            errors.append(f"{manifest_path.relative_to(ROOT)}: missing Kconfig switch {switch}")

    return errors


def main() -> int:
    try:
        schemas = schemas_by_title()
        errors = check_profiles(schemas) + check_examples(schemas) + check_extensions(schemas) + check_nexus_snapshots()
    except codegen.SchemaError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    if errors:
        for error in errors:
            print(f"FAIL {error}", file=sys.stderr)
        return 1

    if codegen.check_all() != 0:
        return 1

    profiles = len(list((CONFIG_DIR / "profiles").glob("*.json")))
    examples = len(EXAMPLE_SCHEMAS)
    extensions = len([path for path in EXTENSIONS_DIR.iterdir() if path.is_dir()])
    print(
        f"OK: {len(schemas)} schemas, {profiles} profiles, {examples} examples, "
        f"{extensions} extensions validated"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
