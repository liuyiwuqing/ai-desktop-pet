# 语音公开记忆工具与接入（2026-11-12）

本页保留接口交付与该轮验证快照。2026-11-13 已补齐固定桥接、最小依赖、受控启动与本地 TLS 全链回归；当前桥接准备/运行及部署路径以 [17](17-voice-bridge-regression.md) 为准。

在长期记忆检索与副本生命周期的基础上，CORE 新增独立凭据保护的 MCP 入口，以及标准库 stdio 适配器。已验证本地进程→真实 CORE HTTP→隔离 PostgreSQL 与上游 HTTP 夹具的调用链；没有修改真实配置、启动常驻桥接、迁移真实库或连接语音云端。

## 当前可用范围

语音侧只获得 `memory.search`，检索用户显式保存、源评论仍公开且正文哈希一致的内容。它不是私人语音聊天记忆，不会自动保存新对话或扩大公开数据范围。工具返回的评论和摘要均是数据，摘要仍为 AI 推断；供应商是否调用、是否遵守数据边界，须通过真实联调确认。

```text
ESP32 ◄── 语音 WSS ──► 小智服务
                          │ MCP 接入点（供应商提供）
                          ▼
                  官方 mcp_pipe.py（常在线服务器）
                          │ stdio
                  memory_mcp_stdio.py
                          │ HTTP 回环 / 远端 HTTPS，独立受限令牌
                      nexus-core
                          ├─ OpenViking：固定目录候选
                          └─ Halo：当前公开状态与哈希核验
```

CORE 不转发音频，设备不持有 CORE 管理令牌、Halo 密钥或 OpenViking 密钥。基础语音仍由 ESP32 直接连接供应商；增强记忆链路需要 CORE 和桥接所在服务器在线，日常可部署在 Linux，不依赖开发用 Mac。失效、删除重试、来源不可核验及历史身份隔离沿用 [15](15-memory-lifecycle.md)，并未因语音入口放宽。

精确接口、版本、请求/响应及故障码见[公开记忆工具 v1](../integration/contracts/memory-tools-v1/README.md)。只暴露检索能力，但检索可按既有政策排队清理已失效副本，所以工具没有声明为无副作用的纯只读。

## 配置与部署准备

1. 先完成 [CORE 部署与 V10 升级预检](../deployment/README.md)：备份/独立恢复验证、可信 Halo 来源、OpenViking 固定目录与身份权限；管理台可显式保存并查到一条当前公开评论。默认关闭状态保持不变，本轮没有填写真实 env。
2. 在 CORE 私有 env 设置 `NEXUS_MEMORY_MCP_ENABLED=true`，填写独立的 `NEXUS_MEMORY_MCP_TOKEN`。它须为 32–256 个可见 ASCII 字符，不能与管理员令牌相同；使用独立随机凭据，不复制服务端密钥。此开关不会代替 `NEXUS_MEMORY_ENABLED` 及 Halo 配置。
3. 将当前 `nexus-core/tools/memory_mcp_stdio.py` 作为受版本管理的发布文件复制到 `/opt/nexus/core-tools/`。它只依赖 Python 3 标准库。CORE JAR 与该脚本配套保留，令牌或 URL 不放在命令行参数中。
4. 使用 [17](17-voice-bridge-regression.md) 的固定源/依赖和准备入口生成 `/opt/nexus/memory-voice/`，由受控启动器运行指定 stdio 脚本；不要另行安装未锁定的全量依赖。2026-11-12 当时只做源码对照，2026-11-13 已运行实际桥接的本地 TLS 回归。若供应商只支持旧协议，需要先确认版本兼容，不能用关闭鉴权或放宽数据范围补救。
5. 从供应商取得专用 MCP WSS 接入点，填写 [memory-voice.env.example](../deployment/memory-voice.env.example) 对应的 `/etc/nexus/memory-voice.env`，权限 600。只包含工具令牌、CORE URL 和 `MCP_ENDPOINT`，不要向桥接加载整个 CORE env。接入点 URL 可能含令牌，也需保护；不要启用记录 RPC 正文的第三方 debug 日志。
6. 核对 [nexus-memory-voice.service](../deployment/nexus-memory-voice.service) 中专用 `nexus-mcp` 用户、Python/脚本路径和网络。模板与 CORE 在同一 Linux 主机，工具端走回环 HTTP；远端 CORE 必须配可信 HTTPS。stdio 适配器不读取环境代理、不跟随重定向，网络代理需求应在部署方案中明确。

在目标机执行 `systemd-analyze verify deployment/nexus-memory-voice.service`，核对配置后再由部署者安装启用。2026-11-12 本轮没有安装用户、服务、第三方依赖或申请 TLS 证书；2026-11-13 的本地独立依赖与桥接验证见 [17](17-voice-bridge-regression.md)，仍未执行 systemd 验收。桥接服务会启动子进程，停止时应终止整个控制组，模板保留 `KillMode=control-group`。

## 真实联调验收

按以下顺序记录脱敏结果，不保存令牌、接入点完整 URL 或评论全文日志：

