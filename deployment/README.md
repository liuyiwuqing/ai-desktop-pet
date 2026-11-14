# 部署准备与恢复

这些文件是待绑定真实环境的模板。本次只在独立测试环境验证，没有安装系统服务、修改真实数据库或 Broker，也没有申请公网证书或部署。基础语音直接由 ESP32 连接小智，日常不依赖开发用 Mac；NEXUS 可运行在长期在线服务器。Mac 控制是可选扩展，只有启用它时才需要目标 Mac 与 Bridge 在线。

## 1Panel 镜像部署（DEL-07）

1Panel 部署使用 CORE 的 [镜像部署说明](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/1panel-deployment.md)：服务器只拉取同次发布的 CORE/初始化镜像，无需上传源码或安装 Python/Java/Node/Maven。初始化容器不挂载 Docker socket，只在明确挂载的新目录生成服务器自己的密码、MQTT CA/证书和完整 `compose.1panel.yml`，拒绝覆盖已有配置；之后直接把生成的编排粘贴到面板。

该编排使用普通 `env_file`，不带旧版 1Panel 不支持的 `format: raw`，不含 `build`、禁止拉取策略、Caddy 或 80/443 映射。CORE 默认只发布 `127.0.0.1:18088`，HTTPS 由 1Panel 的现有反向代理提供；OpenResty 为 bridge 网络时需使用同一 edge 网络的服务地址，不能把容器内回环地址当宿主机。数据库仅内部访问，MQTT/TLS 可按设备上线需要发布 8883。

CORE `043f134` 已推送并核对远端：CLI 11 项、原 Compose 六组、新增镜像部署七组通过；实际本地镜像为 `linux/arm64`，包含无源码/socket 初始化、私有权限/哈希、原样拒绝重复初始化、特殊路径/env、认证/实际 JAR 页面、MQTT TLS、重建持久性。初始化镜像默认网络构建因 Alpine DNS 失败，host 网络构建通过，未修改全局网络或软件源。Actions 的 actionlint 检查通过，未运行无关业务修改或生产迁移。

