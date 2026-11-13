# 20 · CORE 与记忆工具凭据轮换

2026-11-13，SW-05。CORE 配套 `9559429` 已提交并推送；04 配套 `ccd3afa` 已推送。本项支持 CORE 管理 Bearer 和独立 `/mcp/memory` 凭据的短时双令牌过渡、过期、撤销与文件重载；stdio 在每次请求前读取自己的受限副本，现有连接不必重启。没有新增 HTTP 管理秘密的接口、数据库迁移或 MQTT 秘密下发。

设备 Wi-Fi/MQTT/语音供应商凭据仍使用物理完整配置；数据库、Broker、Halo、OpenViking、AI 和 Mac Bridge 的服务凭据继续按各自维护流程更新。本项不把这些凭据的重启更新计作平滑轮换。

## 模式与权限

未设置 `NEXUS_AUTH_TOKEN_FILE` 时，现有 `NEXUS_ADMIN_TOKEN` / `NEXUS_MEMORY_MCP_TOKEN` 环境模式保留；启用 MCP 时仍要求独立 32–256 个可见 ASCII 字符的受限凭据。

设置绝对路径后，CORE **仅**使用该文件的凭据，旧环境值不作后备。初次切换模式需要一次正常服务重启；后续更新无需重启。文件必须为无符号链接的普通文件，权限 `400` 或 `600`，所在目录不得组/其他人可写；建议专用目录 `700`，服务用户只读取自己的文件。CORE 与桥接分开目录/属主，桥接用户不得读取 CORE 文件。

| CORE 文件字段 | 规则 |
| --- | --- |
| format / revision | format=1；revision 为 1–2⁶³−1，更新严格增加 |
| adminActor | 固定 64 hex 管理身份；初始化取原管理员令牌 SHA256，保留既有工具任务归属，不随轮换改变 |
| admin.current / memory.current | `{id,token}`；memory 在 MCP 启用时必需，id 为 1–40 位安全 ID |
| admin.previous / memory.previous | 可选 `{id,token,expiresAt}`；最多保留一个旧值，UTC 秒到期自动失效，最长过渡 24 h |

所有槽位的 ID 和 token 不得重复，跨管理员/工具范围同样检查。未知或重复字段、非整数、过深/超长输入和非法权限拒绝。运行中倒退 revision、同 revision 改内容或改变 adminActor 也拒绝。该水位属于进程内保护，不是磁盘防回滚设施；重启前由运维核对当前修订，不以恢复旧秘密文件作为撤销的回滚。

每次授权读取完整文件：损坏、删除、权限错误或不安全更新返回固定 `503 authentication_configuration_unavailable`，不缓存旧令牌继续放行，不回退环境变量，也不输出秘密。有效凭据修复后自动恢复。缺失/无效 Bearer 返回 401；重复 Authorization 头拒绝。管理员与记忆工具互不授予权限，静态 GET 页面仍公开。

授权通过后，在途业务请求继续执行；撤销影响下一次授权，不强制取消已进入业务的请求。已签名/排队的设备动作也不会因换 HTTP 凭据而撤销。

## 初始化与轮换

以下为待部署的命令，不是本轮已对真实配置执行的记录。CLI 位于 CORE `tools/credential_store.py`，只依赖 Python 标准库与 POSIX 文件能力，不接受秘密参数，不输出 token 或 adminActor。由有权限的操作用户执行；root 更新已有文件时保留目标文件属主/组。初次创建后须为服务用户设置正确属主，目录/文件不要放在 Git 中。

1. 维护窗口内，从当前受保护环境导入原有值，避免改变历史工具任务的身份。已有文件拒绝覆盖：

   ```sh
   python3 tools/credential_store.py init --file /etc/nexus/core-auth/tokens.json
   python3 tools/credential_store.py status --file /etc/nexus/core-auth/tokens.json
   ```

   `init` 读取当前 `NEXUS_ADMIN_TOKEN`，可选读取 `NEXUS_MEMORY_MCP_TOKEN`；先检查权限与独立性，不读取或打印整份真实 env。随后在 CORE env 设置 `NEXUS_AUTH_TOKEN_FILE=/etc/nexus/core-auth/tokens.json`，核对预检并正常重启一次。历史身份必须保留，禁止手改 adminActor 来接管其他任务。

2. 按 status 给出的当前 revision 开始过渡。例如修订 1 时轮换工具令牌：

   ```sh
   python3 tools/credential_store.py rotate --file /etc/nexus/core-auth/tokens.json --scope memory --expected-revision 1 --grace-seconds 300
   python3 tools/credential_store.py export-memory --file /etc/nexus/core-auth/tokens.json --output /etc/nexus/memory-auth/client.json
   ```

   导出只含 `format/revision/id/token` 的当前 memory 凭据，不含管理员信息；初次拒绝覆盖，已知客户端文件更新显式加 `--replace`，无关文件不会被覆盖。更改管理员时用 `--scope admin`，通过受保护编辑器交付新值；不要打印到终端、提交 Git 或放命令行。

