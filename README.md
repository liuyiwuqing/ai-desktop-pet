# 04 AI 超级桌面宠物（AI Desktop Pet）

状态：**设备软件与 NEXUS 远程管理已完成本地软件闭环，暂不上板。** 已接入运行配置、播放缓冲、对话文本/上下文/MCP、成长奖励/专注、温湿度图表、消息模板/提示音、诊断/备份、远程配置/回读/审计、DSP 回退、设备/舵机执行器和签名 OTA/恢复工具。语音开机服务、维护入口、扩展与 OTA 默认关闭；隔离 HTTP/PostgreSQL/MQTT 联调通过，真实部署、云端及硬件验收待做。CORE/记忆工具凭据轮换见 [20](docs/20-credential-rotation.md)。远程 OTA 的事务、管理台与启用步骤见 [19](docs/19-remote-ota.md)。最新语音记忆桥接准备与本地 TLS 回归见 [17](docs/17-voice-bridge-regression.md)，MCP/stdio 接口与运维见 [16](docs/16-voice-memory-tools.md)，长期记忆检索与副本失效见 [15](docs/15-memory-lifecycle.md)，屏幕/BOOT 恢复和设备更名见 [14](docs/14-recovery-and-device-names.md)，消息保留、隔离构建/CI 与浏览器回归见 [13](docs/13-operational-software.md)，前轮管理闭环见 [11](docs/11-product-software-closure.md)，设备首版记录保留在 [09](docs/09-software-delivery.md)。

## 目标

在 ESP32-S3 桌面宠物上统一承载五类能力：

1. AI 智能体语音对话（听、想、说、表情与动作）。
2. 万能信息接收端（第三方消息推送、在线配置、屏幕渲染）。
3. 温湿度传感器接入与可视化。
4. 养成系游戏（成长数值、互动、存档）。
5. 可插拔扩展：语音控制手机/电脑、语音控制舵机等硬件。

设计约束：模块间只通过事件总线和版本化契约通信；HAL 与业务分离；新增能力以扩展包接入，不修改核心模块。

## 文档入口

当前需求、已完成任务、后续顺序和逐项交付约定统一见 [18 · 当前需求与任务台账](docs/18-requirements-and-progress.md)。每项工作同步文档、验证、提交并推送后继续下一项；软件验证与真实云端/生产部署/实机验收分别记录。

设备成长只读同步见 [21](docs/21-growth-sync.md)：上传设备快照并在 CORE 展示，保留两端各自的奖励规则和存档所有权。

完整架构与分模块方案在 [`docs/README.md`](docs/README.md)。评审顺序：

1. `00-architecture.md`：总体架构、目录规范、事件总线、契约与部署拓扑。
2. `01-ai-agent.md`：语音输入/输出、对话上下文、AI 选型、表情与动作。
3. `02-message-hub.md`：接收端选型、配置面板、推送接口、渲染逻辑。
4. `03-climate-sensor.md`：DHT11/升级选型、采集、可视化、异常处理。
5. `04-growth-game.md`：成长数值、互动、状态表现、持久化。
6. `05-extension-device-control.md`：手机/电脑控制扩展接口。
7. `06-extension-servo.md`：舵机与硬件执行器扩展接口。
8. `07-roadmap-acceptance.md`：阶段、依赖与总体验收。
9. `08-structure-review.md`：目录结构、生产级原则与迁移计划。
10. `09-software-delivery.md`：设备首版软件实现、验证入口与当时的交付边界。
11. `10-maintenance-recovery.md`：物理配置、诊断、备份恢复、OTA 与 02 迁移方案。
12. `11-product-software-closure.md`：跨项目管理闭环与该轮构建记录。
13. `12-cloud-interop.md`：小智协议、上下文/工具注册、诊断与真实云端联调步骤。
14. `13-operational-software.md`：消息保留、隔离构建/CI、崩溃恢复及该轮验证证据。
15. `14-recovery-and-device-names.md`：屏幕/BOOT 恢复、设备重命名与该轮验证。
16. `15-memory-lifecycle.md`：长期记忆检索、源评论失效、持久化删除与当前验证。
17. `16-voice-memory-tools.md`：独立 MCP/stdio 公开记忆接口、运维与真实联调步骤。
18. `17-voice-bridge-regression.md`：固定桥接准备、受控启动、stdio 退出恢复与本地 TLS 全链回归。
19. `18-requirements-and-progress.md`：需求映射、当前任务台账与持续交付约定。
20. `19-remote-ota.md`：远程 OTA 协议、持久事务、管理 UI、成套启用与本地验证。
21. `20-credential-rotation.md`：CORE/记忆工具双令牌、受保护文件重载、CLI 与验收边界。
22. `21-growth-sync.md`：设备成长只读镜像、顺序隔离、管理台及 V12 配套离线包。

