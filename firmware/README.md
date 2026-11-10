# firmware

ESP-IDF 6.1 固件工程。S2 组件化与 S3 契约/配置已落地：`main` 只做组合根，契约、事件总线、业务模块与扩展拆为独立组件。

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

或使用 CI 入口：`../tools/ci/build_firmware.sh`。当前构建结果：0 warning，默认镜像 181,936 B，DIRAM 55,018 B（16.1%）。只构建，不烧录、不擦除 Flash。

## 校验

```bash
python3 ../tools/ci/check_deps.py       # 组件依赖图无环
python3 ../tools/ci/check_contracts.py  # schema、profile、example、扩展 manifest、生成物一致性
../tools/ci/test_host.sh                # 主机测试（事件契约、生成配置头、扩展清单）
```

`build_firmware.sh` 会依次执行依赖检查、契约校验、主机测试、`idf.py build` 与 `idf.py size`。

## 扩展开关

两个扩展默认关闭，关闭时二进制不含 `pet_ext_*_init`：

```bash
# 启用示例：复制 sdkconfig 后追加，再用独立 build 目录构建
cp sdkconfig /tmp/pet-sdkconfig.ext
printf '\nCONFIG_PET_EXT_DEVICE_CONTROL=y\nCONFIG_PET_EXT_SERVO=y\n' >> /tmp/pet-sdkconfig.ext
idf.py -B build-ext -D SDKCONFIG=/tmp/pet-sdkconfig.ext build
nm build-ext/nexus_pet.elf | grep pet_ext_
```

## 布局

- `main/`：组合根，只含 `main.c`、`app/` 启动编排与 `Kconfig.projbuild`。
- `components/contracts/`：跨模块契约（`include/pet/`），纯 C、不依赖 ESP-IDF，可主机测试；`config_generated.h`、`extension.h` 均由 S3 契约生成或固化。
- `components/core/`：事件总线（4 条优先级队列，总深度 32），`REQUIRES contracts`。
- `components/svc_growth/`：成长模块骨架，受 `CONFIG_PET_MODULE_GROWTH_ENABLED` 控制。
- `../extensions/`：扩展组件，受 `CONFIG_PET_EXT_*` 控制；`EXTRA_COMPONENT_DIRS` 收集。
- `boards/dodo-s3-lcd/`：板级 `board.json`（引脚/能力唯一事实源）。
- `partitions/`：新分区表与迁移说明，不复用 02/03 的分区表。
- `tests/host/`：主机测试。

每个组件只暴露 `include/`，`src/` 私有；依赖通过 `REQUIRES`/`PRIV_REQUIRES` 显式声明。`hal`、`bsp_*`、`storage`、`presentation`、`ui_lvgl` 随首个真实实现创建，不建空壳。

## 分区

`partitions/16m.csv`：`ota_0`/`ota_1` 各 4 MB，`storage` 2 MB，`assets` 4 MB，`model` 1 MB，剩余 832 KB 保留。

## 边界

- 不在本目录复制 02/03 的整仓源码；先抽取组件再引用。
- 不手改生成的 `sdkconfig`、`build/`、`managed_components/`。
- 构建与烧录分离；默认只构建，不烧录、不擦除 Flash。
- 详细方案见 [`../docs/00-architecture.md`](../docs/00-architecture.md)。