3. 桥接 env 设置 `NEXUS_MEMORY_MCP_TOKEN_FILE=/etc/nexus/memory-auth/client.json`；初次切换重启桥接一次，之后以 `export-memory ... --replace` 原子更新。文件模式忽略环境 token，每次 RPC 前读取。缺失/损坏时当前请求返回固定 `memory_endpoint_unavailable`，不发往 CORE、不自动重试；修复后下一次请求可继续，既有协商不丢失。

4. 用当前凭据检查 CORE/桥接，确认客户端已切换后可等待旧值到期，或按 status 的旧 ID 和当前 revision 提前撤销：

   ```sh
   python3 tools/credential_store.py revoke --file /etc/nexus/core-auth/tokens.json --scope memory --id <旧ID> --expected-revision <当前修订>
   ```

   撤销仅接受 previous；泄露当前值时重新 `rotate --grace-seconds 0`，立即替换当前值。已有活跃 previous 会阻止第三次轮换；确需同时使它失效时显式加 `--revoke-previous`。CLI 使用非阻塞文件锁、expected-revision、临时文件 fsync/原子替换及目录 fsync；失败后用 status 核对，不盲目换 ID 再来。

服务器发布包 format=2 纳入 `core/tools/credential_store.py`；当前 verifier 仍接受已知 format=1 文件集合。预检与只读 probe 能读取 CORE 文件，桥接预检同时确认客户端 token 属于当前有效 memory 范围。包内不带真实凭据；新包不能由旧 verifier 验证，需配套更新工具。

## 已执行验证

| 检查 | 结果与边界 |
| --- | --- |
| CORE 标准完整构建 | 59 条，55 通过、4 条环境条件跳过，0 失败/错误；含 Vue 类型/生产构建、实际 Java 21/PostgreSQL/MQTT；本项无新迁移 |
| 轮换 HTTP | 7 项通过；双令牌/到期/撤销、角色隔离、实际第二服务实例保留工具归属、在途请求不被取消、损坏/缺失/权限/符号链接/版本回退保护、实际 CLI→HTTP |
| 启动配置 | 原有无效/复用拒绝与新增文件模式、权限/跨范围复用检查通过 |
| 凭据 CLI | 4 项通过；初始化、轮换/撤销/导出、冲突/并发/旧值保护、拒绝不安全文件和无关覆盖；仅临时文件 |
| stdio / 桥接启动 | 7 / 6 项通过；同一进程文件轮换、损坏恢复、无秘密环境的预检，以及原协议/响应/权限边界 |
| 实际桥接 + TLS/stdio | 6 项通过；新客户端文件在同一 TLS/stdio 会话内生效，未重连、未重放，保留既有断线恢复/退出/日志隔离检查；CORE HTTP 在此项为边界夹具 |
| 发布 CLI | 10 项通过，含文件模式 preflight/probe、过渡期客户端匹配、format=2 与旧 format=1；原基线 ZIP 重新 verify，SHA256 未变 |

本轮没有轮换真实凭据、更新真实 env、部署 systemd/生产 TLS、迁移真实数据库或操作真实设备。POSIX 文件重载在 macOS 测试；后续 Linux 容器验证见下节，目标主机实际用户/目录和 systemd 托管仍需部署验收。固件未改，不以既有构建冒充新增设备能力。

配套 format=2 发布包已生成并校验，实际 JAR 的 Linux 环境模式启动/重启、V1–V11 与管理台资源核对通过，见 [部署记录](../deployment/README.md)。初次环境模式检查未执行动态文件轮换；后续补齐记录如下。

## Linux 私有文件重载验证

`python3 tools/ci/test_server_linux.py --bundle build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-9559429.zip --auth-rotation --evidence build/server-release-20261113/linux-auth-rotation-9559429.json` 退出 0。同一不可变包 SHA256 保持 `7c7670704dac7c1066275c449e95d4194b1db1599943f7748c2f3062a55f03b5`。

实际 Linux Java 21、PostgreSQL、TLS Mosquitto、Caddy，CORE 使用 `65534:65534`、只读根文件系统；凭据在临时 Linux tmpfs 卷，目录 0700/文件 0600，服务只读挂载。另一同 UID、无网络的 Python 容器运行包内凭据 CLI，真实原子替换文件。

公开 HTTP 验证：双令牌均可用、旧令牌显式撤销/到期后 401、memory 范围不能访问管理 API、损坏文件及 0644 权限返回固定 503且不回退环境变量、修复后立即恢复。Docker 启动时间和 RestartCount 在轮换前后相同；之后另行主动重启，当前令牌仍可用。首次启动/重启、V1–V11 与管理台资源同时通过。容器、网络、卷和临时凭据均在成功返回前清理。

证据只含状态、功能标志、包哈希和镜像 digest，不包含令牌。此 Linux 检查覆盖 admin 文件重载及范围隔离；没有在 Linux 启用 Memory MCP 或执行 stdio 轮换，后两者仍采用上文 macOS/TLS 检查证据。目标 systemd、生产凭据和设备不在本次范围。
