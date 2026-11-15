# firmware

ESP-IDF 6.1 固件工程。S2 组件化、S3 契约/配置、S4 温湿度纵向切片、S5-A/B/C1/C2 消息传输与持久化、S5-D 首版屏幕消息界面、S5-E 设备侧持久回执、S5-F nexus-core v1 协议适配已落地：`main` 只做组合根，契约、事件总线、业务模块、板级实现与扩展拆为独立组件。

设备首版交付见 [09](../docs/09-software-delivery.md)，物理配置、诊断、备份/OTA 与迁移见 [10](../docs/10-maintenance-recovery.md)，远程管理见 [11](../docs/11-product-software-closure.md)。隔离构建预设、CI、coredump 见 [13](../docs/13-operational-software.md)，最新屏幕/BOOT 恢复与 USB 降级见 [14](../docs/14-recovery-and-device-names.md)；AI/模型启动、提示音、维护入口、扩展和 OTA 都需相应显式开关。以下 S/G/A 小节保留分阶段记录，当前差异已注明。

发行硬件为 DevKitC-1 N16R8、240×320 ST7789、INMP441、MAX98357A 与喇叭，BOOT 为默认菜单输入；温湿度、摇杆、光敏、舵机均为可选扩展。当前接线和操作入口见 [27](../docs/27-hardware-and-input.md)，其中功放 LRC 已由 GPIO38 调整为 GPIO40。

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

或使用 CI 入口：`../tools/ci/build_firmware.sh`。构建数据以最新 `idf.py size` 为准；S5-D 增加 LVGL 与 16/24 px 中文字体，镜像明显大于 S5-C2。只构建，不烧录、不擦除 Flash。

## 校验

```bash
python3 ../tools/ci/check_deps.py       # 组件依赖图无环
python3 ../tools/ci/check_contracts.py  # schema、profile、example、扩展 manifest、生成物一致性
../tools/ci/test_host.sh                # 主机测试（事件契约、生成配置头、扩展清单、温湿度、消息时间/校验/队列、网络策略）
```

`build_firmware.sh` 会依次执行依赖检查、契约校验、主机测试、`idf.py build` 与 `idf.py size`，随后运行小智协议和 WSS 主机测试。

## 扩展开关

设备控制、舵机、摇杆均默认关闭。摇杆通过 `CONFIG_PET_EXT_JOYSTICK=y` 接入菜单，故障时回退 BOOT；接线与操作见 [27](../docs/27-hardware-and-input.md)。设备控制与舵机关闭时二进制不含对应 `pet_ext_*_init`：

```bash
# 启用示例：复制 sdkconfig 后追加，再用独立 build 目录构建
cp sdkconfig /tmp/pet-sdkconfig.ext
printf '\nCONFIG_PET_EXT_DEVICE_CONTROL=y\nCONFIG_PET_EXT_SERVO=y\n' >> /tmp/pet-sdkconfig.ext
idf.py -B build-ext -D SDKCONFIG=/tmp/pet-sdkconfig.ext build
nm build-ext/nexus_pet.elf | grep pet_ext_
```

## 气候模块开关

温湿度扩展默认关闭；显式 `CONFIG_PET_MODULE_CLIMATE_ENABLED=y` 后使用 DHT11（GPIO15，5 s 采样）。无硬件时可同时启用 fake 传感器验证链路：

```bash
# 桌面验证：固定温湿度序列，不需要 DHT11
cp sdkconfig /tmp/pet-sdkconfig.fake
printf '\nCONFIG_PET_MODULE_CLIMATE_ENABLED=y\nCONFIG_PET_CLIMATE_FAKE_SENSOR=y\n' >> /tmp/pet-sdkconfig.fake
idf.py -B build-fake -D SDKCONFIG=/tmp/pet-sdkconfig.fake build
xtensa-esp32s3-elf-nm build-fake/nexus_pet.elf | grep fake_climate
```

