#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${root}/firmware/build-host"
json="${root}/firmware/managed_components/espressif__cjson/cJSON"
mkdir -p "${out}"
flags=(-std=c17 -Wall -Wextra -Werror -I "${root}/firmware/components/svc_ota/include"
    -I "${root}/firmware/components/contracts/include" -I "${json}")
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
cc "${flags[@]}" -Wno-deprecated-declarations -c "${json}/cJSON.c" -o "${out}/cjson_ota.o"
cc "${flags[@]}" "${root}/firmware/components/svc_ota/src/manifest.c" \
    "${root}/firmware/tests/host/test_ota_manifest.c" "${out}/cjson_ota.o" -lm -o "${out}/test_ota_manifest"
cc "${flags[@]}" "${root}/firmware/components/svc_ota/src/transfer.c" \
    "${root}/firmware/tests/host/test_ota_transfer.c" -o "${out}/test_ota_transfer"
"${out}/test_ota_transfer"
python3 - "${out}" <<'PY'
import base64,json,pathlib,sys,subprocess,tempfile
payload=dict(format=1,project='nexus_pet',board='dodo-s3-lcd',version='0.4.0',modelSha256='none',partitionSha256='a'*64,imageSha256='c'*64,image='firmware.bin',imageSize=1024,releaseSequence=10,configSchema=1,growthSchema=4)
envelope=dict(payload=base64.b64encode(json.dumps(payload,separators=(',',':')).encode()).decode(),signature=base64.b64encode(bytes([1])+bytes(383)).decode())
with tempfile.TemporaryDirectory(prefix='pet-ota-') as directory:
    file=pathlib.Path(directory)/'manifest.json';file.write_text(json.dumps(envelope))
    subprocess.run([str(pathlib.Path(sys.argv[1])/'test_ota_manifest'),str(file)],check=True)
PY
python3 "${root}/tools/ci/test_release.py"
