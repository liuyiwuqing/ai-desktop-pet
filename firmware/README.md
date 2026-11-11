# firmware

ESP-IDF 6.1 固件工程。S2 组件化、S3 契约/配置、S4 温湿度纵向切片、S5-A 消息中心纯逻辑与 S5-B Wi-Fi/MQTT/TLS 传输层已落地：`main` 只做组合根，契约、事件总线、业务模块、板级实现与扩展拆为独立组件。

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

或使用 CI 入口：`../tools/ci/build_firmware.sh`。当前构建结果：0 编译 warning，默认镜像 1,023,456 B，DIRAM 126,154 B（36.91%）；fake 网络镜像 245,072 B；关闭网络模块 242,056 B。只构建，不烧录、不擦除 Flash。

## 校验

```bash
python3 ../tools/ci/check_deps.py       # 组件依赖图无环
python3 ../tools/ci/check_contracts.py  # schema、profile、example、扩展 manifest、生成物一致性
../tools/ci/test_host.sh                # 主机测试（事件契约、生成配置头、扩展清单、温湿度、消息时间/校验/队列、网络策略）
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

## 气候模块开关

温湿度模块默认启用，使用 DHT11（GPIO15，5 s 采样）。无硬件时用 fake 传感器验证链路：

```bash
# 桌面验证：固定温湿度序列，不需要 DHT11
cp sdkconfig /tmp/pet-sdkconfig.fake
printf '\nCONFIG_PET_CLIMATE_FAKE_SENSOR=y\n' >> /tmp/pet-sdkconfig.fake
idf.py -B build-fake -D SDKCONFIG=/tmp/pet-sdkconfig.fake build
xtensa-esp32s3-elf-nm build-fake/nexus_pet.elf | grep fake_climate
```

关闭 `CONFIG_PET_MODULE_CLIMATE_ENABLED` 可验证组合根不依赖该模块：构建通过且 ELF 中无 climate 符号。首版 UI 出口为串口卡片日志，`presentation` 只产出纯文本快照。

## 网络模块与凭据

网络模块默认启用（Wi-Fi station + MQTT 3.1.1 over TLS 8883、QoS 1、`clean_session=false`、keepalive 60 s、LWT、自动重连）。凭据优先从 NVS 字符串读取，Kconfig 中的 `PET_NET_*` 仅作开发期回退：

| 命名空间 | 键 | 说明 |
| --- | --- | --- |
| `net` | `wifi_ssid` / `wifi_pass` | Wi-Fi |
| `nexus` | `device_id` | MQTT clientId 与主题设备 ID |
| `nexus` | `mqtt_uri` | 仅接受 `mqtts://`（8883） |
| `nexus` | `mqtt_user` / `mqtt_pass` | Broker 凭据 |
| `nexus` | `mqtt_ca` | 可选 PEM CA；缺省使用证书包 |

未配置 SSID 或 Broker URI 时只输出告警并跳过联网，不阻断启动。

无硬件 / 无 Broker 的桌面验证：

```bash
cp sdkconfig /tmp/pet-sdkconfig.netfake
printf '\nCONFIG_PET_NET_FAKE=y\n' >> /tmp/pet-sdkconfig.netfake
idf.py -B build-net-fake -D SDKCONFIG=/tmp/pet-sdkconfig.netfake build
xtensa-esp32s3-elf-nm build-net-fake/nexus_pet.elf | grep fake_net
```

关闭 `CONFIG_PET_MODULE_NET_ENABLED` 可验证组合根不依赖该模块：构建通过且 ELF 中无 `pet_net_*` 符号。

## 布局

- `main/`：组合根，只含 `main.c`、`app/` 启动编排与 `Kconfig.projbuild`。
- `components/contracts/`：跨模块契约（`include/pet/`），纯 C、不依赖 ESP-IDF，可主机测试；`config_generated.h`、`extension.h` 均由 S3 契约生成或固化，`climate.h` 为温湿度契约，`message.h` 为消息契约，`net.h` 为网络契约。
- `components/core/`：事件总线（4 条优先级队列，总深度 32），`REQUIRES contracts`。
- `components/pet_hal/`：HAL 接口与纯逻辑（`climate_sensor.h`、`climate_decode`、fake 传感器；`net.h` 网络驱动接口）。
- `components/bsp_dodo_s3_lcd/`：DODO-S3-LCD 板级实现，当前含 RMT 读取 DHT11。
- `components/storage/`：NVS 薄封装 `pet_storage_*`（blob 与字符串）。
- `components/svc_climate/`：温湿度采样任务、滤波、告警、60 分钟缓冲与当日极值，受 `CONFIG_PET_MODULE_CLIMATE_ENABLED` 控制。
- `components/svc_message_hub/`：消息 ISO-8601 解析、信封校验、8 条优先级队列、16 槽去重、TTL/DND/ACK 状态机；传输与 NVS 持久化在 S5-B/C 接入。
- `components/svc_net/`：网络策略（设备 ID/配置校验/主题构建）、服务生命周期与 ESP/fake 驱动，受 `CONFIG_PET_MODULE_NET_ENABLED` 与 `CONFIG_PET_NET_FAKE` 控制。
- `components/svc_growth/`：成长模块骨架，受 `CONFIG_PET_MODULE_GROWTH_ENABLED` 控制。
- `components/presentation/`：纯逻辑表现快照，当前含温湿度卡片。
- `../extensions/`：扩展组件，受 `CONFIG_PET_EXT_*` 控制；`EXTRA_COMPONENT_DIRS` 收集。
- `boards/dodo-s3-lcd/`：板级 `board.json`（引脚/能力唯一事实源）。
- `partitions/`：新分区表与迁移说明，不复用 02/03 的分区表。
- `tests/host/`：主机测试。

每个组件只暴露 `include/`，`src/` 私有；依赖通过 `REQUIRES`/`PRIV_REQUIRES` 显式声明。`ui_lvgl` 与其余 HAL 接口随对应切片创建，不建空壳。

## 分区

`partitions/16m.csv`：`ota_0`/`ota_1` 各 4 MB，`storage` 2 MB，`assets` 4 MB，`model` 1 MB，剩余 832 KB 保留。

## 边界

- 不在本目录复制 02/03 的整仓源码；先抽取组件再引用。
- 不手改生成的 `sdkconfig`、`build/`、`managed_components/`。
- 构建与烧录分离；默认只构建，不烧录、不擦除 Flash。
- 详细方案见 [`../docs/00-architecture.md`](../docs/00-architecture.md)。
