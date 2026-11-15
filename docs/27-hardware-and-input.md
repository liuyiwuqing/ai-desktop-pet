# 27 · 发行硬件、扩展与输入

更新：2026-11-15。本文为当前硬件及接线入口；旧切片的构建和接线记录仍属于各自时点。内部板型 ID 保持 `dodo-s3-lcd`，不改变 OTA 身份、分区或存档。

## 发行基线

必需硬件：ESP32-S3-DevKitC-1 N16R8（16 MB Flash / 8 MB Octal PSRAM）、240×320 SPI ST7789、INMP441、MAX98357A 与无源喇叭。使用板载 BOOT 按键即可操作，不要求另购按键或任何扩展。硬件包含音频不代表开机自动录音；语音启动仍按 [01](01-ai-agent.md) 的凭据与显式开关执行。

温湿度、二维摇杆（KY-023 类）、光敏和舵机均为可选扩展。模块是否安装不能由浮空 ADC 可靠识别，采用显式配置；未启用时不采样、不驱动。基础版不显示未启用的温湿度菜单，也不将缺少模块报为故障。启用后断线仍按故障处理，不能伪造有效读数。

## 基础接线

下列数字均为 GPIO 编号。断电接线，共地；电源脚按模块丝印确认，不能按不同厂商排针顺序猜测。

| 模块 | 模块端 | 开发板端 |
| --- | --- | --- |
| ST7789 | VCC / GND | 3V3 / GND（确认模块额定电压） |
| ST7789 | SCLK / SDA(MOSI) | GPIO12 / GPIO11 |
| ST7789 | CS / DC / RESET | GPIO10 / GPIO8 / GPIO9 |
| ST7789 | BLK | 有源背光驱动输入接 GPIO42，见下文 |
| INMP441 | VDD / GND / L/R | 3V3 / GND / GND（左声道） |
| INMP441 | SCK / WS / SD | GPIO16 / GPIO17 / GPIO18 |
| MAX98357A | VIN / GND | 5V / GND |
| MAX98357A | BCLK / LRC / DIN | GPIO21 / **GPIO40** / GPIO39 |
| MAX98357A | SD_MODE / GAIN | 标准模块可将 SD_MODE 接 3V3；GAIN 悬空，核对模块电路 |
| 喇叭 | 两根线 | 仅接功放 SPK+ / SPK−，不接 GND |
| BOOT | 板载按键 | GPIO0，无需额外接线 |

功放 LRC 由旧 GPIO38 改为 GPIO40，避开 DevKitC-1 v1.1 的 RGB 灯；初版的 GPIO48 也保留。**旧接线必须同步更改，旧固件不能直接使用新线序。** 依据：[乐鑫板卡说明](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)。GPIO19/20 留给原生 USB，GPIO43/44 留给 UART，Flash/PSRAM 与其他启动绑带脚不分配扩展。

GPIO42 提供高电平有效的 5 kHz PWM **控制信号**。若屏幕 BLK 是裸背光电源，必须使用适配该模块的驱动电路；不能由 GPIO 直接供电。仅沿用原屏幕 BLK→3V3 常亮接法时，软件调光和熄屏均不会控制实际背光，不作为完整显示验收。

INMP441 仅使用 3.3V。功放从 5V 取电；扬声器按实际阻抗和额定功率选配。先用可靠 5V 电源进行低音量验证，不将外部电源与 USB 电源直接并联。模块电气依据：[INMP441](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf)、[MAX98357A](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)。

## 当前扩展启用

DHT11 / KY-015：VCC→3V3，GND→GND，DATA→GPIO15，DATA 用 10kΩ 上拉到 3V3（模块已有则不重复加）。显式 `CONFIG_PET_MODULE_CLIMATE_ENABLED=y` 后启用原 5 秒采样、图表、告警和成长联动；默认关闭。驱动及故障恢复见 [03](03-climate-sensor.md)。

舵机：默认 `CONFIG_PET_EXT_SERVO=n`；启用后还需配置独立的空闲 PWM 引脚、校准限位和安全角。`PET_SERVO_GPIO=-1` 不启动驱动，未 arm 不输出动作；独立电源与急停见 [06](06-extension-servo.md)。不能将舵机电源接开发板 3V3。

二维摇杆（KY-023 类）：VCC→3V3、GND→GND、VRx→GPIO4、VRy→GPIO5、SW→GPIO6。两轴使用 ADC1，避免与 Wi-Fi 共用 ADC2；输入不得超过 3.3V。显式 `CONFIG_PET_EXT_JOYSTICK=y` 启用，默认关闭；方向与安装朝向相反时使用 `PET_JOYSTICK_INVERT_X` / `PET_JOYSTICK_INVERT_Y`。板型 profile 的可选字段 `features.joystick` 默认 false，旧 profile 不带此字段仍有效。它描述能力默认值，实际驱动开关由 sdkconfig 决定。

