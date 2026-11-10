# bridge

本机工具桥。首版复用工作区现有 `mac-bridge`，不在本目录复制实现。

## 职责

- 固定白名单工具：`system.info`、`project.status`、需要确认的 `app.open`。
- 只监听 `127.0.0.1`；Bearer Token；Host/Origin 校验；SQLite 审计。
- 风险操作需要 CORE 签发的 HMAC 确认，绑定请求、管理员、工具、参数与有效期。

## 边界

- 不提供任意 shell、脚本、文件删除、任意路径或对外发信。
- 不把令牌写入日志或命令参数。
- 详细接口见 [`../../mac-bridge/README.md`](../../mac-bridge/README.md) 与 [`../docs/05-extension-device-control.md`](../docs/05-extension-device-control.md)。