发布工作流先验证，再以仓库短期 `GITHUB_TOKEN` 发布 amd64/arm64 两种架构，随后在新 job 中按 digest 从 GHCR 回拉并验证实际镜像。私有仓库/包可见性不变；服务器和 1Panel 需一次性配置 GHCR 的读取凭据，不能把令牌写进编排。CORE `043f134` 的发布运行 [43584412344](https://github.com/liuyiwuqing/nexus-core/actions/runs/43584412344) 已成功，最终 digest 为 `ghcr.io/liuyiwuqing/nexus-core@sha256:2653a588d45b7e46fdf3c8e9672d62bb8be2223a7d5058b7292b01820dbe259c` 与 `ghcr.io/liuyiwuqing/nexus-core-init@sha256:6195bb0732f248eaf6c43cda4778bbe115c67c69f0633c165461bb55c460759c`；发布后回拉验证 7 项通过。Debian/1Panel 目标机尚未连接或验收。

## 管理台 Docker 部署（DEL-06）

本节保留源码构建与独立 Caddy 的历史方案；1Panel 直接拉取镜像使用上面的 DEL-07。

线上管理台采用 CORE 仓库的独立 [Docker Compose 方案](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/docker-deployment.md)：一个非 root/只读 CORE 容器提供 Vue 页面和 API，配套 PostgreSQL、MQTT/TLS、Caddy HTTPS。源码多阶段构建、私有配置初始化、持久化、认证健康检查、日志轮换、备份和升级步骤在该文档维护；根目录开发 Compose 与下文 systemd 方案保留各自用途。

初始化使用 `nexus-core/tools/init_docker.py`，只接受新的私有输出目录，拒绝覆盖原凭据。公开入口为 HTTPS 80/443 与 MQTT/TLS 8883；数据库与 CORE 8088 不发布到宿主。Halo/AI/记忆/Bridge、清理、OTA 和成长同步仍默认关闭。`server_release.py` 的 systemd 预检保持回环绑定要求，不能直接用于容器内的 `0.0.0.0` 配置。

本地 CLI 11 项、完整前端/Maven 构建（62 通过/5 条件跳过）和实际 `linux/arm64` 源码镜像通过；随机隔离 Compose 项目的 HTTPS/鉴权/JAR 页面资源、严格 MQTT TLS、私有端口/非 root/只读、原样 env、容器重建保留消息和 Broker 故障恢复六组检查通过。报告在 CORE 忽略目录 `target/docker-smoke.json`，只删除本轮随机测试卷/网络；没有注册设备或向真实 Broker 发消息。远程 Docker CI 与配套提交另在 [台账 DEL-06](../docs/18-requirements-and-progress.md)核对。

当前没有目标服务器，本次没有上线或申请公网证书。公网 DNS/防火墙、生产资源、真实库升级/备份恢复、供应商与设备仍需目标环境验收；本地 `localhost` 私有 CA 证据不能替代公网 HTTPS。原来的离线 ZIP 保持既有字节，不自动包含新 Docker 方案。

## 当前交付入口

远程 OTA 已随 04 `0910e5f` 与 CORE `2808ed7` 推送，GitHub [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 六个 job 全部成功。当前凭据轮换和发布包 format=2 见 [20](../docs/20-credential-rotation.md)；下列基线包与 Linux 验证保留为历史记录，不自动包含后续功能。

## 离线交付与预检（2026-11-13 基线）

当前尚无目标服务器，先交付[服务端发布包与预检入口](server-release.md)。`tools/server_release.py` 提供 `create/verify/preflight/probe`，只生成新包、核对完整性、读取配置或发只读状态请求，不安装服务、开启功能或迁移真实数据库。`core.env.example` 已补齐可选 Halo/AI/记忆/Bridge 字段，开关仍默认关闭。

配套 CORE 已独立纳入 Git，分支 `codex/core-baseline`，源码基线 `913b3af40eba70a2dba42ac20de6689b933aa11c`，共 100 个文件。私有 `data/`、凭据/私钥、前端依赖和构建输出保持排除。随后按用户连续交付要求建立私有远程 [nexus-core](https://github.com/liuyiwuqing/nexus-core)，文档提交 `ac9a29a` 已推送并保留该基线；原 ZIP 仍绑定 `913b3af`。前端类型检查/生产构建及 Maven verify 通过：44 项，41 通过、3 条件跳过，0 失败/错误；跳过真实模型、Java/C 管理回环和可选语音桥接，不计为本轮通过。

交付 CLI 的 7 项回归通过，覆盖新包/拒绝覆盖、篡改/额外路径/非法 JAR、误提交私有文件、配置权限与缺项、独立令牌、只读状态检查和重定向拒绝。新增 GitHub `server-delivery-tools` job，基线时只在本地执行；随后已推送并远程成功，见本页当前交付入口。

`tools/ci/test_server_linux.py` 已用包内实际 JAR、Java 21.0.13、PostgreSQL 17、Mosquitto 2.0.22 和 Caddy 2.10.2 完成隔离 Linux 容器验证：非 root/只读文件系统、V1–V10 迁移、首次启动/重启后 database/mqtt ready、无鉴权 401/有效鉴权 200，以及管理台首页和两个资源逐字节匹配。重启用例曾因 Docker 动态端口变化超时；对照确认原地址失败而新地址成功后，修正了测试端口重新发现，临时诊断已清理。所有本轮容器/网络已移除，原 CORE 16 份私有文件哈希未变。

当前产物为 `build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-913b3af.zip`，SHA256 `d9121f8105ec8644c72777bf1fad71717abcdf56cf0d050ddac4cbd9d3614122`，配套 `linux-smoke.json` 记录实际镜像 digest 与验证边界。ZIP 包含 17 份交付文件及清单；04 本轮工具仍待提交，`petDirty=true` 如实记录，清单标识实际文件内容。该检查没有运行真实 systemd、生产 TLS、供应商/记忆服务或设备，不代表真实数据库升级与部署验收。

## CORE 与管理台（可选 systemd 方案）

下列保留的替代方案为 Linux/systemd、Java 21、PostgreSQL 17、Mosquitto 2.0.22；沿用 [nexus-core 开发 Compose](../../nexus-core/compose.yml) 的数据库/Broker。Docker 线上部署使用上节独立配置，不能混用这两套启动命令。systemd 构建使用该项目 `python3 tools/run_local.py build`；它会执行前端检查和真实容器集成测试。将生成的 JAR 以不可变文件名保留，再原子切换 `/opt/nexus/core/nexus-core.jar` 指向选定版本。专用 `nexus` 系统用户只读 JAR/配置/CA，不能写源码或私钥。

将 [core.env.example](core.env.example) 复制到 `/etc/nexus/core.env`，由部署者填写独立管理员、数据库、MQTT 凭据和有效 Broker 域名，权限 `600`。CA 是公开证书文件，须允许服务用户读取；检查证书 SAN、有效期和设备端信任链。MQTT 保持 TLS/主机名校验；不得用 `allow-insecure` 绕过。当前管理员是单一共享令牌，审计 actor 为 admin，尚无多用户/RBAC；管理入口应限制到可信网络或 VPN。

[nexus-core.service](nexus-core.service) 的 `/usr/bin/java`、JAR 路径、用户及数据库端口需按主机核对。先用 `systemd-analyze verify deployment/nexus-core.service` 检查目标机支持，再安装并启用；没有在 macOS 上假称 systemd 验收。服务停止等待 30 秒；后台队列/事务由已有持久化机制恢复，进程启动会运行 Flyway。

[Caddyfile](Caddyfile) 在 CORE 同机使用，设定 `NEXUS_CONSOLE_DOMAIN` 为真实域名，默认反代 `127.0.0.1:8088`，管理请求体限 32 KiB，关闭代理重试。公网 HTTPS 需要域名 DNS、80/443 可达及持久化 Caddy 证书目录；已有 TLS 入口可移植相同规则，不必重复安装代理。[官方自动 HTTPS](https://caddyserver.com/docs/automatic-https)、[请求体限制](https://caddyserver.com/docs/caddyfile/directives/request_body)、[反向代理](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy)是配置依据。

```sh
# 以下为部署者在目标机执行的预检，不会启动服务。
NEXUS_CONSOLE_DOMAIN=console.your-domain.example caddy validate --config deployment/Caddyfile --adapter caddyfile
systemd-analyze verify deployment/nexus-core.service
```

实际启用前：确认私有配置没有空值或 `.invalid`，后端仍绑定回环、数据库只在可信网络、Broker 只开放 TLS 端口，Caddy 管理端口不外露。先验证未带令牌访问 `/api/v1/status` 返回 401，再用本地受保护脚本读取令牌验证 database/mqtt 均 ready；不要把令牌写入 shell 历史、URL 或日志。后台健康不等于设备在线。

## 远程配置升级

新库自动应用 V1–V10，已有库在新 JAR 启动时补齐缺失迁移；V8 为远程管理，V9 为消息保留，V10 为长期记忆副本账本与删除队列。须先停 CORE 并 `pg_dump -Fc` 备份，恢复到独立数据库验证可读后再升级。保留旧 JAR、部署配置和证书备份。V8/V9/V10 已在隔离 PostgreSQL 验证，尚未迁移真实库。

消息保留默认关闭，不因升级自动删除历史。启用前读取 `GET /api/v1/retention` 核对预览；7 天正文/30 天元数据、每批上限、外部副本边界与手工执行见 [运行维护记录](../docs/13-operational-software.md)。

长期记忆仍默认关闭。若已有环境启用了 `NEXUS_MEMORY_ENABLED`，V10 升级后会按“源评论撤回/非公开/改文→停止检索并排队删除”处理可追踪副本；先核对固定目录权限及身份，查看 `GET /api/v1/memory` 的待删除/失败/身份隔离计数。V10 导入的旧副本缺少服务/account/peer 的完整历史，会先隔离，需核对旧实例与身份后受控迁移；不自动赋予当前身份。已保存副本的来源账本独立于本地正文清理；规则、权限及真实联调边界见 [15](../docs/15-memory-lifecycle.md)。

Broker 老实例需在 `nexus-core` 执行 `python3 tools/upgrade_broker_policy.py` 只读预览。显式 `--apply` 才会备份并更新已知旧 ACL/包限制；自定义 ACL 会拒绝自动覆盖，须手工合并设备作用域规则。脚本不重启 Broker；在维护窗口人工重启后，验证设备不能读他人 config 或伪造他人 config_acks。完整 [管理契约](../integration/contracts/device-management-v1/README.md)包含状态与冲突规则。数据库和 ACL 必须成套升级；旧 ACL 会使配置一直等待确认。

## 可选公开记忆语音工具

CORE `/mcp/memory` 默认关闭，仅暴露 `memory.search`，使用独立 `NEXUS_MEMORY_MCP_TOKEN`；管理员令牌不能用于此路径，工具令牌不能访问管理 API。`core.env.example` 含默认关闭的配置项。实际检索沿用 V10 源复核与删除政策，工具令牌不赋予显式保存或任意写/删能力。

[memory-voice.env.example](memory-voice.env.example) 和 [nexus-memory-voice.service](nexus-memory-voice.service) 供常在线 Linux 的供应商桥接使用；只向桥接提供受限工具令牌，不加载完整 CORE env。CORE 在本机回环提供 HTTP，远端必须使用可信 HTTPS；不下发服务端密钥到设备。[准备/启动工具](../tools/memory_voice.py) 以[固定源](memory-voice.lock.json)和[固定 wheel](memory-voice.requirements.txt)准备独立环境，并限制配置、日志和进程生命周期。当前准备命令、路径、stdio 退出修补与本地 TLS 回归见 [17](../docs/17-voice-bridge-regression.md)；接口、停用/单令牌轮换及故障定位见 [16](../docs/16-voice-memory-tools.md)。仅运行了本地独立环境，未安装常驻服务或进行真实供应商验收。

## 可选 Mac 控制

[Caddyfile.control](Caddyfile.control) 放在 Gateway/Bridge 所在 Mac，设 `PET_CONTROL_DOMAIN`，仅代理 POST `/v1/execute` 与 GET `/v1/results/<deviceId>/<requestId>`；其他方法/路径 404，请求体限 2048 字节、每路由上游连接最多 8、不重试。它不自带分布式速率限制；需要公网暴露时在现有入口配置每设备速率/并发配额。部署前按 [Gateway 说明](../server/README.md)填写设备令牌/白名单，并保护 SQLite 审计和重放水位。Bridge/Gateway 继续只监听回环；不要把这份代理部署在远端服务器后仍指向远端 localhost。

## 固件、OTA 与回滚

使用 [维护恢复](../docs/10-maintenance-recovery.md)的设备配置、分区和备份流程。OTA 签名私钥由部署者离线保存，固件只包含配套公钥；本机编译测试公钥及 `.invalid` URL 不可用于生产。发布前使用 `tools/package_release.py` 生成并验证签名清单，再在独立目录验证下载、哈希、版本、板型、分区与 SR 模型配套。TLS 证书不替代 OTA 签名。首次启动确认健康后才接受新镜像；烧录、断电恢复与真实升级仍需设备验收。

CORE 故障时先停服务，保留日志和数据库，不执行 `down -v`/Flyway clean；在独立数据库恢复备份并验证后，切回相容 JAR 与数据库。当前迁移无自动 down，不可直接在已写入新数据的库上降级后宣称恢复成功。Broker 用升级脚本保存的 `.management-v1-backup` 恢复 ACL/配置后人工重启。设备配置结果 unknown/unconfirmed 必须读取原请求与设备状态核对，不能换 requestId 重发动作。

远程 OTA 为独立默认关闭能力；V11、设备固件、公钥/HTTPS 与 Broker OTA ACL 须成套部署，步骤见 [远程 OTA](../docs/19-remote-ota.md)。现有 `913b3af` 离线包保持原快照；部署本功能须另行构建新包。

## OTA 与凭据轮换配套包

新包 `build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-9559429.zip` 绑定 CORE `9559429` / 04 `ccd3afa`（创建时两仓已提交，petDirty=false），format=2，共 18 份交付文件，SHA256 `7c7670704dac7c1066275c449e95d4194b1db1599943f7748c2f3062a55f03b5`。包含 OTA、凭据文件模式及其 CLI；旧基线 ZIP 保留且哈希不变。

该包实际 JAR 在独立 Linux Java 21/PostgreSQL/Mosquitto/Caddy 容器首次启动、重启、鉴权、依赖就绪与静态资源核对通过，V1–V11 实际迁移与 JAR 清单一致；证据在同目录 `linux-smoke-9559429.json`。原测试硬编码 V1–V10 曾于迁移历史断言失败；现按包内 JAR 的真实迁移资源清单核对，使用同一不可变包重跑退出 0。无 systemd、生产 TLS、真实数据/供应商或实机结论；临时容器和网络已清理。

DEL-03 在相同包上使用 `--auth-rotation` 验证实际 Linux 私有卷和 UID 65534 下的 admin 凭据原子替换；过渡、到期、撤销、固定 503、权限拒绝及修复后不重启恢复通过，随后主动重启仍能鉴权。证据 `linux-auth-rotation-9559429.json`；额外 Python 容器无网络，临时卷/容器/网络已清理。Memory MCP 的 Linux 动态轮换未启用，目标 systemd/生产用户目录仍待验收，见 [20](../docs/20-credential-rotation.md)。


管理台 RSS / 天气已提供默认关闭的镜像内连接器；按 [拉取来源说明](../../nexus-core/docs/pull-connectors.md) 修改私有 core.env 并重新创建 CORE。已有安装不要重新初始化。