| 摇杆操作 | 行为 |
| --- | --- |
| 主页右推 | 打开菜单 |
| 菜单上 / 下 | 上一项 / 下一项，跳过未启用扩展 |
| 菜单右推 | 执行所选操作；说话项仍需按住中键或 BOOT |
| 左推 | 返回；语音页取消当前会话；温湿度页返回 |
| 中键 | 复用 BOOT 菜单手势，按住说话、松开发送 |

BOOT 始终保留。消息已读、外部控制确认和恢复重启继续要求 BOOT；摇杆方向不合成物理确认。开机、采样失败或弹窗抢占后，需要回中且松开 100 ms 才重新接受摇杆；方向消抖 30 ms，上下按住 400 ms 后每 160 ms 连发，左右不连发。采样错误使当前摇杆手势失效，BOOT 可继续操作；初始化失败则本次启动只使用 BOOT。不支持通过浮空电压推断模块热插拔，接线后显式启用并重启。

光敏模块：使用 3.3V 模拟输出模块（KY-018 类或带 AO 的光敏板），VCC→3V3、GND→GND、AO/S→GPIO7（ADC1）。DO 不作为模拟输入，不能把 5V 输出直接接 ADC。`CONFIG_PET_EXT_LIGHT_SENSOR=y` 启用相对亮度调光；如果光越亮电压越低，启用 `CONFIG_PET_LIGHT_SENSOR_INVERT=y`。默认关闭，板型 profile 的可选 `features.lightSensor` 缺省 false。

正常 UI 刷新时以 500 ms 为最小间隔采样，与摇杆共享一个 ADC1 实例；实际周期受刷新调度影响。使用 EMA 和每步最多 5 个百分点的渐变，目标为 15%–100%；手动 `brightness` 始终是上限，低于 15% 时以上限为准，0 始终熄屏。读数错误或无适配器时回退手动值；不写入 NVS，不将相对 ADC 数值声称为 lux。恢复 UI 不采样光敏。使用此功能必须先确认 GPIO42 接的是高电平有效的背光驱动，BLK 固定供电的屏幕无法调光。

舵机建议将 GPIO2 留作 PWM 扩展位（未启用时不输出），与摇杆和光敏不冲突。仍须按具体型号配置供电、角度和速度；`PET_SERVO_GPIO=-1` 与未 arm 的保护保持不变，不因接线而自动启动。

## 选择扩展并构建

| 配置 | 默认值 | 启用效果 |
| --- | --- | --- |
| `PET_MODULE_CLIMATE_ENABLED` | n | GPIO15 的 DHT11 采集、菜单与环境联动 |
| `PET_EXT_JOYSTICK` | n | GPIO4/5/6 的二维摇杆菜单输入 |
| `PET_EXT_LIGHT_SENSOR` | n | GPIO7 的光敏采样与背光调节 |
| `PET_EXT_SERVO` | n | 编译舵机；另需 GPIO、限位与 arm |

激活 ESP-IDF 6.1 后，在仓库根目录用独立配置打开菜单：

```sh
idf.py -C firmware -B "$PWD/firmware/build-hardware" \
  -D SDKCONFIG="$PWD/firmware/build-hardware/sdkconfig" \
  -D "SDKCONFIG_DEFAULTS=$PWD/firmware/sdkconfig.defaults;$PWD/firmware/profiles/default.defaults" \
  menuconfig
```

在 `PET modules` / `PET extensions` 中只开启已安装的模块。保存后用相同参数将最后的 `menuconfig` 换为 `build`。该目录由手工配置拥有，不交给 `build_matrix.py` 复用。`default` 隔离预设保证所有扩展关闭；`full` 是软件适配器检查预设，会开启温湿度、摇杆和光敏，并编译 GPIO=-1 的舵机，不应当作无需配置的烧录包。

已有 `firmware/sdkconfig` 的显式值不会被默认值悄悄覆盖；使用上面的独立目录，或主动在 menuconfig 调整原配置。没有新增热插拔协议、自动旧配置迁移或服务端硬件开关。

## 默认按键

主页短按 BOOT 查看消息，长按至少 800 ms 后松开进入菜单；菜单短按选择下一项，长按执行，选择“返回主页”退出。语音菜单“按住说话”在持续按住至启动后录音，松开发送；未启动语音、静音或免打扰时沿用现有门控。开机按住及消息抢占不复用旧手势确认新画面。

## 验证边界

本轮只构建和执行主机验证，不烧录、不擦除设备。真实按键、ADC、电源、背光、声学、舵机限位及长期稳定性仍须按实物验收。历史 `firmware/sdkconfig` 可能保留旧显式开关；发行基线使用隔离 `default` 预设，不覆盖开发者现有配置。


本轮软件验证：默认无扩展与 full 扩展组合、off/tones/duplex 五预设均完成 ESP-IDF v6.1 构建；主机/BSP 五种组合、输入/调光状态机与真实 LVGL 10 项及 ASan/UBSan 通过。[远程 CI](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43717383447) 绑定 `acec2a5`，7 个任务成功。此记录不代表实物接线或功能已验收，详细证据见 [18](18-requirements-and-progress.md)。
