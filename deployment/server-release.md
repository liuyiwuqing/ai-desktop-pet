# NEXUS 服务端离线交付与预检

本包用于常在线 Linux 的 CORE/管理台及可选公开记忆桥接。基础语音仍由 ESP32 直接连接供应商。包中没有真实配置、口令、私钥、数据库或固件；不会自动安装、启动服务或迁移数据库。

## 生成与校验

从 `04-ai-desktop-pet` 根目录执行，Python 至少为 3.9：

```sh
python3 tools/server_release.py create --core ../nexus-core --output build/nexus-server.zip
python3 tools/server_release.py verify --bundle build/nexus-server.zip
```

生成前完成 CORE 的标准构建并保持 CORE Git 工作区干净。工具检查可执行 JAR 的应用标识、管理台及 V10 迁移结构，打包 JAR、当前 Git 源码快照、凭据轮换 CLI、stdio 适配器、固定桥接准备工具、依赖锁、systemd/TLS 模板和新装依赖初始化脚本。JAR 结构检查不替代 Maven/前端构建，也不能证明 JAR 一定来自声明的源码提交；交付者须保留配套构建记录。`core-source.tar` 不含 `.git` 历史，完整版本历史仍保留在原 CORE 仓库。

当前新包 `manifest.json` format=2，纳入 `core/tools/credential_store.py`；verifier 仍接受已知 format=1 文件集合。清单记录两仓基线、04 是否有未提交修改，以及每个文件的大小/SHA256。校验拒绝缺失、额外、重复、特殊文件和哈希不匹配。SHA256 用于完整性核对，不代替来源认证；应从可信通道取得本工具及交付方记录的包哈希。输出为新 ZIP，拒绝覆盖既有文件。准备阶段可打包 04 未提交修改，`petDirty=true` 如实记录；准确交付内容由文件清单确定，不将 HEAD 当作全部包内容。

在目标机先用可信工作区中的同版工具校验 ZIP，再解压到新的暂存目录：

```sh
python3 tools/server_release.py verify --bundle /private/path/nexus-server.zip
python3 -m zipfile -e /private/path/nexus-server.zip /private/path/new-staging
```

包内目录与服务模板对应：`core/nexus-core.jar`、`core-tools/memory_mcp_stdio.py`、`desktop-pet/tools/`、`desktop-pet/deployment/`。只复制程序和模板到 `/opt/nexus/`；配置保留在受保护的 `/etc/nexus/`，不要覆盖已有数据库、证书或私有 env。JAR 使用带版本/哈希的新文件名保留，验证后再切换服务入口符号链接。旧 JAR 和配套配置须保留，以便人工回滚。

## 配置与主机预检

从 `desktop-pet` 目录执行：

```sh
python3 tools/server_release.py preflight --env-file /etc/nexus/core.env
python3 tools/server_release.py preflight --env-file /etc/nexus/core.env --voice-env-file /etc/nexus/memory-voice.env
python3 tools/server_release.py preflight --env-file /etc/nexus/core.env --check-host
```

env 沿用包内模板，必须为 owner-only 普通文件（如 600/400），每行不带引号的 `KEY=value`；整行注释可用，不支持 shell、续行或变量展开。工具只读取，不执行文件或输出值。它拒绝重复变量、开发机 `*_CONFIG_FILE` 引用、占位地址、远程明文 HTTP、关闭 MQTT 校验、缺失凭据和启用但不完整的功能配置。CORE 绑定回环，由既有 TLS 代理提供入口。公开记忆工具必须独立于管理员令牌；桥接与 CORE 工具令牌应一致，供应商接入点必须为 WSS。

不带 `--check-host` 只检查配置，不联网、不确认凭据有效或 ACL 正确。`--check-host` 额外要求 Linux/systemd PID 1、模板实际使用的 `/usr/bin/java` 为 Java 21+、systemd 工具以及本机可读/可解析的 MQTT CA；同时检查桥接配置时，要求 `/usr/bin/python3` 至少为 3.9。它不证明 systemd 单元或服务已经启动，仍须在目标机分别执行 `systemd-analyze verify` 检查两份服务单元。可选桥接还需要在目标机准备独立 venv；不能复制 Mac 的 venv。

