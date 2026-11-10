# AI 超级桌面宠物 · 方案索引

更新：2026-11-10。状态：规划评审中，未进入功能实现。

本目录是 `04-ai-desktop-pet` 的设计唯一入口。每个模块独立成文，包含技术选型、实现步骤、验收标准、异常边界和待确认项。建议先读 [08 结构评审](08-structure-review.md) 校正工程边界，再按 `00 → 07` 顺序逐篇确认；前一模块的接口契约确认后，再进入下一模块。

## 模块清单

| 文档 | 范围 | 状态 | 依赖 |
| --- | --- | --- | --- |
| [00-architecture](00-architecture.md) | 分层、目录、事件总线、配置、部署、技术栈总表 | 待评审 | 无 |
| [01-ai-agent](01-ai-agent.md) | 语音输入/输出、上下文、AI 选型、表情与动作 | 待评审 | 00 |
| [02-message-hub](02-message-hub.md) | 接收端选型、配置面板、推送接口、渲染 | 待评审 | 00、01（表情联动） |
| [03-climate-sensor](03-climate-sensor.md) | 温湿度硬件、采集、可视化、异常 | 待评审 | 00 |
| [04-growth-game](04-growth-game.md) | 成长数值、互动、状态表现、存档 | 待评审 | 00、03（环境联动） |
| [05-extension-device-control](05-extension-device-control.md) | 手机/电脑控制接口与前置架构 | 待评审 | 00、01（意图） |
| [06-extension-servo](06-extension-servo.md) | 舵机通信、指令解析、安全限制 | 待评审 | 00、01（意图） |
| [07-roadmap-acceptance](07-roadmap-acceptance.md) | 阶段拆分、依赖、总体验收 | 待评审 | 全部 |
| [08-structure-review](08-structure-review.md) | 目录结构优缺点、生产级原则、迁移计划与待确认决策 | 评审中 | 全部（评审前置） |

## 评审方式

每个模块按以下顺序确认，避免边写边改：

1. **范围**：本模块做什么、不做什么。
2. **选型**：对比方案、推荐项、被放弃方案的原因。
3. **契约**：接口、数据结构、事件、错误码、版本策略。
4. **步骤**：可独立验收的实现切片。
5. **验收**：主机测试、目标测试、实机指标。
6. **风险**：已知限制、待实物确认项。

确认后在对应文档顶部标记 `已确认（日期）`，再进入编码。未确认的模块只允许做接口草案，不写业务实现。

## 统一约定

- 固件语言：C17，ESP-IDF 6.1；UI 用 LVGL 9.x；构建用 CMake/idf.py。
- 服务端：复用 Java 21 / Spring Boot / PostgreSQL / Flyway / Vue 3 的 `nexus-core`，不另起一套同构服务。
- 设备与服务端契约：JSON + 显式 `schemaVersion`；设备侧用 cJSON，服务端用 Jackson。
- 配置分三层：编译期板型配置、运行期非敏感配置、凭据/密钥。凭据只进 NVS 或服务端私有 env，不写文档、不进日志。
- 事件命名：`<module>.<event>`，例如 `ai.state_changed`、`message.received`、`climate.alert`。
- 所有跨模块调用必须经过 `core/contracts` 或事件总线；模块内部函数不对外暴露。
- 先写失败反馈和状态机测试，再做硬件接线；主机测试不能替代实机验收。