关闭 `CONFIG_PET_MODULE_CLIMATE_ENABLED` 可验证组合根不依赖该模块：构建通过且 ELF 中无 climate 符号。启用时 UI 提供数值、60 分钟图表与重试，串口保留诊断摘要；关闭时菜单跳过温湿度，不将未安装模块报成故障。

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
- `components/svc_message_hub/`：nexus-core 信封/UUID 校验、8 条优先级队列、16 槽去重与回执记录、TTL/DND/ACK 状态机，以及 NVS `msg_inbox` 双代 v3 快照。
- `components/svc_net/`：网络策略（设备 ID/配置校验/主题构建）、服务生命周期与 ESP/fake 驱动，受 `CONFIG_PET_MODULE_NET_ENABLED` 与 `CONFIG_PET_NET_FAKE` 控制。
- `components/svc_growth/`：成长规则、双槽存档和每秒结算任务，受 `CONFIG_PET_MODULE_GROWTH_ENABLED` 控制。
- `components/presentation/`：温湿度卡片与消息展示/按键状态机，可主机测试。
- `components/ui_lvgl/`：单任务拥有 LVGL，读取消息快照，处理刷新完成、滚动和已读回执。
- `components/bsp_dodo_s3_lcd/`：ST7789 SPI3、GPIO42 PWM 背光、GPIO0 BOOT，按 `board.json` 生成的引脚配置接入。
- `../extensions/`：扩展组件，受 `CONFIG_PET_EXT_*` 控制；`EXTRA_COMPONENT_DIRS` 收集。
- `boards/dodo-s3-lcd/`：板级 `board.json`（引脚/能力唯一事实源）。
- `partitions/`：新分区表与迁移说明，不复用 02/03 的分区表。
- `tests/host/`：主机测试。

每个组件只暴露 `include/`，`src/` 私有；依赖通过 `REQUIRES`/`PRIV_REQUIRES` 显式声明。显示和 BOOT 通过 HAL 注入；音频等其余 HAL 随后续切片创建。

## 分区

`partitions/16m.csv`：`nvs` 64 KB（含 `msg_inbox` 双代快照），`ota_0`/`ota_1` 各 4 MB，`storage` 2 MB，`assets` 4 MB，`model` 1 MB，`coredump` 64 KB，剩余 832 KB 保留。

## 边界

- 不在本目录复制 02/03 的整仓源码；先抽取组件再引用。
- 不手改生成的 `sdkconfig`、`build/`、`managed_components/`。
- 构建与烧录分离；默认只构建，不烧录、不擦除 Flash。
- 详细方案见 [`../docs/00-architecture.md`](../docs/00-architecture.md)。

## S5-D 消息界面

- 默认开启 `CONFIG_PET_MODULE_UI_ENABLED`；关闭 UI 或网络时不启动显示与按键任务。
- `CONFIG_PET_UI_DND` 提供初始默认值；当前运行配置由 `svc_config` 持久化，物理维护与 CORE 远程下发共用版本事务，支持手动和定时免打扰。
- 空闲短按 BOOT 打开未读；卡片可见时短按确认已读；启动时按住、长按及跨消息按下/松开不确认。自动弹窗超时隐藏，手动打开保留至确认、过期或被紧急消息抢占；滚动到底不自动已读。
- 未读消息按优先级展示，critical 抢占；完整帧刷新失败不发送 displayed，回退纯文本后仍失败则熄屏、保留消息重试。
- 五种模板使用类别图标与边框，评论有引用区、系统卡提高正文密度；NEXUS 信封按标题/正文/来源显示，不额外解析富 payload。提示音需显式开启并受静音/DND 与音频忙碌门控。确认接 BOOT，未假定 KY-036 当前接线。
- S5-E 后，离线已读先持久保存并移出未读队列，后台补发；仅持久化失败或回执队列已满时显示“确认失败 · 短按重试”。Broker PUBACK 不等于业务服务已入库，协议适配已完成，仍需真实端到端验收。
- 中文字体随应用嵌入，覆盖 GB2312 汉字、ASCII 与常用标点，缺字显示方框并只记录数量。后续 AI 体积预算应评估迁入 `assets` 分区。

```bash
python3 ../tools/board_codegen.py --check
../tools/ci/test_ui_host.sh   # 同一 LVGL/字体/卡片代码，生成 build-ui-preview/*.ppm
```

屏幕模拟只证明渲染逻辑和布局；SPI 刷新耗时、背光、BOOT 实物行为、PSRAM/内部堆峰值仍需上板验证。

## S5-E/F 持久回执与协议