## 仓库与版本

本工程使用独立 Git 仓库，与 `02`、`03`、`nexus-core`、`mac-bridge` 分开版本化：

- 架构与模块文档随代码放在 `docs/`，保证设计、实现和发布记录可原子提交。
- 构建依赖必须提交锁文件；发布以 tag 标记，契约变更单独升版本。
- 跨仓库复用通过版本化契约或组件抽取完成，不在本仓库直接复制其他工程源码。

## 目录结构

```text
04-ai-desktop-pet/
├── README.md                     项目入口与目录说明
├── docs/                         架构、交付、维护、云端联调与任务台账
├── deployment/                   CORE 服务、TLS 代理、升级与恢复模板
├── firmware/                     ESP32-S3 固件工程（ESP-IDF）
│   ├── main/                     组合根：app_main、启动编排、诊断
│   ├── components/               ESP-IDF 组件（只暴露 include/）
│   │   ├── contracts/            跨模块契约（纯 C，可主机测试）
│   │   ├── core/                 事件总线与基础服务
│   │   ├── pet_hal/              HAL 接口与 fake 实现
│   │   ├── storage/              NVS 薄封装
│   │   ├── svc_climate/          温湿度采集
│   │   ├── svc_message_hub/      消息校验、优先级队列、去重与 NVS 持久化
│   │   ├── ui_lvgl/               消息卡片、成长动画、刷新确认与 BOOT 操作
│   │   ├── svc_config/           配置事务、物理入口与远程配置
│   │   ├── svc_intent/           结构化 MCP 工具路由
│   │   ├── svc_focus/            专注计时
│   │   ├── svc_ota/              身份、签名升级与回滚接线
│   │   ├── svc_ai_agent/         会话/上下文/模型/WSS/音频拥有者
│   │   ├── svc_audio/            半双工录放音诊断（默认不录音）
│   │   ├── svc_net/              Wi-Fi + MQTT/TLS 传输层
│   │   └── svc_growth/           成长规则、双槽存档与运行服务
│   ├── boards/dodo-s3-lcd/       板级 board.json（引脚/能力唯一事实源）
│   ├── partitions/               分区表与迁移说明
│   └── tests/host/               主机测试
├── server/                       AI 网关预留与 Mac 控制薄网关
│   └── ai-gateway/               ASR/LLM/TTS 会话编排与供应商适配
├── integration/                  与 nexus-core 的契约与部署（不放业务代码）
│   └── contracts/generated/      Schema 生成的 TypeScript 类型
├── bridge/                       本机工具桥（复用 mac-bridge）
├── config/                       非敏感配置与 JSON Schema（唯一事实源）
│   ├── schemas/                  设备配置/profile/消息/回执/扩展 manifest
│   ├── profiles/                 板型与功能开关
│   └── examples/                 示例配置（不含凭据）
├── resources/                    运行时资源
│   ├── fonts/                    中文与数字字体子集
│   ├── images/                   表情、图标、背景
│   ├── animations/               表情/动作帧序列
│   ├── audio/                    提示音、音效
│   └── i18n/                     国际化资源预留（当前 UI 中文）
├── extensions/                   扩展包（manifest + 组件 + 主机契约测试）
│   ├── device_control/           设备控制适配器与能力注册
│   └── servo/                    舵机执行器与安全限制
├── tools/                        构建、契约生成、资源打包、迁移、诊断脚本
└── tests/                        跨模块契约与集成测试
```

