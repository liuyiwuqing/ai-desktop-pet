# 栖团 Qitomo · 文档索引

首次了解项目可从[项目介绍](24-project-introduction.md)、[品牌与 Logo](25-brand-identity.md)、[本地上手](26-getting-started.md)开始。产品名使用“栖团 Qitomo”；现有 NEXUS 技术标识和协议沿用原名，映射见品牌说明。

当前需求、已完成任务和后续顺序统一见 [18 · 需求与任务台账](18-requirements-and-progress.md)，每项工作按“实现、验证、文档、提交、推送”闭环推进。

更新：2026-11-13。最新语音记忆桥接准备与本地 TLS 全链回归见 [17](17-voice-bridge-regression.md)，公开记忆 MCP/stdio 接口与运维见 [16](16-voice-memory-tools.md)，长期记忆检索与副本失效见 [15](15-memory-lifecycle.md)，屏幕/BOOT 恢复与设备更名见 [14](14-recovery-and-device-names.md)，此前运行维护实现见 [13](13-operational-software.md)；设备首版软件见 [09](09-software-delivery.md)，NEXUS 管理闭环、上下文/工具回环及部署模板见 [11](11-product-software-closure.md)。维护见 [10](10-maintenance-recovery.md)，真实云端联调步骤见 [12](12-cloud-interop.md)，部署步骤见 [部署准备](../deployment/README.md)。历史切片说明保留，不能把早期“未接入”当作现状。暂不烧录，真实部署与硬件验收后置。

