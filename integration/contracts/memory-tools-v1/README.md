# 公开记忆工具 v1

作用域：CORE `/mcp/memory` 和 `nexus-core/tools/memory_mcp_stdio.py`；不是设备 WSS 中的 `self.*` 工具，也不改 MQTT 管理契约。固定协议基线为 MCP `2025-06-18`，采用无会话的 Streamable HTTP JSON 响应及 stdio 适配。真实供应商的版本协商和工具采用情况仍需联调。

## 鉴权与传输

- 开关 `NEXUS_MEMORY_MCP_ENABLED=false` 默认关闭，关闭时路径返回 404。开启须配置独立 `NEXUS_MEMORY_MCP_TOKEN`，32–256 个可见 ASCII 字符；空值、空白或复用管理员令牌会阻止启动。实际检索还需配置 Halo 和 OpenViking。
- `Authorization: Bearer <memory-mcp-token>` 只允许精确路径 `/mcp/memory`。管理员令牌不能进入此路径，工具令牌不能访问 `/api/v1/*`。没有 OAuth、短期令牌、按设备授权或多用户隔离；作用域是当前 CORE 实例的固定项目目录。
- 不接收浏览器调用：鉴权通过后任何 `Origin` 都返回 403，不开放 CORS。接口响应 `Cache-Control: no-store`；凭据不得放进查询参数。
- POST 使用 UTF-8 JSON，`Content-Type: application/json`，`Accept: application/json, text/event-stream`。请求体不超过 32768 B，严格拒绝重复字段、尾随 JSON、批量调用及超过 12 层嵌套的输入。
- 初始化可不带协议头；后续请求带 `MCP-Protocol-Version: 2025-06-18`。不支持的头或缺少后续协议头返回 400。初始化会返回服务器支持的固定版本，客户端不支持时应断开。
- 不发放 session ID，不提供 SSE；GET 返回 405。每个调用独立鉴权，不把初始化或 RPC ID 当作权限凭据。

## 方法与消息

1. `initialize`，包含 `protocolVersion/capabilities/clientInfo`；响应只声明 `tools`，`listChanged=false`。
2. `notifications/initialized`，HTTP 202 且无正文。
3. `tools/list`，只返回 `memory.search`，没有分页游标。
4. `tools/call`，只允许下方参数。另支持无参数 `ping`。

```json
{"jsonrpc":"2.0","id":"voice-search-1","method":"tools/call","params":{"name":"memory.search","arguments":{"query":"上次关于专注提醒的公开评论","limit":3}}}
```

`id` 接受最多 80 字符的字符串或 64 位整数（包含 0），不接受 null、布尔或小数；通知不得执行工具调用。`query` 是非空白字符串，最多 256 字符；`limit` 默认 3，范围 1–5 的整数。不能指定账号、peer、URI、设备或任何额外参数。未知方法返回 JSON-RPC `-32601`，未知工具或参数返回 `-32602`；无效 JSON 为 `-32700`，无效请求为 `-32600`。合法请求的 RPC 错误使用 HTTP 200；解析/信封错误为 400，媒体类型/Accept、体积和鉴权错误使用对应 HTTP 状态。

成功结果包含 `isError=false`、`structuredContent`，以及序列化相同结果的 `content[0]={type:"text",text:"..."}`。结果对象包含 `items/partial/dataPolicy`，每条命中为 `messageId/content/summary/originalUrl/verifiedAt`；字段、限额和源复核规则沿用 [15](../../../docs/15-memory-lifecycle.md)。查无结果与部分失败不能回答为“记忆证明了某事”；返回评论或摘要也不授予工具执行权限。

业务故障使用 `isError=true` 的文本内容：`memory_not_configured`、`memory_search_busy` 或 `memory_search_unavailable`。不透传上游正文、异常栈、URI 或密钥。并发限制与管理员检索共用每实例 2 个名额及 12 秒远端预算，不是独立资源池，也不保证整个 HTTP 调用的硬实时上限。

只有显式保存且当前仍可复核的公开评论能命中，不能读取私人语音历史。源评论确认撤回、转为非公开或改文时，检索会触发已经批准的副本失效/删除策略，因此工具声明 `readOnlyHint=false/destructiveHint=true`；没有显式保存、任意写入或任意删除工具。

## stdio 适配

stdio 进程从 `NEXUS_MEMORY_MCP_URL` 和 `NEXUS_MEMORY_MCP_TOKEN` 读取配置；URL 默认本机回环 `/mcp/memory`，远端必须使用 HTTPS，禁止 URL 用户名/密码、查询参数和重定向。忽略环境 HTTP 代理，使用默认 TLS 证书验证。进程只在标准输出写逐行 JSON-RPC；固定配置错误写标准错误并以 2 退出，不输出凭据或上游错误。

适配器先完成初始化，再转发后续调用；32 KiB 输入、64 KiB HTTP 响应、连接/读操作超时 20 秒，顺序处理，不自动重试或重放。该超时不是整个请求的硬实时期限。未初始化返回 `-32002`，HTTP/连接/响应故障返回固定 `-32000 memory_endpoint_unavailable`。调用超时或关闭 stdio 不会保证取消已经到达 CORE 的核验/维护事务；不提供取消、恢复流或自动协议降级。

接口和真实验收步骤见 [16](../../../docs/16-voice-memory-tools.md)，固定桥接、受控启动与本地 TLS 全链回归见 [17](../../../docs/17-voice-bridge-regression.md)。断线或 stdio 退出后的新子进程必须重新初始化；不恢复旧会话、不重放旧查询。协议依据：[MCP transport](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports)、[lifecycle](https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle)、[tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools)。这些是明确支持的子集，未宣称通用 MCP、OAuth 或所有小智服务兼容。