`main` 只做组合根，业务与基础能力放在 `components/`，每个组件只暴露 `include/`，依赖通过 `REQUIRES` 显式声明。扩展能力统一在 `extensions/` 下，通过 `CONFIG_PET_EXT_*` 编译开关接入，默认关闭。配置与消息以 `config/schemas/` 为唯一事实源，生成 C 头与 TypeScript 类型并由 CI 校验。

## 与现有工程的关系

| 现有资产 | 复用方式 |
| --- | --- |
| `02-desktop-pet` | 复用离线玩法、DHT11/光敏/摇杆/触摸驱动思路、NVS 存档经验；不复制整仓。 |
| `03-xiaozhi-voice` | 语音底座、DODO 板型、ST7789/INMP441/MAX98357A 接线与构建经验。 |
| `nexus-core` | 消息中心：Halo/Webhook 接入、消息去重、MQTT/TLS 与设备认证、配置下发/回读/审计和管理台；不代理语音。 |
| `server/ai-gateway` | 自建 ASR/LLM/TTS 网关预留；首版由 ESP32 直接连接小智，经 `ai_session` 契约隔离。 |
| `mac-bridge` | 手机/电脑控制的本机执行适配器，继续用固定白名单与确认机制。 |
| `docs/HARDWARE.md` | 硬件与分区事实来源；新工程重新定义分区，不复用 02/03 的分区表。 |

原则：先抽取可复用组件，再在新工程引用；禁止把 02/03 的源码整段复制成第二份实现。

## 硬件基线

- ESP32-S3 N16R8：16 MB Flash、8 MB PSRAM。
- 2.4 英寸 ST7789 240×320 SPI 屏。
- INMP441 I2S 麦克风、MAX98357A I2S 功放与扬声器。
- DHT11/KY-015 温湿度；37 合 1 KY 套件中的光敏、摇杆、触摸、蜂鸣器。
- 预留 1–2 路舵机扩展（独立 5V 供电）。

## 当前进度（2026-11-13）

服务端已建立独立本地 Git 基线，离线发布包、配置/主机预检和只读探测已实现，包内实际 JAR 的 Linux 容器启动/重启通过，见 [部署记录](deployment/README.md)。后续任务以 [18](docs/18-requirements-and-progress.md) 为准，以下保留早期切片说明。

