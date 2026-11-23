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
| KEY_A 确认键 | 一端接 GND | GPIO13（内部上拉，active-low；未接线时关闭 `PET_INPUT_TRIPLE`） |
| KEY_B 方向键 | 一端接 GND | GPIO14（同上） |
| KEY_C 语音键 | 一端接 GND | GPIO41（同上） |

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
| `PET_INPUT_TRIPLE` | y | GPIO13/14/41 三物理按键输入层（未接线设备应关闭，降级为 BOOT 单键） |
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

主页短按 BOOT 查看消息，长按至少 800 ms 后松开进入菜单；菜单内短按选择下一项，长按进入分类或执行动作，BOOT 左方向键返回上级/关闭。语音菜单“按住说话”在持续按住至启动后录音，松开发送；未启动语音、静音或免打扰时沿用现有门控。开机按住及消息抢占不复用旧手势确认新画面。

### 两级菜单（INT-01-S3，主机交付）

菜单改为静态表驱动的两级结构（`ui_lvgl/src/menu_def.h/.c`）：ROOT 分类为互动、状态、环境、消息、AI 对话、设置、网络、系统，各分类下挂叶子动作，深度恒为 2，新增节点只改表不改状态机。可用性按动作门控：AI 项需语音、环境需温湿度、网络需网络模块、成长/成就/专注在 `voice_only` 下隐藏，分类内全部不可用时整类跳过。进入子页刷新 30 秒无操作计时，超时自动关闭菜单回主页（`pet_growth_input_menu_expired`）。设置分类提供音量/亮度/提示音 ±5、息屏时长循环（关闭→30→60→300→600 秒）与待机亮度调整，本地立即生效并异步保存；消息分类触发未读浏览，系统分类显示固件版本（`esp_app_get_description`）。选中即预览（冷却、任务、成就说明）沿用既有渲染，“成长成就”仍为仅预览不执行。所有叶子动作经统一 `handle_selection` 分发，BOOT 与三键路径共用。主机测试 `test_menu_def`（表结构与深度约束）与重写后的 `test_growth_input`（两级导航、PTT、超时、门控）覆盖。

启用 `PET_SETUP_PORTAL_ENABLED` 后，菜单增加“网络设置”，执行后重启进入限时热点配网；设置页长按 BOOT 2 秒返回宠物。手机设置 Wi-Fi、小智官方绑定与可选 MQTT 的操作见 [28](28-device-web-setup.md)。正常语音/消息与热点设置使用独立启动模式，不同时拥有 Wi-Fi 和显示。

### 三物理按键层（INT-01-S2，主机交付，待接线）

`board.json` `input.keys` 定义三键并经 `tools/board_codegen.py` 生成 `PET_BOARD_KEY_{CONFIRM,NEXT,VOICE}_GPIO`（13/14/41）；全部按键对地、内部上拉、active-low。`CONFIG_PET_INPUT_TRIPLE`（默认 y）启用后 `pet_button_input_t` 暴露 `keys_init`/`keys_read`，hal 层只做电平采集；消抖与分类由纯逻辑 `pet_key_tracker_update`（`pet/hal/keys.h`）完成：30 ms 消抖、短按 <800 ms、长按 ≥800 ms（每按一次只发一次）、组合=两键按下起点重叠窗口 ≤300 ms（仅定义 A+B 重启提示、B+C 息屏开关，其余键对不组合），组合期间抑制两键的单键事件，两键均稳定释放后发出组合事件。业务映射为表驱动 `pet_ui_key_action`（`ui_lvgl/src/key_actions.h`），按场景（主页/菜单/消息/AI 对话/TTS 播放/睡眠/息屏）翻译为 UI 动作，设计原则 A=确认/播放控制、B=导航/音量、C=语音/静音，KEY_C 长按恒为静音；完整矩阵见 [30 §3.3](30-interaction-feature-expansion-plan.md)。映射已于 INT-01-S3 接入 ui_service 状态机：`key_scene` 判定场景（息屏 > 消息 > AI 对话 > TTS 播放 > 菜单 > 主页），`key_dispatch` 翻译为统一动作（菜单开/确认/上下级、PTT 起止、静音、打断 TTS、音量、息屏/唤醒、重启提示、未读浏览、B 长按直达设置分类）。主机测试覆盖 tracker 全部阈值/组合/抑制与映射矩阵（`test_key_input`）与 BSP 三键采集（`test_bsp_input`）。边界：睡眠场景暂按主页场景处理（矩阵中睡眠专属动作与消息详情/清除、上一条、重播等低频动作未接线）；三键实体未接线，全部结论仅为主机测试。

## 验证边界

HW-01–03 仅构建和执行主机验证，当时未烧录或擦除设备；后续用户授权的首次实机操作单列于下节。真实按键、ADC、电源、背光、声学、舵机限位及长期稳定性仍须按实物验收。历史 `firmware/sdkconfig` 可能保留旧显式开关；发行基线使用隔离 `default` 预设，不覆盖开发者现有配置。


