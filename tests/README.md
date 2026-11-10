# tests

跨模块契约与集成测试。

## 分层

- 主机测试：纯逻辑、状态机、解析器、成长公式、CRC、配置 schema。
- 契约测试：MQTT 信封、HTTP Webhook、设备控制意图、Bridge 白名单。
- 集成测试：服务端 + Broker + mock 设备；不替代 HIL。
- HIL：真实屏幕、音频、传感器、断网、掉电、24 h/7 d。

## 规则

- 先写失败反馈，再实现；测试必须能在无硬件环境运行（除 HIL）。
- 不把主机测试通过描述为硬件验收通过。
- 测试不读取真实凭据；使用临时配置与 mock。
- 详细验收项见 [`../docs/07-roadmap-acceptance.md`](../docs/07-roadmap-acceptance.md)。