S1–S4、S5-A 至 S5-F、G1–G6 成长模块及 A1–A8 音频/会话/WSS/输入/UI/模型接入软件已完成相应代码切片。消息链路现在使用现有 nexus-core v1；v3 消息快照保存完整投递身份，兼容 v1/v2 旧数据。离线已读、重启补发、终态重投和满队列重试已有主机验证；仍需真实 Broker 与上板验收。详细行为与验证见[消息模块](docs/02-message-hub.md#s5-f-nexus-core-v1-协议适配2026-11-11)。

- 当前仍未烧录 `04`，硬件与整条消息时间线待实机验收。
- 2026-11-12 补齐 14 项设备软件交付及跨项目管理闭环，见 [当前记录](docs/11-product-software-closure.md)。NEXUS 配置事务、管理台、ACL 升级工具与 Java/C 回环均已实现；真实库和 Broker 尚未升级。当前电脑控制仅 Mac Bridge 三项白名单能力。
- 02/03 保持独立；分区不兼容，烧录前需备份、核对板上固件与迁移方案，不自动擦除 NVS。
- 本阶段主机测试与构建结果不能替代屏幕、按键、网络、掉电和长期稳定性的实机验收。

以下 G/A 条目保留各切片落地时的状态，后续接线与最终结果以 [11](docs/11-product-software-closure.md) 为准。

成长服务可直接在桌面体验：运行 `./tools/simulate_growth.sh`，输入 `feed`、`pet`、`play`、`sleep`、`wait 3600`、`wake`。说明与边界见[成长模块 G1](docs/04-growth-game.md#9-g1-软件切片2026-11-11)。

G2：空闲主页长按 BOOT 进入互动菜单，短按切换、长按执行；主页及消息页的短按消息操作保留。消息弹出取消菜单。

G3：每日喂食 1 次、摸头 3 次、玩耍 1 次，每项可领取 15 经验与 5 金币。菜单中进入对应任务领取；CLI 使用 `claim-feed` / `claim-pet` / `claim-play`。

G4：成长主页显示原生图元宠物，随发呆、开心、兴奋、低落、困倦、饥饿、生病、睡眠切换动作；消息和菜单遮挡时停帧。运行 `./tools/ci/test_ui_host.sh` 后执行 `python3 tools/export_pet_preview.py`，打开生成的 `firmware/build-ui-preview/pet-animation.html`，可在桌面切换八种状态预览。

G5：首次成功照料、亲密 20、等级 5 分别奖励 10/20/30 金币，随成功操作一次性保存；菜单在任务之后新增成就查看页，CLI 使用 `achievements`。成长存档现为 v3，兼容 v1/v2，旧固件不能读取 v3，降级需先备份/迁移。

G6：可信温湿度读数可触发冷/热/干/湿表现，并按有效读数覆盖的在线时长每小时额外扣 1 点心情；读数 30 秒过期，不外推关机环境。CLI 使用 `climate 31 90`、`climate-off`、`climate-loss`；动画预览新增四种环境。规则与验证见[成长模块 G6](docs/04-growth-game.md#14-g6-温湿度成长联动2026-11-11)。

A1：新增 I²S 音频 HAL、PCM 转换/音量/电平、有限 FIFO 与一秒录放音诊断。默认不开麦，诊断需显式编译启用；真实音频验收待做。见[音频模块 A1](docs/01-ai-agent.md#13-a1-音频基础软件切片2026-11-11)。

A2：新增端点与会话状态机，支持 300 ms 静音端点、15 s 上限、手动打断、静音和旧轮次隔离。运行 `./tools/simulate_voice.sh` 可用合成 PCM 与注入事件演练；未绑定 BOOT、未接入唤醒模型或云端。见[会话模块 A2](docs/01-ai-agent.md#14-a2-端点与会话状态机2026-11-11)。

A3：新增单一拥有者 runtime 边界和 WakeNet/VAD 适配接口，固定音频/传输动作顺序与失败回送；此层同步执行端口，任务由 A4 持有。见[运行边界 A3](docs/01-ai-agent.md#15-a3-运行拥有者与前端适配边界2026-11-12)。

A4：新增 FreeRTOS 拥有者服务、控制/播放队列及 HAL 流式采集/播放，支持短读短写、同毫秒帧暂存、打断、静音、超时、真实 drain 门控与故障锁定。应用接入前必须提供传输端口，当前不自动启动。见[任务执行 A4](docs/01-ai-agent.md#16-a4-拥有者任务与-hal-流式执行2026-11-12)。

A5：新增小智 WebSocket v1 协议与 Opus 适配库，20/60 ms 帧转换、尾包补齐、播放背压与单轮连接隔离已有主机验证；真实 SDK 入口已通过 ESP32-S3 强制链接检查。见[协议与编解码 A5](docs/01-ai-agent.md#17-a5-小智-v1-协议与-opus-适配库2026-11-12)。

A6：新增独立 WSS/codec 任务，向 A4 提供有限队列、分片重组、超时、失败回送和每轮新连接。`CONFIG_PET_AI_XIAOZHI_ON_BOOT` 默认关闭，显式开启后读取 NVS 并组合 A4/WSS，未启用模型时需主动 PTT；BOOT/UI 接线见 A7，模型接入见 A8。见[网络接线 A6](docs/01-ai-agent.md#18-a6-wss-网络拥有者与-a4-接线2026-11-12)。

A7：现有 BOOT 菜单增加按住说话、静音和免打扰；AI 状态页使用采集电平快照，critical 抢占会取消对话。控制队满不再丢失静音/取消，策略变化不恢复旧会话。见[输入、状态与隐私 A7](docs/01-ai-agent.md#19-a7-boot-输入ai-状态与隐私仲裁2026-11-12)。

A8：接入锁定 ESP-SR 的“小爱同学”与模型 VAD，增加模型包/分区/SHA256 检查、拥有者生命周期及失败锁定。默认关闭；显式启用后本地监听可见，云端轮次由唤醒或 PTT 触发，保留半双工与静音/DND 门控。见[模型接入 A8](docs/01-ai-agent.md#20-a8-wakenet-与模型-vad-接入2026-11-12)。