本轮软件验证：默认无扩展与 full 扩展组合、off/tones/duplex 五预设均完成 ESP-IDF v6.1 构建；主机/BSP 五种组合、输入/调光状态机与真实 LVGL 10 项及 ASan/UBSan 通过。[远程 CI](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43717383447) 绑定 `acec2a5`，7 个任务成功。此记录不代表实物接线或功能已验收，详细证据见 [18](18-requirements-and-progress.md)。

## 首次实机记录（2026-11-15）

用户实际连接的芯片为 ESP32-S3 rev 0.2，16 MB Flash、8 MB PSRAM。完整原 Flash 备份已通过芯片摘要核验；用户确认后仅初始化 `0x9000–0x18FFF` 的 64 KB NVS，旧小智联网/授权设置不自动迁移。基础应用及配套四份产物均经独立 `verify-flash` 验证。备份和全部本地证据位于忽略目录 `build/hardware-20261115/`，不进入 Git。

| 验证项 | 实际结果 | 边界 |
| --- | --- | --- |
| 启动与存储 | PSRAM 自检通过；连续串口会话 34 秒无 panic，NVS 失败 0 | 未测试断电恢复或长期运行 |
| 显示软件 | UI 初始化；734 次帧提交、0 次失败、最大 94 ms | 初始只有背光；绕过面包板供电后用户确认角色页面正常 |
| 显示隔离 | 原供电路径下，10 MHz SPI、直接 RGB565 五色循环，无 LVGL | 用户确认当时画面完全无变化；后续由供电改线恢复，未修改正式显示驱动 |
| BOOT 菜单 | 用户确认长按打开、短按切换正常 | 未扩大到全部菜单动作或掉电恢复 |
| 合成提示音 | 功放接线不一致，用户断电调整后确认可听见 1 秒提示音 | 具体原错接引脚未提供，未测最大功率或失真 |
| 麦克风回环 | 首测无有效声音；手动 BOOT 诊断两次录入/播放各 16,000 个采样，drain/cleanup 成功，用户确认能听清本人语音 | 第二次 RMS 1,529、peak 21,698；未校准增益、频响或噪声 |
| 并行音频 | 原 UI 启用版本超时；关闭 UI、USB 主控制台的隔离版本完成 16,000/24,000 个采样，最大采集间隔约 20 ms | 改线后合成音可听见；UI 同启的超时原因、参考对齐/漂移及声学打断仍待验证 |

照片中模块丝印为 **240×320、2.4 寸 ST7789 SPI**，供电脚标为 **3V3**。从 GND 一端起的排针顺序为 `GND、3V3、SCLK、SDA(MOSI)、RESET、DC、CS、BLK`；按丝印接线，不按插头左右方向猜测，也不将该 3V3 脚改接 5V。照片没有包含完整线缆两端，不能代替接线核验。

本次屏幕与麦克风最初经面包板电源轨供电。用户断电后只将屏幕 `3V3/GND` 绕过面包板直连开发板，六根信号线保持原接法；重新上电后角色页面正常。这将故障范围缩小到原面包板供电路径/连接；模块端电压和具体断轨/触点尚未测量确认。用户随后确认 BOOT 菜单正常；核对功放 5V/GND、SD_MODE 与 GPIO21/40/39 后修正不一致接线，提示音可听见。不要仅凭背光、I²S 返回值或单次非零 peak 判定整机通过。

为消除“聊天文字提示”与实际录音时机不一致的歧义，最后使用本机忽略目录中的独立手动诊断：蓝色待机；短按并释放 BOOT 后，先发出合成短音，红色录音 1 秒，绿色时停止说话、听 1 秒回放，再返回蓝色。它直接编译正式 BSP/PCM/回环源码，无网络或 NVS 初始化，不自动录音，不留存 PCM。用户按键试验后明确确认能听清自己的话。该临时程序不属于正常发行固件；无 UI 的隔离版本不绘制角色画面，恢复基础版本后角色页已由用户再次确认。

本机维护版本显式选择 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` 和 `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`，并设置 `CONFIG_PET_PROVISION_CONSOLE_ENABLED=y` / `CONFIG_PET_RECOVERY_ENABLED=y`；默认 UART 主控制台的 USB 副输出不能代替输入通道。本板打开原生 USB 串口时观察到 USB 复位，诊断应在同一连接内等待启动后再连续查询，不能将刚复位时的零帧计数解释为故障。

临时探针仅替换应用分区，不再次初始化 NVS；测试后恢复关闭录音/提示音诊断、关闭可选扩展及开机语音的基础版本。屏幕绘制/BOOT 菜单/提示音及语音录入回放已由用户观察确认；后续补充说明主页虽有“等待时间同步”，宠物实际上未显示，不能将原“角色页面正常”反馈当作宠物可见的最终结论。HW-05 单列修正显示对时间的依赖：未对时仍保留宠物及提示，成长结算继续等待真实时间。修正版应用已烧入并独立回读校验，用户随后明确确认宠物已显示、菜单正常。

联网/云端、UI 同启的并行诊断、OTA/掉电、扩展及长稳仍待验收。本机凭据模板和字段说明放于忽略目录 `data/device-setup/`；填完整包并配合开机 WSS 启动开关后，才继续真实联网/语音验证，见 [维护入口](10-maintenance-recovery.md#物理维护入口)。上述本机实测不新增远程 CI 或生产部署结论。