收件箱、完整投递身份与最多 16 条回执记录在同一份 v3 快照中提交；同一投递只保留最新状态。每秒由独立任务尝试补发，单条 PUBACK 最多等待 3 s；网络等待不持消息锁。写入失败保留原状态；确认后落盘失败允许安全重发。

已确认 received/displayed 回执被移除；已确认 read/expired 记录作为可淘汰的终态保留，服务端重投时可再次回复。收件箱满或未确认回执积压时拒收新投递；不能挤掉已由设备确认接管的消息。未对时时暂不接收新消息，过期发送 rejected/expired，不冒充用户已读。

v3 可读 v1/v2，旧收件箱仍可本地阅读，旧 msg_ 回执独立留档；首次成功变更写入 v3。旧固件不能读取 v3，降级前需要备份和迁移。最大快照 23,116 B，64 KB NVS 可容纳两代及当前配置上限的凭据/温湿度存档；非活动代先清理再写，保护实际恢复代。

当前消息使用 `/inbox`、`/acks`、`/state` 与 UUID/事件/设备身份；另有 telemetry/config/config_acks 管理链路。配置下发、完整回读、传感器摘要及 Java HTTP/PostgreSQL/MQTT/C 隔离回环已验证，见 [`integration/README.md`](../integration/README.md)。真实部署的 Broker/ACL 和硬件仍待验收。

## G1 成长服务

`pet/growth.h` 定义动作、结果和快照；`svc_growth/api.h` 接受可注入时钟，提供 interact/tick/snapshot/flush。固件由 `pet_growth_start()` 启动独立工作任务，存档故障不阻断其他模块。互动应由业务任务调用，快照读取非阻塞。

默认 UTC+8，每日 04:00 换日；`PET_GROWTH_UTC_OFFSET_MINUTES` 提供初值，运行配置支持固定偏移。存档为独立 `pet_growth` 命名空间，当前 v4 为两份 104 B 快照和一份 16 B 提交记录，可读取 v1 80 B、v2/v3 84 B；旧固件不能读取 v4。新增对话/专注奖励去重水位。首次软件体验使用 `../tools/simulate_growth.sh`，恢复策略见 [`04-growth-game.md`](../docs/04-growth-game.md)与[维护说明](../docs/10-maintenance-recovery.md)。

## G2 成长主页

成长与 UI 同时启用时显示成长主页；主页长按 BOOT 打开菜单，短按选择、长按执行，返回项退出。消息弹出取消菜单，短按查看/已读语义保持。互动经独立 `pet_interact` 任务写存档，UI 非阻塞读取结果。成长关闭时保留消息空闲页。主机预览与测试使用 `../tools/ci/test_ui_host.sh`。

## G3 每日任务

成长菜单新增喂食/摸头/玩耍任务领取入口，每项 15 经验与 5 金币，每日 04:00 换日；领取请求携带任务日期，使用同一事务存档。G3 快照升级为 v2（84 B），可读 v1（80 B）；G5 后写 v3，见下文。CLI 支持 `claim-feed` / `claim-pet` / `claim-play`。验证和版本边界见成长文档 G3。

## G4 状态动画

`presentation/pet_motion` 接受成长状态、单调毫秒与可见性，输出纯整数姿态；`ui_lvgl/pet_avatar` 使用一个固定 208×78 对象绘制八种状态。现有 UI 任务以至少 40 ms 的间隔推进帧，更高优先级即时切换，降级需稳定 3 s；睡眠/唤醒和兴奋结束即时切换。消息、菜单和不可用状态隐藏角色并停帧，不写成长存档。

`../tools/ci/test_ui_host.sh` 输出真实 LVGL 帧，随后 `python3 ../tools/export_pet_preview.py` 生成 `build-ui-preview/pet-animation.html`。预览为 10 fps 抽样，25 fps 仅为设备调度上限；SPI 帧率、运行时堆/栈和音频并行未测。实现范围与验证见成长文档 G4。

## G5 成就

首次成功照料、亲密 20、等级 5 分别一次性奖励 10/20/30 金币，与成功操作在同一存档提交，仍按 999,999 封顶。位图随快照暴露，菜单成就页和 CLI `achievements` 只读展示；升级或达成触发 3 s 兴奋表现。