新装数据库/Broker 可参考包内 `core/compose.yml` 与 `core/tools/init_local.py`，先核对可达域名、设备身份、开放端口与权限。初始化会生成真实凭据及 CA，并非预检步骤，不应在既有数据目录上重复初始化。`init_local.py` 产生的 `local.env` 是开发/Compose 格式，不直接作为 systemd CORE 配置；按 `core.env.example` 手工填写绝对 CA 路径和必要字段。Compose 数据库和 Broker 默认仅绑定回环，设备连接需要单独确定受保护的网络入口。

已有实例升级前停 CORE、备份数据库，并在独立数据库验证恢复；确认 Flyway V8/V9/V10 与设备主题 ACL 配套，再启动新 JAR。启动会运行迁移，预检不会运行迁移。保留策略和记忆增强默认关闭；启用保留前先核对数据库备份及管理 API 清理预览。记忆需核对 Halo/OpenViking 的真实身份与固定目录权限，旧身份副本不能通过换 peer 自动接管。

## 启动后的只读检查

```sh
python3 tools/server_release.py probe --env-file /etc/nexus/core.env --url https://console.your-domain.example
```

私有 CA 可加 `--ca /private/path/ca.crt`；同机 CORE 可使用 `http://127.0.0.1:8088`。工具只发两次 `GET /api/v1/status`：先确认无令牌为 401，再确认带管理员令牌为 200、database/mqtt 均 ready。禁止重定向和环境代理，保留 TLS 校验；令牌不进入参数或输出，也不输出上游响应正文。失败退出 1；成功只代表该 HTTP 鉴权边界及两个依赖当时就绪。

后续仍需逐项验证设备 ACL、MQTT 消息/回执、真实供应商协议及工具采用、公开评论源核验/删除、重启恢复和长期稳定性。记忆检索可能按既定政策排队清理失效副本，不属于本只读探测。物理设备、语音/声学、生产数据库迁移及 systemd 实际托管的验收不能由此包替代。

从完整源码工作区可重复运行 `python3 tools/ci/test_server_release.py` 验证 CLI 边界。可选的 `python3 tools/ci/test_server_linux.py --bundle build/nexus-server.zip --evidence /new/path/linux-smoke.json` 使用 Docker、OpenSSL 和已有镜像 `eclipse-temurin:21-jre-jammy`、`postgres:17-alpine`、`eclipse-mosquitto:2.0.22`、`caddy:2.10.2-alpine`，只在新临时目录生成测试凭据。它运行包内实际 JAR、TLS Broker、数据库和代理，按包内 JAR 的迁移清单检查数据库、首次启动/重启、鉴权和管理台资源；随后删除本轮容器、网络及临时数据。证据记录实际镜像 digest，不代表 systemd 托管、生产 TLS 或真实供应商验收。

支持轮换的 format=2 包可增加 `--auth-rotation`：在 Linux 私有 tmpfs 卷中，以实际非 root UID 运行包内 CLI 原子替换凭据，检查 admin 过渡/到期/撤销、范围隔离、异常文件拒绝和不重启恢复，再验证主动重启。此模式额外拉取 `python:3.13-alpine` 并记录 digest；清理临时卷。它不启用 Memory MCP、不触及真实凭据，详细边界见 [20](../docs/20-credential-rotation.md#linux-私有文件重载验证)。

受保护凭据文件模式、current/previous 过渡和客户端导出见 [凭据轮换](../docs/20-credential-rotation.md)。预检和 probe 从 `NEXUS_AUTH_TOKEN_FILE` 读取管理员当前值，桥接预检验证独立客户端 token 仍有效；不回退旧环境变量，不输出秘密。配置文件只保留在 `/etc/nexus/`，发布 ZIP 不带真实凭据。
