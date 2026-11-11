# 04 AI 超级桌面宠物（AI Desktop Pet）

状态：**首个纵向切片阶段（S1–S3 已完成，S4 温湿度切片、S5-A 消息中心纯逻辑与 S5-B Wi-Fi/MQTT/TLS 传输层代码完成，实机待验证）**。已有可构建固件、组件边界、配置/消息/扩展 schema 与生成契约，以及温湿度采样到卡片快照、消息校验到优先级队列、Wi-Fi/MQTT/TLS 连接与重连的完整逻辑链路；所有功能方案先评审，确认后再写代码。目录与构建边界的评审结论见 [`docs/08-structure-review.md`](docs/08-structure-review.md)。

## 目标

在 ESP32-S3 桌面宠物上统一承载五类能力：

1. AI 智能体语音对话（听、想、说、表情与动作）。
2. 万能信息接收端（第三方消息推送、在线配置、屏幕渲染）。
3. 温湿度传感器接入与可视化。
4. 养成系游戏（成长数值、互动、存档）。
5. 可插拔扩展：语音控制手机/电脑、语音控制舵机等硬件。

设计约束：模块间只通过事件总线和版本化契约通信；HAL 与业务分离；新增能力以扩展包接入，不修改核心模块。

## 文档入口

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

## 仓库与版本

本工程使用独立 Git 仓库，与 `02`、`03`、`nexus-core`、`mac-bridge` 分开版本化：

- 架构与模块文档随代码放在 `docs/`，保证设计、实现和发布记录可原子提交。
- 构建依赖必须提交锁文件；发布以 tag 标记，契约变更单独升版本。
- 跨仓库复用通过版本化契约或组件抽取完成，不在本仓库直接复制其他工程源码。

## 目录结构

```text
04-ai-desktop-pet/
├── README.md                     项目入口与目录说明
├── docs/                         架构与模块方案（00–08）
├── firmware/                     ESP32-S3 固件工程（ESP-IDF）
│   ├── main/                     组合根：app_main、启动编排、诊断
│   ├── components/               ESP-IDF 组件（只暴露 include/）
│   │   ├── contracts/            跨模块契约（纯 C，可主机测试）
│   │   ├── core/                 事件总线与基础服务
│   │   ├── pet_hal/              HAL 接口与 fake 实现
│   │   ├── storage/              NVS 薄封装
│   │   ├── svc_climate/          温湿度采集
│   │   ├── svc_message_hub/      消息校验、优先级队列与去重
│   │   ├── svc_net/              Wi-Fi + MQTT/TLS 传输层
│   │   └── svc_growth/           成长模块骨架（Kconfig 开关控制）
│   ├── boards/dodo-s3-lcd/       板级 board.json（引脚/能力唯一事实源）
│   ├── partitions/               分区表与迁移说明
│   └── tests/host/               主机测试
├── server/                       独立 AI 网关（消息中心归 nexus-core）
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
│   └── i18n/                     中英文文案
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
| `nexus-core` | 消息中心：Halo/Webhook 接入、消息去重、MQTT/TLS 与设备认证、配置下发、管理台；签发短期 AI 会话令牌。 |
| `server/ai-gateway` | 独立 AI 会话服务：ASR/LLM/TTS 编排与供应商密钥；首版复用 03 小智服务，经 `ai_session` 契约隔离。 |
| `mac-bridge` | 手机/电脑控制的本机执行适配器，继续用固定白名单与确认机制。 |
| `docs/HARDWARE.md` | 硬件与分区事实来源；新工程重新定义分区，不复用 02/03 的分区表。 |

原则：先抽取可复用组件，再在新工程引用；禁止把 02/03 的源码整段复制成第二份实现。

## 硬件基线

- ESP32-S3 N16R8：16 MB Flash、8 MB PSRAM。
- 2.4 英寸 ST7789 240×320 SPI 屏。
- INMP441 I2S 麦克风、MAX98357A I2S 功放与扬声器。
- DHT11/KY-015 温湿度；37 合 1 KY 套件中的光敏、摇杆、触摸、蜂鸣器。
- 预留 1–2 路舵机扩展（独立 5V 供电）。

## 当前不做

- 不烧录、不改 02/03 现有工程、不迁移 NVS、不擦除 Flash。
- 不实现具体业务代码；先完成方案评审与接口确认。
- 不把未上板验证的主机测试结果描述为硬件验收通过。