服务端已完成 CORE 版本保护、离线交付包、配置/主机预检和只读状态探测。当前 V12 配套包、实际 JAR 的 Linux 启动/重启与 admin 凭据热轮换证据见 [21](21-growth-sync.md#配套离线交付包)，通用命令见[使用说明](../deployment/server-release.md)；旧包记录仍保留在[部署交付记录](../deployment/README.md)。没有目标服务器，真实部署仍待后续。

本目录是 `04-ai-desktop-pet` 的设计与交付入口。先读 [00 当前架构](00-architecture.md)和最新交付记录，再按模块查阅接口、异常边界与验收项。[08 结构评审](08-structure-review.md)保留历史决策过程，不能作为当前完成度清单。

## 文档清单

| 文档 | 范围 | 状态 | 依赖 |
| --- | --- | --- | --- |
| [00-architecture](00-architecture.md) | 分层、目录、事件总线、配置、部署、技术栈总表 | S1–S3 已落地 | 无 |
| [01-ai-agent](01-ai-agent.md) | 语音输入/输出、上下文、AI 选型、表情与动作 | A1–A8 软件已实现，默认不启动 AI；本地唤醒/模型 VAD 可选接入，真实推理/云端/实机待验收 | 00 |
| [02-message-hub](02-message-hub.md) | 接收端选型、配置面板、推送接口、渲染 | 消息协议及远程管理隔离回环已落地，实机验收待做 | 00、01（表情联动） |
| [03-climate-sensor](03-climate-sensor.md) | 温湿度硬件、采集、可视化、异常 | 采样、成长消费、LVGL 数值/趋势/告警与重试已实现；实机待验收 | 00 |
| [04-growth-game](04-growth-game.md) | 成长数值、互动、状态表现、存档 | 成长/对话奖励/专注与受保护备份恢复已实现；实机待验收 | 00、03（环境联动） |
| [05-extension-device-control](05-extension-device-control.md) | 手机/电脑控制接口与前置架构 | 命令/确认/审计、Mac Bridge 网关已实现；默认关闭 | 00、01（意图） |
| [06-extension-servo](06-extension-servo.md) | 舵机通信、指令解析、安全限制 | 插值/序列/守卫/LEDC 已实现；默认关闭、需校准 | 00、01（意图） |
| [07-roadmap-acceptance](07-roadmap-acceptance.md) | 阶段拆分、依赖、总体验收 | 软件交付已记录，部署/实机待验收 | 全部 |
| [08-structure-review](08-structure-review.md) | 目录结构优缺点、生产级原则、迁移计划与待确认决策 | 历史结构评审与决策 | 全部（评审前置） |
| [09-software-delivery](09-software-delivery.md) | 设备首版交付与验证边界 | 历史交付记录，后续见 11 | 全部 |
| [10-maintenance-recovery](10-maintenance-recovery.md) | 物理配置、诊断、备份、OTA、02 迁移 | 可执行维护说明与只读迁移方案 | 09 |
| [11-product-software-closure](11-product-software-closure.md) | 远程管理、协议联调、部署与版本收口 | 管理闭环交付记录，后续见 13 | 09、10 |
| [12-cloud-interop](12-cloud-interop.md) | 小智上下文、工具协议与诊断 | 本地回环已验证，真实供应商待验收 | 01、11 |
| [13-operational-software](13-operational-software.md) | 消息保留、构建/CI、崩溃恢复与浏览器回归 | 运维软件交付快照，后续见 14 | 10、11、12 |
| [14-recovery-and-device-names](14-recovery-and-device-names.md) | 屏幕/BOOT 恢复与管理台设备更名 | 恢复与更名交付快照，后续见 15 | 10、13 |
| [15-memory-lifecycle](15-memory-lifecycle.md) | 长期记忆检索、源评论复核、持久化删除与管理页 | 生命周期交付快照，语音工具后续见 16 | 13、14 |
| [16-voice-memory-tools](16-voice-memory-tools.md) | 独立鉴权 MCP、stdio 适配、Linux 桥接及故障排查 | 接口交付快照，桥接回归后续见 17 | 12、15 |
| [17-voice-bridge-regression](17-voice-bridge-regression.md) | 固定桥接、受控启动、stdio 恢复与本地 TLS 全链回归 | 当前交付与验证记录，真实供应商待验收 | 12、15、16 |
| [18-requirements-and-progress](18-requirements-and-progress.md) | 当前需求、任务台账与交付边界 | 后续工作状态入口 | 全部 |
| [19-remote-ota](19-remote-ota.md) | 远程 OTA 协议、事务、管理 UI 与成套启用 | 软件已验证，生产签发与实机待验收 | 10、11 |
| [20-credential-rotation](20-credential-rotation.md) | CORE / 记忆工具双令牌、文件重载与交付预检 | 本地与隔离 Linux 已验证，真实运维待验收 | 13、16 |
| [21-growth-sync](21-growth-sync.md) | 设备成长只读镜像、顺序隔离与管理台 | 软件已验证，跨端奖励下发为二期 | 04、11 |
| [22-audio-duplex-diagnostic](22-audio-duplex-diagnostic.md) | 并行 HAL、有限诊断与隐私门控 | 普通 AI 仍半双工，声学验收待做 | 01、10 |
| [23-console-completion](23-console-completion.md) | 管理台完成度、测试发送重试与状态筛选 | 当前实现、验证与完整规划缺口 | 02、11 |
| [24-project-introduction](24-project-introduction.md) | 产品定位、使用场景、系统关系与阶段 | 项目介绍，完成度以 18 为准 | 00、18 |
| [25-brand-identity](25-brand-identity.md) | 栖团 Qitomo 命名、Logo、配色与技术名映射 | 品牌资产已交付；固件技术名沿用 | 24 |
| [26-getting-started](26-getting-started.md) | 成长、语音、LVGL 本地体验与开发检查 | 本机演练入口，硬件另行验收 | 01、04 |

当前发行硬件与可选模块、接线及菜单操作见 [27-hardware-and-input](27-hardware-and-input.md)。

手机热点配网、Wi-Fi-only 和小智官方绑定见 [28-device-web-setup](28-device-web-setup.md)。

设备设置项拓展、语音玩法、三按键交互、菜单体系、连续对话与创新功能的设计方案见 [30-interaction-feature-expansion-plan](30-interaction-feature-expansion-plan.md)；后续按切片实现并在 [18](18-requirements-and-progress.md) 记账，方案本身不是完成记录。

## 评审方式

每个模块按以下顺序确认，避免边写边改：

1. **范围**：本模块做什么、不做什么。
2. **选型**：对比方案、推荐项、被放弃方案的原因。
3. **契约**：接口、数据结构、事件、错误码、版本策略。
4. **步骤**：可独立验收的实现切片。
5. **验收**：主机测试、目标测试、实机指标。
6. **风险**：已知限制、待实物确认项。

尚未实施的模块继续按上述方式评审；已确认的 A/B/D 组决策与 S1–S5 切片见 `07`、`08`，不能再把整仓描述为“尚未进入实现”。

## 统一约定

- 固件语言：C17，ESP-IDF 6.1；UI 用 LVGL 9.x；构建用 CMake/idf.py。
- 服务端：复用 Java 21 / Spring Boot / PostgreSQL / Flyway / Vue 3 的 `nexus-core`，不另起一套同构服务。
- 设备与服务端契约：JSON + 显式 `schemaVersion`；设备侧用 cJSON，服务端用 Jackson。
- 配置分三层：编译期板型配置、运行期非敏感配置、凭据/密钥。凭据只进 NVS 或服务端私有 env，不写文档、不进日志。
- 事件命名：`<module>.<event>`，例如 `ai.state_changed`、`message.received`、`climate.alert`。
- 跨模块调用使用 `contracts`、事件或明确的公开服务 API；组合根注册工具与平台端口，模块内部函数不对外暴露。
- 先写失败反馈和状态机测试，再做硬件接线；主机测试不能替代实机验收。
