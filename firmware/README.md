# firmware

ESP-IDF 6.1 固件工程。S1 可构建骨架已落地：CMake、sdkconfig、16 MB 分区表、最小事件总线与启动诊断。

## 构建

```bash
export IDF_PATH=~/esp/v6.1/esp-idf
export IDF_PYTHON_ENV_PATH=~/.espressif/tools/python/v6.1/venv   # 若 export.sh 报 python env 不存在
source "$IDF_PATH/export.sh"
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py size
```

或使用 CI 入口：`../tools/ci/build_firmware.sh`。

当前构建结果：0 warning，镜像约 181 KB。只构建，不烧录、不擦除 Flash。

## 布局

- `main/app/`：启动、任务编排、事件分发、看门狗。
- `main/core/`：事件总线、模块生命周期、配置、契约。
- `main/modules/`：业务模块；每个模块只暴露 `include/` 契约，不互相直接调用。
- `main/hal/`：I2S、SPI、GPIO、I2C、RMT、Wi-Fi。
- `main/ui/`：LVGL 页面、组件、主题。
- `main/storage/`：NVS、LittleFS、版本化存档。
- `boards/dodo-s3-lcd/`：板级 `board.json`（引脚/能力唯一事实源）。
- `components/`：可复用 ESP-IDF 组件。
- `partitions/`：新分区表与迁移说明，不复用 02/03 的分区表。
- `tests/`：主机测试与目标测试入口。

## 分区

`partitions/16m.csv`：`ota_0`/`ota_1` 各 4 MB，`storage` 2 MB，`assets` 4 MB，`model` 1 MB，剩余 832 KB 保留。

## 边界

- 不在本目录复制 02/03 的整仓源码；先抽取组件再引用。
- 不手改生成的 `sdkconfig`、`build/`、`managed_components/`。
- 构建与烧录分离；默认只构建，不烧录、不擦除 Flash。
- 详细方案见 [`../docs/00-architecture.md`](../docs/00-architecture.md)。
