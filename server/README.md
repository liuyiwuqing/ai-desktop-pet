# server

AI 首版继续复用小智服务，`ai-gateway/` 是后续自建网关预留，不复制 ASR/LLM/TTS；消息、注册、管理台仍由 `nexus-core` 负责。

小智协议与上下文/工具联调见 [12](../docs/12-cloud-interop.md)。CORE 常驻服务、TLS 代理与升级恢复模板见 [部署准备](../deployment/README.md)；当前只完成本地验证，尚未部署。

## Mac 控制薄网关

`control_gateway.py` 仅把设备受限请求交给现有 `mac-bridge`，自身不运行 shell/启动应用。Python 标准库实现，监听 `127.0.0.1:8092`，必须由 TLS 反向代理暴露；Bridge 保持 loopback，禁止直接暴露 Bridge 端口。

私有配置文件权限 `600`，不入库，字段如下：

- `bridgeUrl`：固定 loopback HTTP 根，默认 `http://127.0.0.1:8091`。
- `bridgeToken`：现有 Bridge 的至少 32 字符令牌。
- `devices`：键是 deviceId，值包含各设备独立 `token`，以及 `applications`/`projects` 白名单 ID 数组；每组最多 16 项。

```bash
python3 server/control_gateway.py --config <private-config.json> --database <private-audit.sqlite> --port 8092
```

数据库自动以 `600` 创建。[控制代理模板](../deployment/Caddyfile.control)仅转发 POST `/v1/execute` 和合法 GET `/v1/results/<deviceId>/<requestId>`，保留 Authorization，请求体限 2048 字节、每路由上游连接最多 8，禁用代理重试。模板没有内置分布式速率限制，公网入口另需配置每设备速率/并发配额。TLS 证书须被设备信任；设备 control/api_uri 指向该 HTTPS 根，设备和 Gateway 白名单同时匹配才能执行。

POST `/v1/execute` 严格接受 deviceId/requestId/toolId/arguments/approved/expiresAt。system.info 无参数，project.status 仅 projectId，app.open 仅 applicationId 且须 approved。approved 代表受信设备软件已获得本地确认，不能作为不受信客户端的通用授权入口。短期到期窗口不超过 60 秒。GET `/v1/results/<deviceId>/<requestId>` 只查询同设备结果。

网关先在 SQLite 预留 UNKNOWN，再映射稳定 UUID 调用 Bridge；app.open 使用 Bridge 已有 HMAC approval。超时/重启/重复请求不重放动作；UNKNOWN 只能 GET 查询确认。审计保存 ID、能力、参数摘要和清洗结果 30 天，保留最高序号防历史清理后重放，不记录令牌、路径或 stdout。备份/迁移网关数据库时应同时保护重放水位。

`python3 tools/ci/test_control_gateway.py` 用公开 Gateway API 和 Bridge 边界替身验证确认、作用域、重启去重和 UNKNOWN 查询；未实际部署 TLS 代理，也未操作真实 Mac。
