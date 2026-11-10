# device_control（扩展）

语音控制手机/电脑的扩展包。当前仅规划，未实现。

## 计划内容

- `include/command_adapter.h`：设备发现、执行、健康检查接口。
- `include/capability_registry.h`：能力注册、schema 校验、风险级别。
- `src/`：意图路由、确认令牌校验、审计适配。
- `README.md`：能力表、平台接入步骤、授权与安全边界。

## 约束

- L2 破坏性操作必须二次确认；L3（任意 shell/提权/格式化）不注册。
- 适配器只接受白名单能力与已校验参数。
- 详细方案见 [`../../docs/05-extension-device-control.md`](../../docs/05-extension-device-control.md)。
