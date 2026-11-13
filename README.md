<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/brand/qitomo-logo-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/brand/qitomo-logo.svg">
    <img src="docs/assets/brand/qitomo-logo.svg" width="420" alt="栖团 Qitomo：猫耳与对话气泡组成的薄荷绿标志">
  </picture>
</p>

# 栖团 Qitomo

**会回应、会成长的桌面伙伴。**

栖团是一款基于 ESP32-S3 的实体 AI 桌面宠物，将语音对话、消息收件箱、环境感知和养成互动放进一块小屏幕。它用表情回应交流，接收来自服务端的消息，也会随着照料、对话和专注积累成长。

设备的设计目标是插电联网后独立运行；Mac 用于开发，可选的电脑控制由 Mac Bridge 扩展提供。语音由设备直接连接小智服务，消息与远程管理由独立的 `nexus-core` 承担。

> 当前为开发版本 `0.4.0-dev`。核心软件已有主机、构建及隔离联调证据；真实云端、生产部署与整机验收尚未完成，`04` 工程暂未烧录。普通 AI 会话仍为半双工，语音开机服务及敏感扩展需显式启用。当前状态以[需求与任务台账](docs/18-requirements-and-progress.md)为准。

## 可以做什么

| 能力 | 已有软件实现 | 使用条件与边界 |
| --- | --- | --- |
| 语音交流 | 唤醒/VAD、按住说话、WSS/Opus、对话文字与表情、上下文及工具接口 | 需要小智服务、设备凭据和显式开关；真实声音、延迟与声学打断待验收 |
| 桌面收件箱 | 消息卡片、优先级、去重、离线持久化、展示/已读回执、免打扰 | 需要 CORE 与 MQTT/TLS；真实投递和掉电链路待验收 |
| 环境感知 | DHT11 温湿度、60 分钟趋势、告警与冷/热/干/湿表现 | 需要实际接线；传感器误差与长期采集待验收 |
| 陪伴成长 | 喂食、摸头、玩耍、睡眠、每日任务、成就、对话与专注奖励 | 成长逻辑可离线演练；物理存档掉电与 7 天体验待验收 |
| 可选扩展 | Mac 白名单工具、舵机限位/急停、远程配置、签名 OTA 与恢复 | 分别按开关启用；Mac 控制、机械校准与实机 OTA 待验收 |

成长界面已有八种原生 LVGL 状态动画。品牌 Logo 提炼自当前猫形角色的猫耳与薄荷绿配色，资源位于[品牌资产目录](docs/assets/brand/README.md)。

## 从这里开始

| 想了解什么 | 阅读入口 |
| --- | --- |
| 项目定位、场景与系统组成 | [项目介绍](docs/24-project-introduction.md) |
| 名称含义、Logo 与使用规范 | [品牌说明](docs/25-brand-identity.md) |
| 无设备体验与开发检查 | [本地上手](docs/26-getting-started.md) |
| 当前完成度和剩余验收 | [需求与任务台账](docs/18-requirements-and-progress.md) |
| 架构、功能设计和历史交付 | [文档索引](docs/README.md) |
| 服务器预检、离线交付与部署 | [部署说明](deployment/README.md) |

在仓库根目录运行以下命令，可先体验真实成长规则，无需开发板、网络或云端凭据：

```sh
bash tools/simulate_growth.sh
```

输入 `feed`、`pet`、`play`、`sleep`、`wait 3600`、`wake`，用 `status` 查看数值，`quit` 退出。桌面存档保存在忽略的构建目录中。语音状态机和真实 LVGL 动画的本地入口见[上手文档](docs/26-getting-started.md)。

## 系统与仓库边界

| 部分 | 职责 |
| --- | --- |
| 本仓库 `04-ai-desktop-pet` | ESP32 固件、屏幕表现、设备端业务、集成契约与交付工具 |
| 独立仓库 `nexus-core` | 消息接入、MQTT 投递、管理台、设备配置、OTA 管理和可选公开记忆工具 |
| 小智服务 | 设备直连的云端语音会话；CORE 不代理音频 |
| 独立仓库 `mac-bridge` | 可选的 Mac 白名单执行能力；Mac 不作为设备常驻运行依赖 |
| `02-desktop-pet` / `03-xiaozhi-voice` | 独立 donor 工程，提供既有玩法、板型与语音经验 |

`server/ai-gateway/` 为自建语音网关预留，当前没有替代小智服务。品牌名称使用“栖团 Qitomo”，工程目录、`nexus_pet` 构建标识、MQTT/NVS 命名及独立仓库名称沿用现有技术标识，映射见[品牌说明](docs/25-brand-identity.md)。

## 目录

```text
04-ai-desktop-pet/
├── docs/                 产品介绍、架构、验收与任务台账
│   └── assets/brand/     SVG Logo、PNG 图标与品牌展示图
├── firmware/             ESP-IDF 固件、板型与主机测试
│   ├── main/             应用组合根与启动编排
│   ├── components/       契约、HAL、业务服务、表现与 LVGL
│   └── boards/           板级 board.json，引脚配置事实源
├── config/               JSON Schema、非敏感示例与 profile
├── integration/          与 CORE 的版本化契约及联调入口
├── extensions/           设备控制、舵机扩展
├── resources/            运行时字体与资源；品牌素材不打入固件
├── deployment/           服务端交付、TLS 与恢复模板
├── server/               Mac 控制薄网关及 AI 网关预留
├── bridge/               Mac Bridge 集成说明
├── tools/                构建、模拟、配置与诊断工具
└── tests/                跨模块契约与集成测试说明
```

固件以 C17 / ESP-IDF 6.1 / LVGL 9.x 实现，CORE 使用 Java 21 / Spring Boot / PostgreSQL / Vue 3。`main` 负责组合，业务、HAL 与 UI 分层；跨模块使用版本化契约、事件和明确的公开服务接口，详见[总体架构](docs/00-architecture.md)。

## 硬件基线

当前板型为 `dodo-s3-lcd`：ESP32-S3 N16R8（16 MB Flash、8 MB PSRAM）、240×320 ST7789 屏、INMP441 麦克风、MAX98357A 功放与扬声器、DHT11 温湿度传感器。舵机为可选扩展，需要独立供电和校准。

实际引脚以 [board.json](firmware/boards/dodo-s3-lcd/board.json) 为准。`02`、`03` 与本工程使用独立版本和存档/分区方案；首次上板前需按[维护与迁移文档](docs/10-maintenance-recovery.md)核对，当前流程只构建、不烧录或擦除。

## 开发与交付

新增功能优先验证公开行为，沿用现有主机、真实 LVGL、Java/HTTP/PostgreSQL/MQTT 与 CLI 检查。每项同步实现、契约、说明与台账，分别记录本地软件、远程 CI、部署和实机证据；详细入口见[本地上手](docs/26-getting-started.md)与[项目交付约定](AGENTS.md)。

各仓库独立版本化，依赖锁文件随源码提交，发布以 tag 标记，契约变更单独升版本。跨仓复用通过版本化契约或抽取组件完成，保持 `02` / `03` donor 独立。

凭据、私有 env、NVS/崩溃备份、生成固件和服务端发布包不进入 Git。首版不做本地大模型、任意 shell、多设备集群或自动旧 NVS 迁移；完整产品验收标准保留在[路线与验收](docs/07-roadmap-acceptance.md)。
