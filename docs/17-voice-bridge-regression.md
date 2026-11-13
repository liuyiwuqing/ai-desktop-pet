# 语音记忆桥接准备与回归（2026-11-13）

公开记忆工具已补齐可重复的桥接准备、受控启动及本地 TLS WebSocket 回归。已运行实际桥接、stdio 适配器和真实 CORE/PostgreSQL；供应商、Halo、模型与 OpenViking 的外部边界使用隔离夹具。没有连接真实语音供应商、安装常驻服务、修改真实配置或烧录设备。

基础语音继续由 ESP32 直连供应商。可选公开评论记忆工具可部署在常在线 Linux，不依赖开发 Mac；权限、来源核验和副本失效规则沿用 [16](16-voice-memory-tools.md) 与 [15](15-memory-lifecycle.md)。

## 固定输入与最小修补

[准备/启动入口](../tools/memory_voice.py) 读取[源锁文件](../deployment/memory-voice.lock.json)与[依赖锁文件](../deployment/memory-voice.requirements.txt)。第三方源码不随本仓分发，在准备时下载并校验；离线源文件也必须匹配同一哈希。

| 输入 | 固定值 |
| --- | --- |
| [桥接来源](https://github.com/78/mcp-calculator/tree/c537f71d61fd73b47d6c8955b5df6d3721acf4e4) | `c537f71d61fd73b47d6c8955b5df6d3721acf4e4`，源码标注 v0.2.0 |
| 上游 `mcp_pipe.py` SHA256 | `31af96a0bd83062251bafe31c22cc01dc02a6b687c5775c5f70e3cf552372df8` |
| 应用本地修补后的 SHA256 | `f155374d153e2d94577fe5b05b45f9839c555f44402699574ae450ecae742002` |
| Python 依赖 | `websockets==15.0.1`、`python-dotenv==1.2.1`；固定通用 wheel URL 和 SHA256 |

仅使用桥接的“直接启动 Python stdio 脚本”模式，无须安装其通用代理、FastMCP 等其他模式的依赖。pip 使用独立 venv、`--isolated --require-hashes --no-deps --only-binary=:all:`。`verify` 检查上游/运行脚本、空配置及已安装包版本，不是对 venv 全部文件的完整性扫描；部署时仍应保护程序和环境目录的写权限。

回归复现了上游的一处退出问题：stdio 超长输入按适配器契约退出后，桥接的 stdout 读取仅 `break`，WebSocket 接收仍在等待，8 秒内没有恢复连接。准备工具只把该分支改为 `raise RuntimeError("stdio_output_closed")`，交给现有的异常清理与重连流程；不改 RPC 内容或工具权限；沿用上游退避，最长 600 秒，本轮未进行 24 h/7 d 运行验收。原文件保存为 `mcp_pipe.upstream.py`，实际运行文件为 `mcp_pipe.py`，两者分别校验哈希。源码变化或匹配位置不唯一时拒绝修补。

## 准备与启动

在本项目根目录执行，Python 至少为 3.9，目标为 macOS/Linux。当前本地验证使用 Python 3.14；其他运行环境须执行下方回归。venv 包含目标机解释器路径，应在目标机准备，不能把开发 Mac 的 venv 直接复制到 Linux。

```bash
python3 tools/memory_voice.py prepare --directory build/memory-voice
python3 tools/memory_voice.py verify --directory build/memory-voice
```

第一条下载公开固定源码与两个 wheel，只写指定目录，不读取真实 env，也不连接供应商或 CORE。可添加 `--source-file /path/to/mcp_pipe.py` 使用已取得的原始源码；依赖安装仍需 wheel 可用。`verify` 不访问网络。未知的非空目录、不同锁文件、已有脚本或配置被改动时均拒绝覆盖，更新版本应选新目录并保留旧版。

配置仍只有 `MCP_ENDPOINT`、`NEXUS_MEMORY_MCP_URL`、`NEXUS_MEMORY_MCP_TOKEN`，通过受保护环境注入，参见 [env 模板](../deployment/memory-voice.env.example)。供应商地址必须为 `wss://`；CORE 可走同机回环 HTTP，远端必须可信 HTTPS。启动命令不含凭据：

```bash
python3 tools/memory_voice.py run --directory build/memory-voice --stdio ../nexus-core/tools/memory_mcp_stdio.py
```

启动前用真实 stdio 进程的 EOF 路径检查 CORE 配置，避免地址错误后仍挂着桥接。桥接只收到明确列出的环境变量，禁用 dotenv、环境代理和外部 `MCP_CONFIG`，固定空配置后再运行指定脚本。管理员/模型密钥及 `PYTHONPATH` 不传给子进程。使用系统 TLS 信任链，可通过受控 `SSL_CERT_FILE`/`SSL_CERT_DIR` 配置私有 CA，不关闭证书校验。

原桥接会把异常和 WebSocket close reason 原文记入日志，可能包含接入点或敏感数据；启动器丢弃其 stdout/stderr，只输出固定的进程启动状态或允许列表中的错误原因。`running` 仅说明进程已启动，实际在线与工具可用必须用初始化、发现和调用验证，不能靠这行状态认定。此入口不提供连接级指标或 RPC 正文日志。

SIGINT/SIGTERM 会停止整个独立进程组，超时后强制退出；macOS 在父进程退出后的组信号竞态会进一步核查是否仍有存活成员，真实权限错误仍报失败。更新后的 [systemd 模板](../deployment/nexus-memory-voice.service) 同时保留 `KillMode=control-group`，覆盖启动器异常退出后的系统级清理。没有通用容器/PID 1 托管验收，生产环境按该服务模板使用。

Linux 模板约定本仓工具及两个锁文件位于 `/opt/nexus/desktop-pet/`，桥接环境位于 `/opt/nexus/memory-voice/`，适配器位于 `/opt/nexus/core-tools/memory_mcp_stdio.py`。准备时选 `/opt/nexus/memory-voice`，为 `nexus-mcp` 配置可读/可遍历权限，运行时禁止服务用户改程序和 venv；env 为 600 且由服务管理器读取。按目标机路径核对模板，执行 `systemd-analyze verify` 后再安装启用。本轮没有安装用户、配置真实 env 或执行 systemd 验收。

## 可重复验证

准备/验证 CLI 只需 Python 标准库和网络；完整桥接测试还需要同级 `nexus-core` 的真实 stdio 脚本，以及 OpenSSL。以下 TLS 证书及端点令牌均在测试中临时生成或使用固定测试值。

```bash
python3 tools/ci/test_memory_voice_prepare.py
build/memory-voice/.venv/bin/python tools/ci/test_memory_voice_bridge.py
python3 tools/ci/test_memory_mcp_stdio.py
```

完整 CORE 回归从同级项目运行，使用独立 PostgreSQL/MQTT 测试容器及外部 HTTP 夹具：

```bash
cd ../nexus-core
NEXUS_VOICE_BRIDGE_TEST=true python3 tools/run_local.py test
```

`NEXUS_VOICE_BRIDGE_DIRECTORY` 可指定已准备的绝对目录，默认采用相邻本项目的 `build/memory-voice`。新测试 `thePinnedVoiceBridgeRevalidatesPublicContentAcrossTlsReconnects` 通过临时 WSS 服务执行初始化、工具发现、检索、断线重连，再把源评论改为非公开，核对后续检索与重连均返回空。底层确实使用 Java HTTP 和隔离 PostgreSQL，不是假成功结果；上游评论/模型/记忆服务仍是测试夹具。

| 本轮检查 | 结果 | 覆盖 |
| --- | --- | --- |
| 准备/启动 CLI | 5 项通过 | 固定源/依赖、离线验证、拒绝覆盖、改动/非法源、WSS 地址与 CORE 启动预检 |
| 实际桥接/stdio/WSS | 5 项通过 | TLS 拒绝不可信证书、发现/调用、重连重初始化、stdio 退出恢复、故障不重放、环境/日志隔离、进程组退出 |
| 原 stdio 入口回归 | 6 项通过 | HTTP 响应/鉴权故障、禁止重定向、初始化与体积/Unicode 边界 |
| CORE 完整测试（显式启用桥接项） | 44 项，42 通过，2 条件跳过，0 失败/错误 | 真实 HTTP/PostgreSQL/MQTT；外部服务为夹具 |
| 当前仓库契约镜像检查 | 提交前恢复后通过 | 22 个误回退文件已恢复到覆盖前的精确字节；4 个消息 schema/example 与 `nexus-v1` 快照一致，未放宽检查 |

准备 CLI 和无效 CORE 启动用例先失败后通过；stdio 退出恢复用例先因等待重连超时失败，加入限定修补后通过。关闭路径另修正了实际发生的 macOS 组信号竞态，并核查无存活组成员。GitHub 工作流新增准备检查；独立 04 checkout 缺少同级 CORE 时，该套测试明确跳过 1 项 stdio 启动预检，其余 4 项运行。完整 WSS/CORE 回归保留为配套 checkout 的本地入口，未声称 GitHub 已执行。

CORE 两项跳过是需单独启用的真实模型调用与 Java/C 管理回环，未计入通过数。桥接项已通过本轮显式开关执行；通常不设置开关的 CORE 测试会额外跳过此项。没有为本轮重跑前端、浏览器或固件构建，它们保持前轮证据。

09:07 左右曾发现 22 个消息/网络相关输入回到了旧 HEAD，`sourceTreeSha256` 从 `19d1452e…cc63b` 变为 `27a2c74d…86d5f`；reflog 没有新的分支移动。`check_contracts.py` 如实报出 4 个镜像不一致，差异包括 `msg_*` 取代 UUID、时间戳类型与消息信封变化、`ack/state` 取代 `acks/status`。确认变更意图前保留了现场，没有修改快照掩盖问题。覆盖来源仍未确认。

2026-11-13 用户确认属于误回退，并要求提交代码。提交前按已验证的恢复包还原 22 个文件，逐文件 SHA256 与覆盖前构建记录一致，其余文件、HEAD 和暂存区均经核对保持原样；恢复与回滚预先在隔离目录演练。恢复后全部 343 份固件输入重新匹配 `19d1452e…cc63b`，四预设既有产物的大小/哈希也全部匹配。暂存检查随后清理三个 runtime/测试文件的末尾空行和字体许可文本的尾空白，内容语义未变；使用 ESP-IDF 6.1 重新完成 default/full/off/tones 四预设增量构建，全部退出 0。新的共同输入 `sourceTreeSha256=cacf5a1d914895e8619f304808e0501c98260d871443b87b6ce380aba213d145`，四套镜像字节与清理前一致，构建证据及许可清单已重新生成。

提交前在实际工作区重新运行完整主机回归（ASan/UBSan）、契约镜像、依赖环和板级生成检查，全部退出 0；准备 CLI 5 项、stdio 6 项通过。另从历史记录提取原 `MqttTransport.java` 并核对冻结哈希，逐段审查新增配置/遥测通道及主题前缀校验后，更新该文件的兼容指纹；其余五个服务端指纹与消息 schema 保持不变。使用 JDK 21 重新运行 Java/C 源码契约回环通过，仍使用明确的 SQL/传输/Flash 边界替身，不代表真实 Broker 或数据库联调。

CORE JAR 仍为 [16](16-voice-memory-tools.md) 中的 `cbe51464…3caea`，stdio 脚本仍为 `8ed3ce45…0b84`，本轮已重新核对二者。

## 交付边界与下一步

本轮未修改固件、前端、CORE 业务实现或数据库迁移，未重新打包 JAR。默认关闭、独立鉴权、来源权限和显式保存策略不变。真实供应商版本协商、工具采用与最终语音回答、真实 Halo/OpenViking 权限及删除、Linux 托管和生产 TLS、设备语音/声学验收仍待推进；本地 WSS 夹具通过不能替代这些验收。

成长跨端同步、凭据轮换、远程 OTA 管理、资源/语言及其他平台仍属后续能力；本轮完成的是记忆工具的桥接软件闭环。真实联调沿用 [12](12-cloud-interop.md) 和 [16](16-voice-memory-tools.md) 的步骤。
