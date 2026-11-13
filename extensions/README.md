# extensions

可插拔扩展包。扩展消费核心事件与意图，不反向修改核心模块。

## 目录

- `device_control/`：手机/电脑控制的能力注册与适配器（S6 实现）。
- `servo/`：舵机执行器、指令解析、序列播放与安全限制（S6 实现）。

每个扩展目录固定结构：

```text
extensions/<name>/
├── extension.json        # id、version、contractVersion、capabilities、risk、defaultEnabled
├── CMakeLists.txt
├── include/pet_ext/<name>.h
├── src/<name>.c
├── test/host/test_<name>_contract.c
└── README.md
```

## 契约

- `extension.json` 由 `config/schemas/extension.schema.json` 校验；核心契约版本在 `firmware/components/contracts/include/pet/extension.h`，两者不一致时 `check_contracts.py` 失败。
- 组合根注册结构化 MCP 路由并注入平台端口；调用扩展公开 API，结果通过快照回送。AI 会话引擎与消息服务不依赖扩展内部实现。
- 编译期开关：`CONFIG_PET_EXT_DEVICE_CONTROL`、`CONFIG_PET_EXT_SERVO`，默认关闭；关闭时组合根不引用扩展，二进制不出现 `*_init` 符号。
- 新增扩展需要：manifest、职责清晰的公开 API、组件 CMake、主机清单测试、Kconfig 开关，且不修改 `ai_agent` 状态机或消息信封。

## 校验

```bash
python3 tools/ci/check_contracts.py   # manifest、契约版本、目录结构、Kconfig 开关
tools/ci/test_host.sh                 # 每个扩展的清单契约测试
```

## 文档

- [`../docs/05-extension-device-control.md`](../docs/05-extension-device-control.md)
- [`../docs/06-extension-servo.md`](../docs/06-extension-servo.md)