G5 当时使用 v3 84 B，在保留字节 78 存成就位图；当前 v4 104 B 继续读取这些旧档。读取旧档不发奖；首次照料需升级后成功操作，其他门槛在下一次成功操作时按当前数值检查。详细事务、恢复和验证边界见成长文档 G5，未烧录或迁移设备 NVS。

## G6 温湿度成长联动

成长工作任务经事件队列接收滤波后温湿度，可信读数 30 s 内有效：温度 <10 / >30 °C 或湿度 <30 / >80% 时，每小时额外扣 1 点心情，同时不适不叠加。无效、低质量或过期读数清除影响；重启不补算离线环境。仅环境策略保存在 RAM，仍写 v3 84 B 成长存档。

主页和角色增加冷/热/干/湿表现，保留业务状态与消息/菜单优先级。CLI `climate 温度 湿度`、`climate-off`、`climate-loss` 可模拟持续采样、失效和断流；预览新增四种环境。详细事件/线程边界与验证见成长文档 G6；传感器、LCD 和 Flash 实机验收待后续。


## A1 音频 HAL 与诊断

`pet_hal` 提供 signed 16-bit mono PCM 接口；BSP 使用 INMP441（16/17/18）与 MAX98357A（21/38/39）两路 I²S。初始化不打开设备，录音/播放由单一拥有者串行调用；业务诊断放在 `svc_audio`，不占用 BOOT 或改动成长/消息交互。

- `CONFIG_PET_MODULE_AUDIO_ENABLED=y`：默认只编入音频能力，不自动录音。
- `CONFIG_PET_AUDIO_FAKE=y`：使用合成方波/逻辑播放，适合桌面检查。
- `CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT=y`：显式启用一次一秒录音后回放（25% 音量），默认关闭，DND 时不可选。只有此选项启用时，组合根才创建诊断任务。

在独立 `SDKCONFIG` 与 `build-*` 中配置后只运行 `idf.py ... build`；本阶段不执行烧录。`tools/ci/test_host.sh` 覆盖 PCM/FIFO/服务及 BSP 边界故障；细节与未覆盖硬件范围见 `docs/01-ai-agent.md` A1。

## A2 端点与会话引擎

`svc_ai_agent/session.h` 提供纯 C 会话 API；`contracts/pet/ai.h` 固定状态与快照。`CONFIG_PET_MODULE_AI_ENABLED=y` 默认编译引擎，但不创建任务、不启用音频、不连接云端；关闭时不编译实现。实例由单一拥有者串行调用，返回状态与控制意图，后续运行任务负责 HAL/网络执行及失败反馈。

在仓库根运行 `./tools/simulate_voice.sh` 演练端点、按住说话、手动打断、静音与旧轮次隔离。CLI 的 PCM/唤醒/响应/播放完成均为注入，BOOT 消息/成长行为保留，A7 增加语音菜单与状态页面；设备应用可通过 A6 选择启动 A4/WSS，真实唤醒模型与云端仍待验收。接口、时钟/轮次约束与验证见 `docs/01-ai-agent.md` A2。

`svc_ai_agent/runtime.h` 固定运行拥有者边界：拥有者读取帧后调用 `pet_ai_runtime_frame`，外部事件调用 `pet_ai_runtime_handle`；音频/传输动作由 sink 顺序执行，WakeNet/VAD 由 frontend 注入。A3 本身不创建任务，A4 服务持有它并执行 HAL；详见 `docs/01-ai-agent.md` A3。

## A4 FreeRTOS 拥有者服务

AI 与音频开关同时开启时编译 `svc_ai_agent/api.h` 对应实现。组合根显式注入音频设备、非阻塞传输和可选前端，再调用 `pet_ai_agent_start`；默认应用不启动，A6 提供可选接线。跨任务控制使用 `post`，24 kHz PCM 复制使用 `play`，`snapshot` 查询异步初始化/故障与只读状态。播放结束只能由任务等待 HAL drain 后生成，HAL/清理失败锁住服务至重启；不得同时运行 A1 诊断。队满须处理返回错误，不保证物理即时静音。接口、资源预算与主机/构建证据见 `docs/01-ai-agent.md` A4。

## A5 小智协议与编解码适配库