1. 开关关闭时接口 404；开启后无令牌/管理员令牌访问 MCP 均为 401，受限令牌访问管理 API 为 401。带浏览器 Origin 为 403。确认管理入口仍使用自己的管理员凭据。
2. 运行 stdio 适配器或供应商桥接，确认 `initialize → notifications/initialized → tools/list`，协商为支持的版本，且只发现 `memory.search`。官方桥接在线不等于协议、工具或记忆已生效。
3. 在测试数据上显式保存一条公开评论，用语音提出相关问题，核对供应商发出的 `tools/call`、CORE 返回的已核验内容和最终回答；没有有效结果时应说明未找到，不能把 AI 摘要当成事实或声称记住了私人聊天。
4. 用隔离源数据验证转为非公开/改文后不再返回结果，删除失败保留队列并重试；源接口故障则隐藏并给出部分结果提示，不能误删。确认失效规则后再对真实公开内容验收，不为了验收操作他人的评论。
5. 验证 CORE/OpenViking 不可用、工具超时、桥接断线重连；工具故障不应冒充查无结果或自动保存内容，基础语音仍须按供应商配置独立工作。协议错误、业务 `isError`、`partial` 和空结果要分别记录。

实际语音工具回调不经过 ESP32 的 `self.*` 注册表，固件 diagnostics 的 MCP 计数不能证明这条服务器工具链被调用。设备静音/清除本地上下文不会删除供应商已经接收的公开评论工具结果；供应商会话历史由其能力和策略单独管理。

## 停用、轮换与故障定位

- 停止记忆增强：先停 `nexus-memory-voice`，再关闭 CORE 的 MCP 开关并重启 CORE。若凭据已泄露，立即关闭入口或轮换；单纯停桥接不能撤销已泄露令牌的访问。
- 轮换工具令牌：当前只支持一个静态令牌，维护窗口内同时更新两份受保护 env 并重启 CORE/桥接；不支持双令牌无缝轮换。复用管理员令牌会使 CORE 启动失败。该令牌与 OpenViking 身份/副本所有权独立。
- 接口 404 查 CORE 开关及精确路径；401 查是否拿错令牌；403 查意外 Origin；400 查版本/请求形状；406 查 Accept。合法 RPC 中的 `memory_not_configured`、`memory_search_busy`、`memory_search_unavailable` 分别查配置、共享并发和源站/记忆服务健康。
- stdio `memory_endpoint_unavailable` 不含上游错误，先用受保护的本机诊断核对 HTTP/TLS/状态，不自动重发旧查询。`partial=true` 表示有结果无法核验，空结果不构成“不存在该评论”的证明。
- 不通过更换 OpenViking account/user/peer 或手改身份指纹解决旧副本隔离；沿用 V10 维护流程。关闭 MCP 不会停止已经启用的副本复核/删除后台任务，暂停该任务需要按既有记忆服务开关与维护流程处理。

## 本地验证与产物

| 检查 | 本轮结果 | 覆盖边界 |
| --- | --- | --- |
| CORE 标准 `python3 tools/run_local.py build` | 43 项，41 通过，2 条件跳过，0 失败/错误 | 含 Vue 类型/生产构建及 Maven verify；真实 HTTP/PostgreSQL/MQTT，上游 Halo/模型/OpenViking 为隔离 HTTP 夹具 |
| 公开记忆 MCP 流程 | 4 项通过 | 独立鉴权/管理员隔离、发现/源复核及失效、非法 RPC/参数/UTF-8、真实 stdio 子进程发现与检索 |
| 启动及默认开关 | 2 项通过 | 默认关闭；启用时缺失、复用或非法凭据阻止启动 |
| `python3 tools/ci/test_memory_mcp_stdio.py` | 6 项通过 | HTTP/stdio 进程、初始化与通知、配置、重定向/故障/异常/截断 HTTP 响应、体积及 Unicode 边界 |
| 契约/板级/依赖 | 通过 | 10 schemas、1 profile、8 examples、2 extensions；19 组件无环 |
| JAR 核对 | 通过 | MCP/鉴权类与当前编译文件一致，V10 一致；3 个前端文件集合及内容逐字节相同 |
| 固件输入/产物 | 与 [14](14-recovery-and-device-names.md) 四预设记录一致 | 源清单及每套 6 个产物大小/哈希核对；没有重新构建或烧录 |

两项跳过为真实兼容模型调用，以及需单独启用的 Java/C 管理回环，未计入通过数。本轮没有修改前端或固件；前轮 38 项浏览器、LVGL 与固件构建结果保留为历史证据，没有重复执行并冒充本轮验收。

独立鉴权用例先以 404 的失败结果复现缺少入口，再实现入口和权限边界后通过。stdio 的异常初始化响应用例复现了未经校验的数组响应导致进程异常退出；已在信任边界验证响应形状，改为固定错误，并验证 HTTP 声明长度与正文一致、协议异常固定报错；最终 6 项通过。

| 配套发布文件 | 字节 | SHA256 |
| --- | ---: | --- |
| `nexus-core/target/nexus-core-0.1.0-SNAPSHOT.jar` | 26,270,673 | `cbe51464099760e5c9a8043496e2bc7ee87627c9fae408606b9ee8812dd3caea` |
| `nexus-core/tools/memory_mcp_stdio.py` | 5,659 | `8ed3ce455764fc6c6b1c2ced920657add6358955e9bbf6812128edb3d3590b84` |

JAR 不包含第三方桥接或上述 stdio 脚本，部署时须同时保留配套文件。固件源清单指纹保持 `19d1452e…cc63b`。新入口没有新增数据库迁移，仍使用 V10 账本；不代表真实数据库已经升级。

截至本页快照，官方桥接尚未实际运行；其后已在本地隔离环境运行，见 [17](17-voice-bridge-regression.md)。供应商 WSS/版本/工具采用联调、真实 OpenViking/Halo 权限与删除、真实库迁移及设备语音验收仍未完成。没有执行部署、外部删除、烧录、提交或推送。跨端成长、凭据轮换、远程 OTA 管理及更多平台仍是独立后续范围。
