# extensions

可插拔扩展包。扩展消费核心事件与意图，不反向修改核心模块。

## 目录

- `device_control/`：手机/电脑控制的能力注册与适配器。
- `servo/`：舵机执行器、指令解析、序列播放与安全限制。

## 契约

- 扩展只通过 `EVT_INTENT_READY` 接收意图，通过结果事件回报。
- 核心模块不 `#include` 扩展头；扩展依赖 `core/contracts`。
- 编译期开关：`CONFIG_EXT_DEVICE_CONTROL`、`CONFIG_EXT_SERVO`。
- 每个扩展必须有 mock 适配器与主机契约测试。
- 新增扩展不得修改 `ai_agent` 状态机或消息信封。

## 文档

- [`../docs/05-extension-device-control.md`](../docs/05-extension-device-control.md)
- [`../docs/06-extension-servo.md`](../docs/06-extension-servo.md)