`CONFIG_PET_AI_XIAOZHI_ENABLED` 控制 `svc_ai_agent/xiaozhi.h` 与 `opus.h` 对应实现，依赖 AI/音频开关。固定为 03 默认 WebSocket v1、上行 16 kHz/下行 24 kHz 单声道、60 ms Opus；内部与 A4 之间使用 20 ms PCM。单轮连接代次、严格握手、尾包补齐与播放 `peek/commit` 背压见 `docs/01-ai-agent.md` A5。不能在 A4 采集循环中阻塞握手或运行 codec。组件清单锁定 JSON/codec 依赖；初次 `idf.py reconfigure` 后可执行 `bash tools/ci/test_ai_protocol_host.sh`。

## A6 WSS 与可选启动

`CONFIG_PET_AI_XIAOZHI_WSS_ENABLED` 编译 `svc_ai_agent/wss.h` 对应的独立网络/codec 拥有者；依赖真实网络、PSRAM 和证书包。`CONFIG_PET_AI_XIAOZHI_ON_BOOT` 默认关闭，显式开启后 `app_ai_start` 从 NVS `xiaozhi` 读取 `ws_uri`、`ws_token`、`device_id`、`client_id` 及可选 `ws_ca`，组合 WSS 与板级音频；凭据错误跳过启动，不写 NVS。没有启用 A8 模型时初始化不开麦，需主动 PTT；启用模型则本地监听，唤醒或 PTT 才开云端轮次。A7 提供 BOOT 菜单与 PTT 电平接口。

WSS 每轮新连接，IP/SNTP 门控、验证 TLS、有限 PSRAM 队列、分片重组、播放背压及失败回送见 [`../docs/01-ai-agent.md` A6](../docs/01-ai-agent.md#18-a6-wss-网络拥有者与-a4-接线2026-11-12)。网络启动仍沿用 `svc_net` 的 Wi-Fi/MQTT 配置。`IDF_PATH=... bash tools/ci/test_ai_wss_host.sh` 运行真实 A4/A5/WSS 逻辑与外部 SDK/OS/I²S 替身；不连接云端。

## A7 输入、状态与隐私

AI/音频/UI 开启且不处于开机诊断模式时，现有 BOOT 菜单增加语音、静音和 DND。AI 已就绪时长按菜单默认选中说话项；按住至显示“正在听”后说话，松开发送，活动对话短按取消。成长菜单项和消息确认仍保留；关闭成长模块时提供独立语音菜单。未配置或未启动 AI 会显示不可用，不隐式开启服务。

`pet_ai_agent_ptt(bool)` 由音频拥有者绑定轮次；`pet_ai_agent_set_policy` 统一用户静音、DND 与 critical/显示故障中断。静音、解除静音和取消使用优先邮箱，普通队满也接受；快照区分请求、待处理和 HAL 启停，清理失败显示故障。UI 只读快照，不直接操作音频 HAL。选择仅本次开机有效，默认仍不启动 AI、不录音；真实模型/云端/硬件验收待推进。详见 `docs/01-ai-agent.md` A7。

## A8 本地唤醒与模型 VAD

`CONFIG_PET_AI_ESP_SR_ENABLED` 默认关闭；`sdkconfig.ai-sr.defaults` 是显式启用 AI 开机服务及“小爱同学”/`vadnet1_medium` 的独立构建预设。仍需有效 NVS 凭据和现有网络配置。开启后空闲本地监听在屏幕可见，不上传或保存空闲音频；思考、播放、静音/DND 和关键消息抢占停止采集。默认固件继续不开麦。

模型通过 A4 音频任务的 open/reset/close 生命周期管理，异常锁住至重启。固定 1 MiB 模型分区与 960 KiB 包限制，构建生成 SHA256、启动核对配套镜像；固件与 `srmodels/srmodels.bin` 必须成对。应用 OTA 校验模型哈希不变，改变模型使用配套 USB 包；模型 OTA 未实现。独立构建命令与边界见 [`A8 实现记录`](../docs/01-ai-agent.md#20-a8-wakenet-与模型-vad-接入2026-11-12)。后续 DSP/参考缓冲已接线，保持半双工回退；真实推理、录音、云端、声学 AEC/全双工与打断仍未验收。
