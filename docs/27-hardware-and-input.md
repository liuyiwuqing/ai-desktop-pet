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

摇杆和光敏的配置、操作与验证随本轮后续工作项补充，不将引脚预留当成驱动已完成。

## 默认按键

主页短按 BOOT 查看消息，长按至少 800 ms 后松开进入菜单；菜单短按选择下一项，长按执行，选择“返回主页”退出。语音菜单“按住说话”在持续按住至启动后录音，松开发送；未启动语音、静音或免打扰时沿用现有门控。开机按住及消息抢占不复用旧手势确认新画面。

## 验证边界

本轮只构建和执行主机验证，不烧录、不擦除设备。真实按键、ADC、电源、背光、声学、舵机限位及长期稳定性仍须按实物验收。历史 `firmware/sdkconfig` 可能保留旧显式开关；发行基线使用隔离 `default` 预设，不覆盖开发者现有配置。
