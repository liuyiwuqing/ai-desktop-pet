# ACC-01 生产部署与数据库迁移执行计划

2026-11-20 制定，2026-11-23 执行 G0–G4。基线：CORE `d2711f7`（迁移 V1–V28）、04 `042ed1f`。**本文件主体是执行计划，执行结果回填在各节末尾**：G0 发布物（3.4）、G1 只读盘点（5.3）、G2 备份与隔离恢复（6.2）、G4 离线导入与升级（8.5）、G6 运行时检查（10.2.1）与 24 小时观测（10.3.1）。2026-11-24 追加执行 ACC-02 变更（CORE `d2711f7` → `88d2e90`、V29、伙伴空间启用），记录见第 15 节；同日追加 ACC-03（遥测契约缺口修复，CORE `3fdca79`，第 16 节）与 ACC-04（控制台可编辑屏幕与提示音，CORE `c88957e`，第 17 节）。当前生产基线以第 17 节为准。生产仍未接真实设备消息；HTTPS/SSE、MQTT TLS 与端口暴露的运行时检查已记录在 10.2.1，浏览器 UI 登录/权限页、24 小时观测与实机联调未完成；第 13 节保留空白模板供后续变更使用。生产仍未接真实设备消息；HTTPS/SSE、MQTT TLS 与端口暴露的运行时检查已记录在 10.2.1，浏览器 UI 登录/权限页、24 小时观测与实机联调未完成；第 13 节保留空白模板供后续变更使用。

## 1. 目标、范围与完成判定

目标：把通过发布门禁的 CORE 镜像部署到已提供的 Debian/1Panel 服务器，完成 PostgreSQL 首次建库或既有库迁移，配置 HTTPS、SSE、MQTT TLS 和备份，留下可复核的证据。

范围内：

- 发布物冻结：提交、镜像 digest、初始化镜像 digest。
- 目标机预检、首次部署、既有安装升级、Flyway 数据库迁移。
- 备份、隔离恢复验证、失败回滚。
- HTTPS/反代、SSE 不缓冲、MQTT 8883、健康检查与基础冒烟。
- 24 小时观测与证据归档。

不在范围内：

- ACC-02 的实机烧录、声学、传感器、断网掉电、长时间稳定性。
- 真实供应商接入、AI/记忆/Halo/Bridge/OTA/成长同步/清理策略。
- 真实消息群发、真实设备擦除或凭据重建。
- 数据库手工改表、跳过 Flyway、`flyway clean`、`down -v`。

ACC-01 只有在真实服务器完成部署、数据库迁移与备份恢复验证，并留下第 13 节证据后才算通过；本地测试、远程 CI、镜像构建成功都不能替代。

## 2. 当前基线（制定时事实）

| 项 | 事实 |
| --- | --- |
| CORE 代码 | `d2711f7`，最新迁移 `V28__delivery_display.sql`；V27、V28 在 `acc95fb` 之后加入 |
| 最近已发布镜像 | CORE `d2711f7`，发布运行 [45103288550](https://github.com/liuyiwuqing/nexus-core/actions/runs/45103288550) `success`，包含 V1–V28 |
| 已发布 CORE digest | `ghcr.io/liuyiwuqing/nexus-core@sha256:bcf7f6191d61e2f1cd93592ff4e4f9c44eaa42e07688d7cb243254800049a037` |
| 已发布 init digest | `ghcr.io/liuyiwuqing/nexus-core-init@sha256:70d178fb6236b68939438a5f22b151deb374bc72968c78efa2793216237c8055` |
| 域名/设备 | `pet.muyin.site`、设备 `dodo-1`；MQTT 统一使用 `pet.muyin.site:8883`（2026-11-23 决定） |
| 目标机 | Debian 12/1Panel，已通过 SSH 只读连接；现有 `nexus-production`，G1/G2 已记录，升级路径 |
| 镜像路线 | 优先按 digest 在线拉取；GHCR 不可达时使用离线导入，必须同时导入 CORE、init、PostgreSQL、Mosquitto |

**关键差异**：直接部署 `acc95fb` 是已发布且已做镜像冒烟的组合，但不含 V27/V28 与 11-18 之后的管理台修复。推荐先对当前绿色提交执行发布门禁（G0），用新产物部署；只有明确接受功能冻结时才使用 `acc95fb`。G0 已于 2026-11-23 执行，产物见 3.4。

## 3. G0 发布物冻结

### 3.1 选择发布提交

在发布工作站执行，确认远程分支与准备发布的提交一致：

```sh
cd nexus-core
git fetch origin
git status --short
git rev-parse HEAD
git ls-remote origin refs/heads/codex/core-baseline
gh run list --branch codex/core-baseline --limit 5 \
  --json databaseId,workflowName,status,conclusion,headSha
```

门禁条件：

- 工作区无未提交改动；`HEAD` 已推送，远程分支指向同一提交。
- 该提交的 `Docker CI` 为 `success`；`Publish deployment images` 的 `validate` 会再跑一次真实 Compose 部署检查。
- 迁移文件只增不改；`V1`–`V28` 的顺序和内容与已审阅版本一致。

### 3.2 发布多架构镜像

工作流只允许在默认分支 `codex/core-baseline` 上发布：

```sh
gh workflow run publish-images.yml \
  --repo liuyiwuqing/nexus-core \
  --ref codex/core-baseline

# 等待新运行出现，确认 headSha 等于发布提交
gh run list --repo liuyiwuqing/nexus-core \
  --workflow publish-images.yml --limit 1 \
  --json databaseId,headSha,status,conclusion,createdAt
```

记录 `<run-id>`，等待完成：

```sh
gh run watch <run-id> --repo liuyiwuqing/nexus-core --exit-status
```

下载已核验的镜像引用；产物不含密钥：

```sh
install -d -m 700 /secure-release/<run-id>
gh run download <run-id> --repo liuyiwuqing/nexus-core \
  -n published-image-verification -D /secure-release/<run-id>
cat /secure-release/<run-id>/published-images.env
```

冻结并记录：

```sh
NEXUS_CORE_IMAGE='ghcr.io/liuyiwuqing/nexus-core@sha256:<core-digest>'
NEXUS_INIT_IMAGE='ghcr.io/liuyiwuqing/nexus-core-init@sha256:<init-digest>'
```

核对：

- `headSha` 等于发布的 CORE 提交。
- 两个镜像引用都以 `@sha256:` 结尾，不使用 `latest`、分支名或短提交 tag。
- 可选：`docker buildx imagetools inspect "$NEXUS_CORE_IMAGE"` 确认 `linux/amd64` 存在；目标机若是 arm64，同时确认 `linux/arm64`。
- `published-image-smoke.json` 为净化报告，可归档；不包含凭据。

### 3.3 两条发布路线

| 路线 | 条件 | 结果 |
| --- | --- | --- |
| A（推荐） | 当前提交 CI 绿，发布工作流成功 | 部署当前功能，含 V27/V28；必须重新拉取新 digest |
| B（回退） | 不希望现在发布，接受功能冻结 | 部署 `acc95fb` 已发布 digest；明确不含 V27/V28 与最新管理台修复 |

不得把 A 的代码与 B 的镜像混用；前端、后端、迁移必须来自同一镜像。

### 3.4 G0 执行记录（2026-11-23）

| 项 | 值 |
| --- | --- |
| 发布提交 | CORE `d2711f7`（04 配套 `042ed1f`） |
| 发布运行 | [45103288550](https://github.com/liuyiwuqing/nexus-core/actions/runs/45103288550) `success` |
| CORE 镜像 | `ghcr.io/liuyiwuqing/nexus-core@sha256:bcf7f6191d61e2f1cd93592ff4e4f9c44eaa42e07688d7cb243254800049a037` |
| 初始化镜像 | `ghcr.io/liuyiwuqing/nexus-core-init@sha256:70d178fb6236b68939438a5f22b151deb374bc72968c78efa2793216237c8055` |
| 平台 | `linux/amd64`、`linux/arm64`（index digest 与上表 CORE digest 一致） |
| 冒烟 | 7 项通过：初始化生成、重复拒绝、1Panel 编排解析、CORE 非根只读就绪、静态资源与鉴权、MQTT TLS、重建持久性 |
| 迁移核对 | 拉取 arm64 镜像后 `/app/core.jar` 的 `db/migration` 含 V1–V28；V27、V28 的 SHA-256 与源码一致 |
| 边界 | 仅隔离镜像容器；无目标机、生产库、HTTPS/DNS、真实供应商或设备 |

产物下载于本机 `/tmp/nexus-publish-45103288550`（临时目录，不入库）；部署者应在发布工作站归档到 `/secure-release/45103288550`。后续 G1 起统一使用上述两个 digest；`acc95fb` 仅保留为 B 路线回退。

## 4. 角色、窗口与停止条件

| 角色 | 职责 |
| --- | --- |
| 变更负责人（用户） | 确认目标机、窗口、域名、设备与是否允许发送测试消息 |
| 执行人 | 有 SSH 与 1Panel 权限，执行本计划并回填证据 |
| 复核人 | 复核备份可恢复、迁移结果与回滚路径；不参与同一命令的盲操作 |
| 观测人（可选） | 部署后 24 小时看健康、日志、磁盘与证书 |

窗口建议：首次部署至少预留 2 小时；既有安装升级至少 1 小时；数据库较大或需恢复演练时单独延长。窗口内不做其他 1Panel 编排、证书或防火墙变更。

出现以下任一情况立即停止，不继续“试试看”：

- 预检不满足最低资源、DNS、端口或时间同步。
- `/etc/nexus/docker` 已存在却准备执行初始化。
- 备份未完成、校验失败、隔离恢复失败。
- `flyway_schema_history` 出现失败行，或迁移日志有 SQL 错误。
- CORE 健康超过 10 分钟未就绪，或反复重启。
- 出现无法解释的 5xx、MQTT 认证循环、磁盘低于 20%。
- 任何步骤要求删除 `postgres-data`、`console-sessions`、CA 或设备配置。

## 5. G1 目标机预检

以具有 Docker 权限的账号登录目标机。命令只读，不修改服务：

```sh
uname -a
uname -m
cat /etc/os-release
docker version --format 'server={{.Server.Version}}'
docker compose version
df -h / /var/lib/docker /etc /var/backups
free -m
nproc
timedatectl status
ss -lntp | grep -E ':(80|443|8883|18088)\b' || true
getent ahosts pet.muyin.site
getent ahosts mqtt.muyin.site
docker compose ls
docker volume ls
```

| 检查项 | 最低要求 | 不满足时 |
| --- | --- | --- |
| CPU/内存 | 2 vCPU、2 GiB；建议 4 GiB | 先扩容；CORE 自身限制 1 GiB，数据库与 1Panel 另计 |
| 磁盘 | Docker/数据库所在分区 ≥10 GiB 可用 | 清理或扩容；不要先拉镜像 |
| 备份空间 | 独立位置 ≥ 当前数据库大小 ×3 | 准备加密备份位置后再升级 |
| Docker/Compose | Docker 24+、Compose v2 | 由 1Panel 升级；不要用旧 `docker-compose` |
| 1Panel/OpenResty | 已运行，80/443 可用 | 先修面板；不直接暴露 CORE |
| DNS | 两个域名解析到目标机；MQTT 不走 CDN 代理 | 先改 DNS 并等待生效 |
| 端口 | 80/443 由 1Panel；8883 仅在接设备时开放；18088 只监听回环 | 调整防火墙/安全组 |
| 时间 | NTP 同步正常 | 先修时间，否则 TLS/证书异常 |
| GHCR | 能拉取两个镜像，或离线包已就位 | 走 5.2 离线路线 |
| 既有安装 | 记录项目名、卷名、镜像、迁移版本、私有目录 | 若存在，禁止执行首次初始化 |

### 5.1 在线拉取检查

```sh
docker pull "$NEXUS_INIT_IMAGE"
docker pull "$NEXUS_CORE_IMAGE"
docker image inspect "$NEXUS_CORE_IMAGE" \
  --format '{{.Os}}/{{.Architecture}} {{index .RepoDigests 0}}'
```

- 若返回未授权：在提示符输入有 `read:packages` 权限的 PAT，或使用 1Panel 镜像仓库配置；不要把 PAT 写进命令、编排或聊天。
- 若 GHCR 层 TLS 超时：不要反复重试，改走离线路线。
- 目标机是 amd64 时，镜像架构必须是 `linux/amd64`；Apple Silicon 本机构建的本地镜像不能直接当服务器镜像。

### 5.2 离线导入路线

在可联网的工作站按目标架构准备四个镜像：CORE、init、`postgres:17-alpine`、`eclipse-mosquitto:2.0.22`。后两个的 digest 以部署编排中的值为准。

```sh
docker pull --platform linux/amd64 "$NEXUS_CORE_IMAGE"
docker pull --platform linux/amd64 "$NEXUS_INIT_IMAGE"
docker pull --platform linux/amd64 postgres:17-alpine@sha256:<postgres-digest>
docker pull --platform linux/amd64 eclipse-mosquitto:2.0.22@sha256:<mosquitto-digest>
docker save -o nexus-acc01-images.tar \
  "$NEXUS_CORE_IMAGE" "$NEXUS_INIT_IMAGE" \
  postgres:17-alpine@sha256:<postgres-digest> \
  eclipse-mosquitto:2.0.22@sha256:<mosquitto-digest>
sha256sum nexus-acc01-images.tar
```

传输后在服务器校验并导入：

```sh
sha256sum nexus-acc01-images.tar
docker load -i nexus-acc01-images.tar
docker image inspect "$NEXUS_CORE_IMAGE" \
  --format '{{.Os}}/{{.Architecture}} {{index .RepoDigests 0}}'
```

只导入 CORE 和 init 不够；首次部署时启动仍会尝试拉取 PostgreSQL/Mosquitto，离线环境会失败。既有安装升级时后两者已在本地，只需导入 CORE（本次 G4 即如此）。

### 5.3 G1 只读盘点（2026-11-23）

通过 `nexus-deploy`（docker 组、无免密 sudo）登录，只执行查询，未停止或修改服务。现有安装判定为**升级**，不得执行第 7 节首次初始化。

| 项 | 实际值 |
| --- | --- |
| 主机 | Debian 12 / x86_64；Docker 27.3.1 |
| Compose 项目 | `nexus-production`，服务 `core`、`postgres`、`mqtt` |
| 活动编排 | `/data/1panel/docker/compose/nexus-production/docker-compose.yml`（1Panel 管理，root-only，`nexus-deploy` 不可读） |
| 私有配置 | `/etc/nexus/docker`（root-only）；MQTT 证书/配置只读挂载；CORE 挂载 `ca.crt` |
| CORE | `ghcr.io/liuyiwuqing/nexus-core:sha-230c809…`，healthy；`65534:65534`、只读根、1 GiB、`cap_drop: ALL`、`no-new-privileges` |
| PostgreSQL | `postgres:17-alpine@sha256:18cfe3ef…`，healthy，卷 `nexus-production_postgres-data`，库大小约 9.4 MB |
| MQTT | `eclipse-mosquitto:2.0.22@sha256:81a6193a…`，`0.0.0.0:8883`；数据/日志为匿名卷 |
| 迁移 | `flyway_schema_history` 最新 V23，全部 success；待执行 V24–V28 |
| 数据量 | `deliveries` 6 行、`device_telemetry_samples` 146 行 |
| 端口 | 80/443 OpenResty；`127.0.0.1:18088`；`0.0.0.0:8883` |
| HTTPS | `pet.muyin.site` 返回 CORE `401`；Let's Encrypt 有效至 2027-02-13 |
| DNS | `pet.muyin.site → 106.54.109.58`；`mqtt.muyin.site` 无 A 记录 |
| MQTT 证书 | CN `pet.muyin.site`，SAN 含 `pet.muyin.site`、`mqtt.muyin.site`、`mqtt`、`localhost`、`127.0.0.1` |
| 会话卷 | 不存在；旧镜像无 `NEXUS_AUTH_SESSION_DIRECTORY`，默认目录在只读根上，`remember` 会话不可用 |

G1 结论与待办：

- 走第 8 节升级路径；保留项目名、卷、CA、设备配置与现有 env（Halo/成长同步/OTA 已开启，AI/记忆/清理/桥接关闭）。
- 活动编排不是 `/etc/nexus/docker/compose.1panel.yml`；修改必须在 1Panel 编排编辑器或用 root 对该实际文件操作，不能另建第二份编排。
- 升级时补 `console-sessions` 卷；新镜像已内置 `NEXUS_AUTH_SESSION_DIRECTORY=/var/lib/nexus/sessions`，空卷会继承镜像目录属主。
- `mqtt.muyin.site` 缺 A 记录；2026-11-23 决定统一使用 `pet.muyin.site:8883`（证书 SAN 已覆盖），不新增 `mqtt` A 记录；设备配网页 Broker 填 `pet.muyin.site`。
- 迁移量很小（V24 涉及 146 行，V27/V28 各 6 行），但仍按第 6 节备份并在维护窗口停 CORE 写入。
- 目标机 available 约 940 MB；CORE 当前占用 145.5 MiB/1 GiB，重启预计可容纳，但需在窗口内观察。

## 6. G2 既有安装备份与恢复演练

仅当 G1 发现已有安装时执行；首次部署跳到第 7 节。

先记录现状，再停写备份。以下命令以项目名 `nexus-production` 为例；实际值以 `docker compose ls` 和 1Panel 编排为准，不能另建项目名。

权限边界：`nexus-deploy` 无免密 sudo。`pg_dump` 与临时恢复容器可经 docker 组执行；读取活动编排、`/etc/nexus/docker` 以及写入 `/var/backups` 需要 root 或 1Panel 终端。没有 root 时把 dump 放到 `~/nexus-backups`（700），私有目录包由 root 补做，不能跳过异机副本。

```sh
PROJECT=nexus-production
PG_ID=$(docker ps -q \
  --filter "label=com.docker.compose.project=$PROJECT" \
  --filter "label=com.docker.compose.service=postgres")
CORE_ID=$(docker ps -q \
  --filter "label=com.docker.compose.project=$PROJECT" \
  --filter "label=com.docker.compose.service=core")
test -n "$PG_ID" && test -n "$CORE_ID"

docker inspect "$CORE_ID" --format 'image={{.Config.Image}} id={{.Image}}'
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select version, description, success, installed_on from flyway_schema_history order by installed_rank desc limit 5;"
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select 'devices' as table_name, count(*) from devices union all select 'messages', count(*) from messages union all select 'deliveries', count(*) from deliveries;"
```

停 CORE 写入，保留 PostgreSQL 运行：

```sh
docker stop "$CORE_ID"
```

创建备份；目录必须存在且仅管理员可读：

```sh
install -d -m 700 /var/backups/nexus
umask 077
TS=$(date -u +%Y%m%dT%H%M%SZ)
DUMP="/var/backups/nexus/nexus-before-acc01-$TS.dump"

docker exec -i "$PG_ID" pg_dump -U nexus -d nexus -Fc > "$DUMP"
sha256sum "$DUMP" | tee "$DUMP.sha256"
if command -v pg_restore >/dev/null; then pg_restore -l "$DUMP" | head -20; fi

tar -C /etc -czf "/var/backups/nexus/nexus-private-$TS.tar.gz" nexus/docker
sha256sum "/var/backups/nexus/nexus-private-$TS.tar.gz" \
  | tee "/var/backups/nexus/nexus-private-$TS.tar.gz.sha256"
```

将 dump、私有目录包和校验文件复制到受控的异机/加密位置，并在两端复核 sha256。私有目录包含管理员令牌、数据库/MQTT 密码、CA 和设备配置，不能提交 Git、不能贴到聊天或工单明文。

### 6.1 隔离恢复验证

在目标机用临时容器和临时卷验证，不接触生产卷、不监听端口：

```sh
RESTORE_VOLUME=nexus-restore-check-$(date -u +%s)
RESTORE_NAME=nexus-restore-check-$(date -u +%s)
docker volume create "$RESTORE_VOLUME"

docker run -d --name "$RESTORE_NAME" \
  --network none \
  -e POSTGRES_DB=nexus \
  -e POSTGRES_USER=nexus \
  -e POSTGRES_HOST_AUTH_METHOD=trust \
  -v "$RESTORE_VOLUME:/var/lib/postgresql/data" \
  postgres:17-alpine@sha256:18cfe3ef5e6815560c98237d6216d1e5119702fb0f3894c8785dd58b8bbe5d73

until docker exec "$RESTORE_NAME" pg_isready -U nexus -d nexus; do sleep 1; done

docker exec -i "$RESTORE_NAME" pg_restore -U nexus -d nexus \
  --no-owner --exit-on-error < "$DUMP"

docker exec -i "$RESTORE_NAME" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select version, success from flyway_schema_history order by installed_rank desc limit 5;"
docker exec -i "$RESTORE_NAME" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select 'devices' as table_name, count(*) from devices union all select 'messages', count(*) from messages union all select 'deliveries', count(*) from deliveries;"

docker rm -f "$RESTORE_NAME"
docker volume rm "$RESTORE_VOLUME"
```

通过标准：`pg_restore` 退出 0；迁移历史最后一条与升级前一致；关键表行数与备份前一致。失败则终止升级，不启动新镜像。

### 6.2 G2 执行记录（2026-11-23）

- 备份：`/home/nexus-deploy/nexus-backups/nexus-20261123T071328Z.dump`，94,826 B，SHA-256 `bb7070ca…99e8`；已复制到执行机 `~/nexus-backups/`，权限 600，哈希一致。
- 隔离恢复：临时卷/容器 `nexus-restore-check-20261123T071328Z`，`--network none`，`pg_restore --no-owner --exit-on-error` 退出 0；`deliveries` 6 行、`device_telemetry_samples` 146 行、库 9,166 kB，与生产一致；临时资源已删除。
- 更正：不要用 `max(version)` 判断 Flyway 版本（varchar 字典序会得到 9），必须按 `installed_rank` 排序；生产最新为 V23。
- 资源：CORE 145.5 MiB/1 GiB，PG 41.8 MiB，MQTT 4.7 MiB；宿主 available 约 940 MiB、swap 可用约 1.7 GiB，重启 CORE 预计可容纳，但仍需在窗口内观察。

## 7. G3 首次部署（无既有目录）

只有在 `test ! -e /etc/nexus/docker` 成立时执行。已有目录时跳到第 8 节，**绝不覆盖或重新初始化**。

```sh
set -eu
test ! -e /etc/nexus/docker
install -d -m 700 /etc/nexus
docker pull "$NEXUS_CORE_IMAGE"
docker pull "$NEXUS_INIT_IMAGE"

docker run --rm --read-only --cap-drop ALL \
  --security-opt no-new-privileges:true --tmpfs /tmp \
  --mount type=bind,source=/etc/nexus,target=/etc/nexus \
  "$NEXUS_INIT_IMAGE" \
  --data-dir /etc/nexus/docker \
  --domain pet.muyin.site \
  --mqtt-host pet.muyin.site \
  --device dodo-1 \
  --image "$NEXUS_CORE_IMAGE"
```

初始化后核对（不打印密钥）：

```sh
find /etc/nexus/docker -maxdepth 2 -printf '%M %u:%g %p\n' | sort
grep -n 'image: ghcr.io/liuyiwuqing/nexus-core@sha256:' \
  /etc/nexus/docker/compose.1panel.yml
grep -nE '^NEXUS_(DB_URL|MQTT_URI|MQTT_ALLOW_INSECURE)=' \
  /etc/nexus/docker/core.env
```

预期：目录 `700`、凭据/环境文件 `600`；CORE 镜像 digest 等于 G0 冻结值；`NEXUS_DB_URL` 指向 `postgres:5432`；`NEXUS_MQTT_URI` 为 `ssl://mqtt:8883`，不允许不安全连接。不要在终端 `cat` 整个 `core.env`。

在 1Panel「容器 → 编排 → 创建编排」填写项目名 `nexus-production`，粘贴 `/etc/nexus/docker/compose.1panel.yml` 全文，关闭强制拉取（镜像已存在时）。创建后等待 PostgreSQL、CORE 健康，MQTT 运行。

若改用命令行管理同一项目：

```sh
docker compose -p nexus-production \
  -f /etc/nexus/docker/compose.1panel.yml \
  up -d --wait --wait-timeout 180
docker compose -p nexus-production \
  -f /etc/nexus/docker/compose.1panel.yml ps
```

编排所有者必须二选一：1Panel 或命令行。不要一边在 1Panel 保存旧内容、一边用命令行启动新内容，造成下一次面板操作回滚镜像。

## 8. G4 既有安装升级与数据库迁移

### 8.1 迁移前判断

先看当前迁移版本和 `deliveries` 规模：

```sh
PROJECT=nexus-production
PG_ID=$(docker ps -q \
  --filter "label=com.docker.compose.project=$PROJECT" \
  --filter "label=com.docker.compose.service=postgres")

docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select version, description, success, installed_on from flyway_schema_history order by installed_rank desc limit 6;"
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select reltuples::bigint as estimated_rows, pg_size_pretty(pg_total_relation_size(oid)) as size from pg_class where oid = 'deliveries'::regclass;"
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select pid, state, wait_event_type, left(query, 80) from pg_stat_activity where datname = 'nexus' and pid <> pg_backend_pid();"
```

判定：

| 当前状态 | 新镜像启动后 | 说明 |
| --- | --- | --- |
| 空库/全新卷 | Flyway 执行 V1–V28 | 首次部署正常路径 |
| V26 或更早 | 顺序补齐到 V28 | V27 建部分索引；V28 加可空列和两条 CHECK 约束 |
| V27 | 只执行 V28 | 同上 |
| V28 | 无迁移 | 校验 `failed=0` 与数据计数即可 |
| 有失败行或校验和不一致 | 停止 | 不手工删历史行、不改已执行迁移；恢复备份或新增加前滚迁移 |

V27/V28 的锁风险：`CREATE INDEX` 不是并发建索引；`ADD CONSTRAINT ... CHECK` 会扫描 `deliveries` 并在迁移事务内持有较强锁。表很小时是秒级；表大或磁盘慢时要在维护窗口执行，期间不要重启 CORE。禁止为了“提前优化”手工先建索引或约束，Flyway 会因对象已存在而失败。

### 8.2 备份与停写

按第 6 节完成备份、异机复制和隔离恢复验证。确认 `$DUMP` 与校验文件可读后，停止 CORE：

```sh
CORE_ID=$(docker ps -q \
  --filter "label=com.docker.compose.project=$PROJECT" \
  --filter "label=com.docker.compose.service=core")
docker stop "$CORE_ID"
```

保留 PostgreSQL、MQTT 和 1Panel 反代运行；反代此时会返回 502，属于维护窗口预期。不要停 PostgreSQL，否则备份与迁移都无法进行。

### 8.3 更新镜像并启动

先备份编排，修改前后只允许镜像行变化。实际路径以 Compose 标签为准（本机为 `/data/1panel/docker/compose/nexus-production/docker-compose.yml`）；该文件与 `/etc/nexus/docker` 均为 root-only，`nexus-deploy` 不可读。修改统一在 1Panel 编排编辑器完成，不要另建第二份编排。需要命令行核对时由 root 执行：

```sh
COMPOSE=$(docker inspect "$CORE_ID" \
  --format '{{index .Config.Labels "com.docker.compose.project.config_files"}}')
sudo cp -a "$COMPOSE" "$COMPOSE.bak-$(date -u +%Y%m%dT%H%M%SZ)"
```

在 1Panel 编排编辑器中把 CORE 的 `image` 替换为 G0 冻结的 digest；活动编排就是唯一副本，不再维护 `/etc/nexus/docker/compose.1panel.yml` 第二份。若走 5.2 离线导入，`docker load` 不保留 digest 引用，`image` 必须改用本地 tag（本次为 `:acc01-d2711f7`）并加 `pull_policy: never`，否则 Compose 无法按 digest 找到本地镜像。若旧编排没有登录会话卷，按 [CORE 1Panel 部署说明](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/1panel-deployment.md) 补齐：

- `core.environment` 增加 `NEXUS_AUTH_SESSION_DIRECTORY: /var/lib/nexus/sessions`
- `core.volumes` 增加 `console-sessions:/var/lib/nexus/sessions`
- 顶层 `volumes` 增加 `console-sessions:`

保存前核对：

```sh
grep -n 'image: ghcr.io/liuyiwuqing/nexus-core@sha256:' "$COMPOSE"
diff -u "$(ls -1t "$COMPOSE".bak-* | head -1)" "$COMPOSE"
```

预期 diff 只有 `image` 行（以及首次补会话卷时的三处）。确认新镜像已存在：

```sh
docker image inspect "$NEXUS_CORE_IMAGE" \
  --format '{{.Os}}/{{.Architecture}} {{index .RepoDigests 0}}'
```

在 1Panel 重新部署该编排；若 1Panel 不支持就地更新，用同一项目名执行：

```sh
sudo docker compose -p nexus-production \
  -f "$COMPOSE" \
  up -d --wait --wait-timeout 180 core
```

不要新建项目名，不要复用“新建编排”迁移数据库；项目名变化会创建新卷，看起来像数据丢失。

### 8.4 迁移过程观察

```sh
docker logs -f --tail 200 "$(docker ps -q \
  --filter "label=com.docker.compose.project=nexus-production" \
  --filter "label=com.docker.compose.service=core")"
```

关注：

- Flyway 依次执行缺失版本，最后到该镜像内的最高版本（A 路线 V28；B 路线无新迁移）。
- 无 `Migration ... failed`、`SQL State`、连接超时或反复重启。
- 健康检查在 10 分钟内通过；MQTT 恢复 `ready`。
- 迁移期间不要重复 `docker restart` 或并发启动第二个 CORE。

### 8.5 G4 执行记录（2026-11-23，离线导入）

镜像准备，全程跳过 GHCR 拉取：

| 项 | 值 |
| --- | --- |
| 本地归档 | `/tmp/nexus-core-acc01-amd64.tar`，319,047,168 B，SHA-256 `7bc6bc5909e7897d6614be70fc01da9f989985741939a10a053779a9c5528deb` |
| 服务器归档 | `/home/nexus-deploy/images/nexus-core-acc01-amd64.tar`，尺寸与 SHA-256 一致 |
| `docker load` | 成功；镜像 ID `sha256:a8f98e163337def81f4136d862851edaa60452124fd153543114927a4fe00700`，`linux/amd64`，`revision=d2711f7` |
| 分层 | 8 层 diffID 与本地逐层一致；`RepoDigests=[]` |
| 本地 tag | `ghcr.io/liuyiwuqing/nexus-core:acc01-d2711f7` |

编排只改 CORE 四处：`image` 改为上述本地 tag、加 `pull_policy: never`、加 `NEXUS_AUTH_SESSION_DIRECTORY: /var/lib/nexus/sessions`、挂载 `console-sessions:/var/lib/nexus/sessions` 并声明顶层卷；PostgreSQL、Mosquitto、网络与安全配置未动。新编排 SHA-256 `b7760ab206d4610461dff13fa8dd19feaa0720c64542f817e5d101f908b6c6ab`，旧编排备份 `/home/nexus-deploy/backups/nexus-production.docker-compose.yml.20261123`，SHA-256 `c0149c62db20c06d209f2749bd62dc6de1216cb7106c78558889d72668730fc3`。

执行方式：`nexus-deploy` 无免密 sudo，但属于 docker 组。使用本地 CORE 镜像作为一次性 root 客户端，挂载宿主 `docker.sock`、`/usr/bin/docker`、compose 插件、活动编排目录与 `/etc/nexus/docker`，以宿主真实路径执行 `docker compose -f /data/1panel/docker/compose/nexus-production/docker-compose.yml -p nexus-production up -d --no-deps --force-recreate core`。只读检查 `agent.db` 确认 `composes` 表只保存 `path`、不保存 YAML，1Panel 编排是文件驱动，因此直接改文件不会产生第二份内容。

结果：

- 首次重建 07:32:22Z 启动，12 s 进入 healthy；Flyway 从 V23 顺序执行 V24–V28，07:32:27Z 完成，5 个迁移全部成功。因首次 compose 客户端路径为容器内 `/w/...`，07:34 用宿主真实路径强制重建一次修正 `com.docker.compose.project.config_files` 标签；第二次启动显示 “No migration necessary”。
- `flyway_schema_history` 最高 `installed_rank=28`，`bool_and(success)=t`；`deliveries` 6 行，`device_telemetry_samples` 147 行（迁移未改变行数；第 147 条是 CORE 启动时补入库的保留遥测，`observed_at=2026-11-18 16:43:37Z`、`received_at=2026-11-23 07:32:30.495093Z`，设备 `dodo-1` 自 2026-11-18 16:43:29Z 后未在线，不是升级窗口内的实时样本）。
- 运行镜像 ID `sha256:a8f98e…`，`65534:65534`、只读根、1 GiB、`cap_drop: ALL`、重启计数 0。
- 会话卷 `nexus-production_console-sessions` 已创建；容器内 `/var/lib/nexus/sessions` 为 `nobody:nogroup 700`，写入探测成功。
- `https://pet.muyin.site/` 返回 200（管理台登录页），`/api/console/session` 与 `/api/v1/deliveries` 返回 401。
- Mosquitto 日志显示 CORE 以 `nexus-core` 重新连接（172.20.0.2:49224），旧连接已断开。
- 资源：宿主 available 约 1.0 GiB，根分区剩余约 14 GB。

边界：本次是既有安装升级，不导入 init 镜像、不执行首次初始化；未重建 PostgreSQL/Mosquitto；未发送真实消息；未做 24 小时观测；HTTPS/SSE 运行时检查见 10.2.1，设备 MQTT 改指 `pet.muyin.site:8883` 与实机联调仍待执行。回滚若跨越 V28，必须同时恢复 G2 备份，不能只换回旧镜像。

## 9. G5 数据库迁移验证

```sh
PG_ID=$(docker ps -q \
  --filter "label=com.docker.compose.project=nexus-production" \
  --filter "label=com.docker.compose.service=postgres")

docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select version, description, success, installed_on from flyway_schema_history order by installed_rank desc limit 6;"
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select count(*) as failed from flyway_schema_history where not success;"
docker exec -i "$PG_ID" psql -U nexus -d nexus -v ON_ERROR_STOP=1 \
  -c "select 'devices' as table_name, count(*) from devices union all select 'messages', count(*) from messages union all select 'deliveries', count(*) from deliveries;"
```

通过标准：

- 最新版本等于部署镜像内的最高迁移（A 路线 `28`，B 路线 `26`），`failed = 0`。
- 关键表行数不小于升级前记录；若有差异，能由窗口内的正常写入或既有清理策略解释，不能无解释缺失。
- CORE 日志没有迁移错误；`/api/v1/status` 的 `database` 与 `mqtt` 均为 `ready`。
- 若迁移失败：保留日志与数据库现场，停止重试；不要手工修改 `flyway_schema_history` 或删除半成品对象，按第 11 节回滚。

首次部署不需要“升级前备份”，但迁移成功后立即做一次基线备份：

```sh
umask 077
TS=$(date -u +%Y%m%dT%H%M%SZ)
docker exec -i "$PG_ID" pg_dump -U nexus -d nexus -Fc \
  > "/var/backups/nexus/nexus-baseline-$TS.dump"
sha256sum "/var/backups/nexus/nexus-baseline-$TS.dump"
```

## 10. G6 代理、HTTPS、SSE 与部署后验证

### 10.1 反向代理

在 1Panel 为 `pet.muyin.site` 配置 HTTPS 反向代理：

- OpenResty 为 host 网络：上游 `http://127.0.0.1:18088`。
- OpenResty 为 bridge 网络：让 OpenResty 加入 CORE 的 `edge` 网络，上游 `http://core:8088`；不能把 OpenResty 自己的 `127.0.0.1` 当宿主机。
- 不把 18088、5432 发布到公网；数据库只在私有网络。
- 证书由 1Panel 申请/续期；确认 80/443 可达。

SSE 接口 `/api/v1/events` 需要关闭代理缓冲并保留合理读取超时；页面有轮询回退，但不应把缓冲当作正常状态。参考 [CORE 实时更新说明](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/console-events-and-setup.md)。

### 10.2 验证清单

先定位容器；如果任一变量为空，停止核对，不要继续。下表中的 `$TOKEN` 先在受控终端读取，例如 `read -rsp 'Admin token: ' TOKEN; echo`，用完 `unset TOKEN`；不要把令牌写进命令历史、URL 或日志。

```sh
PROJECT=nexus-production
CORE_ID=$(docker ps -q --filter "label=com.docker.compose.project=$PROJECT" --filter "label=com.docker.compose.service=core")
PG_ID=$(docker ps -q --filter "label=com.docker.compose.project=$PROJECT" --filter "label=com.docker.compose.service=postgres")
test -n "$CORE_ID" && test -n "$PG_ID"
```

| 检查 | 命令/方法 | 通过标准 |
| --- | --- | --- |
| 容器状态 | 1Panel 编排状态或 `docker ps --filter label=com.docker.compose.project=nexus-production` | postgres/core healthy，mqtt running |
| 镜像 | `docker inspect "$CORE_ID" --format '{{.Config.Image}} {{.Image}}'` | 等于 G0 digest |
| 安全选项 | `docker inspect "$CORE_ID" --format 'user={{.Config.User}} ro={{.HostConfig.ReadonlyRootfs}} mem={{.HostConfig.Memory}} capdrop={{.HostConfig.CapDrop}}'` | `65534:65534`、只读、1 GiB、`[ALL]` |
| 健康 | `curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:18088/api/v1/status` | 无令牌 `401` |
| 状态 | 安全读取管理员令牌后请求 `/api/v1/status` | `database=ready`、`mqtt=ready` |
| 迁移 | 第 9 节 SQL | 等于镜像内最高迁移（A：28；B：26），`failed=0` |
| 会话目录 | `docker exec "$CORE_ID" ls -ldn /var/lib/nexus/sessions` | `65534:65534`、`700` |
| 端口 | `ss -lntp \| grep -E ':(18088\|5432)\b'` | 18088 仅 `127.0.0.1`；无 5432 公网监听 |
| HTTPS | `curl -sS -o /dev/null -w '%{http_code} %{ssl_verify_result}\n' https://pet.muyin.site/` | `200 0` |
| MQTT TLS | `openssl s_client -connect 127.0.0.1:8883 -servername pet.muyin.site -verify_hostname pet.muyin.site -CAfile /etc/nexus/docker/mosquitto/certs/ca.crt -verify_return_error </dev/null` | `Verify return code: 0 (ok)` |
| SSE | `timeout 5 curl -sSi -H "Authorization: Bearer $TOKEN" https://pet.muyin.site/api/v1/events \| sed -n '1,12p'` | `200`、`text/event-stream`、`X-Accel-Buffering: no`；超时退出 124 属正常 |
| 登录 | 浏览器打开 HTTPS，管理员令牌登录 | 进入管理台；不把令牌写进 URL/本地存储 |
| 权限 | “权限”页签发 operator/viewer（按需） | 新令牌可用，旧令牌按策略失效 |
| 重启持久性 | 浏览器登录后重启 CORE，再刷新 | 8 小时内会话仍在；否则检查会话卷 |
| 设备 TLS | 经批准后把 `device.json` 交付测试设备并连接 | 设备上线；不把 CA 私钥/密码贴到聊天 |
| 消息冒烟 | 仅经用户批准，用测试设备发一条可回滚消息 | 投递、接收、回执可见；未批准则跳过，不能记为通过 |
| 日志 | `docker logs --tail 200 ... core` | 无持续异常、无迁移错误、无认证风暴 |

管理员令牌读取必须在受控终端完成，例如 `read -rsp 'Admin token: ' TOKEN; echo`，用完 `unset TOKEN`；不要把 `core.env` 输出到屏幕、日志或命令历史。

### 10.2.1 G6 运行时检查（2026-11-23）

在 G4 升级完成后执行；未发送真实消息，未修改 OpenResty 配置。

| 检查 | 实测 |
| --- | --- |
| 状态 | 管理员令牌请求 `/api/v1/status` 返回 `{"mqttError":"","schemaVersion":1,"database":"ready","mqtt":"ready"}` |
| 迁移 | `flyway_schema_history` 最高 V28，`failed=0` |
| HTTPS | `GET https://pet.muyin.site/` 200（管理台 SPA）；`HEAD /` 401；HTTP 301 跳 HTTPS；HSTS `max-age=31536000` |
| SSE | `/api/v1/events` HTTP/2 200、`content-type: text/event-stream`、`cache-control: no-store`、`x-nexus-role: admin`；`curl -N` 6 秒收到 `event:ready` 与多条 `event:heartbeat`，body 491 B，未发现代理缓冲 |
| 端口 | `127.0.0.1:18088` 仅回环；5432 无公网监听 |
| MQTT TLS | CN `pet.muyin.site`，issuer `NEXUS Local CA`，有效期至 2029-02-17，SAN 含 `pet.muyin.site`、`mqtt.muyin.site`、`mqtt`、`localhost`、`127.0.0.1` |

SSE 的 `X-Accel-Buffering: no` 由 CORE `ConsoleSetupController` 设置，OpenResty 将其作为内部控制头消费、不转发给客户端；未额外配置 `proxy_buffering off` 时仍能持续收到事件，因此不修改 `pet.conf`/`root.conf`。

会话与重启：`POST /api/v1/session`（`remember=true`）返回 200、`role=admin`、`persisted=true`、到期 2026-11-23T15:46:05Z；带 Cookie `GET /api/v1/session` 200，无 Cookie 401。07:46:12Z 主动重启 CORE 做持久性检查，14 s 后 healthy；Cookie 仍返回 200 且到期时间不变，MQTT 于 07:46:20Z 重连，`/api/v1/status` 双 ready。本次重启是有意验收，观测日志在该时刻后使用新的容器启动时间。未执行：浏览器 UI 登录、权限页签发；设备 `dodo-1` 自 2026-11-18 16:43:29Z 后离线，`pet.muyin.site:8883` 的真实设备连接与遥测无法验证，不记为通过。24 小时观测见 10.3。

管理员令牌模式：2026-11-23 将管理员令牌从 env 切换为受保护文件模式。编排新增 `NEXUS_AUTH_TOKEN_FILE=/etc/nexus/core-auth/tokens.json` 和宿主 `/etc/nexus/docker/core-auth` 目录挂载；宿主目录/`tokens.json`/`tokens.json.lock` 属主 65534:65534，权限 700/600/600。旧编排备份 `docker-compose.yml.bak-auth-20261123`（SHA-256 `b7760ab…`），新编排 `ad3d737…`；令牌文件由当前令牌初始化（revision=1、actor `cf739d…`），CORE 07:52:41Z healthy，`GET /api/v1/access/admin-token` 返回 `{"mode":"file","editable":true,"revision":1}`，管理员登录 200。`core.env` 的旧 `NEXUS_ADMIN_TOKEN` 在文件模式下不再校验，仅保留用于回滚；管理台“权限 → 修改管理员令牌”现可自定义新令牌，保存后立即生效、无需重启。

### 10.3 24 小时观测

- 每 15 分钟检查一次容器健康与 `/api/v1/status`；至少覆盖一个业务低峰。
- 观察磁盘增长、PostgreSQL 连接数、CORE 内存、MQTT 重连、5xx 与证书到期时间。
- 记录一次异机备份完成与校验结果。
- 不启用真实清理、OTA、AI、Halo、Bridge、成长同步；这些默认关闭，ACC-01 不改变默认值。
- 实机在线时长、声学、断网掉电与传感器稳定性属于 ACC-02，不以 24 小时管理台观测替代。

### 10.3.1 观测执行状态（2026-11-23）

- 观测脚本 `~/nexus-observe/observe.sh`（`nexus-deploy`，`nohup setsid` 后台，PID 946298），日志 `~/nexus-observe/observe-20261123T074228Z.ndjson`，07:42:28Z 启动，每 900 s 一条，计划到 2026-11-24 07:42Z。
- 07:52:03Z 更新脚本后以同一日志续跑：令牌优先从容器内 `/etc/nexus/core-auth/tokens.json` 读取，缺失时回退 `NEXUS_ADMIN_TOKEN`；重启后首条样本三服务正常、status 双 ready。
- 截至 07:56Z，access.log 最近 200 行共 4 条 502：07:46:14/17 与 07:52:42/45，分别对应会话持久性和令牌文件模式两次计划内重建；无其他 5xx。
- 每条记录：CORE/PostgreSQL/Mosquitto 状态与重启计数、`/api/v1/status`、根分区可用量与使用率、PostgreSQL 连接数、CORE 内存、16 分钟内 MQTT 重连数、access.log 最近 200 行 5xx 数、HTTPS/MQTT 证书到期时间。
- 首条样本：三服务正常（CORE/PostgreSQL healthy、MQTT running，重启 0），status `database=ready`、`mqtt=ready`，根分区可用 16,356,420 kB（已用 81%），PostgreSQL 连接 12，CORE 223.7 MiB/1 GiB，MQTT 重连 0，5xx 0，HTTPS 证书至 2027-02-13，MQTT 证书至 2029-02-17。
- 异机备份：服务器 `~/nexus-backups/acc01-observe-20261123T074441Z.dump`，107,230 B，SHA-256 `258df4d00bc085f442d030389afc33f7c92875f050723b7931a89936e1f05998`，`pg_restore --list` 含 42 个 TABLE DATA 段；已复制到观测工作站 `~/.nexus-evidence/acc01/`，本地哈希一致。
- 07:46:12Z 为验证会话持久性主动重启 CORE 一次，14 s 恢复，MQTT 07:46:20Z 重连，status 双 ready；该时刻是计划内中断，后续样本使用新的容器启动时间。
- 状态：观测进行中，24 小时未满，不记为通过；设备 `dodo-1` 仍离线，实机 MQTT/遥测不由本观测替代。

### 10.3.2 设备回归与 dodo-1 在线监控（2026-11-23）

- `dodo-1` 自 2026-11-18 16:43Z 离线后，于 2026-11-23 12:56:47Z 重新上报遥测（RSSI -41/-43），`devices.last_seen` 此后持续刷新；说明设备已按 G1 决定改用 `pet.muyin.site:8883` 成功接入生产 MQTT/TLS，设备 TLS 路线由真实设备验证。
- 新增服务器侧设备在线监控：`~/nexus-device-observe.sh`（`nexus-deploy`，`nohup setsid` 后台），每 300 s 只读查询 `devices`/`device_telemetry_samples`，记录 `fresh_online`（`online` 且 90 秒内有活动）、`last_seen`、最近 RSSI、16 分钟内断开事件；在线/离线翻转时附加 `event` 标记。日志 `~/nexus-observe/device-dodo-1.ndjson`；不写库、不重启服务、不输出凭据。
- 执行工作站另建每 30 分钟定时巡检（Schedule 任务 `c5a90b00`）：SSH 只读核对在线状态与观测脚本存活，正常时简报，离线/查询失败/断开事件激增时报告证据与排查方向，不自行重启容器或改配置。
- 边界：设备监控只记录连接状态与遥测，不发送测试消息；断开原因码分析、长稳与声学仍属 ACC-02。

### 10.3.3 观测结论（2026-11-24，ACC-01 窗口回填）

窗口在 `2026-11-24T07:55:07Z` 收尾（`start` 07-23T07:42:28Z，`stop` 07-24T07:55:07Z，97 条样本/期望 96 条）。用新增的 `tools/observe_report.py` 跑出来的结论是 **REVIEW，不能判定通过**：

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| 窗口闭环 / 时长 / 样本数 | 通过 | 23.96 h 对声明 24 h；97 条对 96 条 |
| 无采样断档 | 通过 | 0 处断档 |
| 容器未重启 | 通过 | `core=0 pg=0 mqtt=0` |
| 容器状态正常 | 通过 | 97 条全 running/healthy |
| **status 通道可用** | **待复核** | ready 仅 4 条，**93 条是 `{"error":"unauthorized"}`**，从 `2026-11-23T08:37:09Z` 一直持续到窗口结束 |
| 5xx 恒定 | 待复核 | 3 条样本的近 200 行日志含 5xx（`07:52`、`08:07`、`08:22`，对应脚本更新与 `07:46` 计划内重启的滚动残留，非增量） |

**根因**：采集脚本从 `docker cp ... - | awk '/"token"/ {print $2}'` 里抓令牌——那是 **tar 流**，`awk` 命中 `"token"` 后取第二个字段，只有 `tokens.json` 是**多行缩进**排版时才碰巧得到正确值。`/etc/nexus/docker/core-auth/tokens.json` 在 `2026-11-23T08:32Z` 被重写（现为 210 字节、0 换行、以 `{"admin":{"current":{` 开头的单行紧凑 JSON，`format=1`、`revision=2`），`awk` 取到的是整行片段，于是之后每条样本都 401。这份重写在仓库里**没有对应记录**，我无法归因（若是控制台「修改管理员令牌」，应补进台账：它会让所有旧令牌立即失效）。

**为什么一直没被发现**：`401` 被原样写进 `status` 字段，容器健康、5xx 为 0、磁盘正常，日志表面一切正常；§10.3.1 末条停在「观测进行中，24 小时未满」，此后没人回头核对通道是否还有效。

**仍然有效的证据**（不依赖令牌的通道完整覆盖 24 小时）：
- 磁盘可用 `16,356,420 kB → 14,280,124 kB`（-2.03 GB，最低 `13,990,300 kB`），使用率峰值 84%；同期主机还跑着与本项目无关的容器，读数含它们的增长。
- CORE 内存 `155.4–299.7 MiB`（限额 1 GiB）、PostgreSQL 连接恒为 12、16 分钟窗内 MQTT 重连合计 0。
- HTTPS 证书最早到期 `2027-02-13T14:59:31`、MQTT 证书 `2029-02-17T17:11:14`，24 小时内无变化。
- 设备侧同期（`device-dodo-1.ndjson`，284 条/23.59 h）：`dodo-1` 在 `11-23T14:47:48Z` 离线、`11-24T13:08:16Z` 才回来，窗口内 268 条样本（约 22.26 h）不在线，在线段 RSSI `-46…-35 dBm`。

**结论口径**：ACC-01 的 24 小时观测只能确认「服务不重启、资源平稳、证书有效」，**不能**确认「`/api/v1/status` 24 小时双 ready」——那 24 小时里只有 4 条有效证据。要改记为通过，必须用修好的采集脚本重跑一轮（§19 已在跑 ACC-04 的窗口）。

**工具**：`tools/observe_report.py`（标准库，只读）把窗口 ndjson 变成上表，并把「服务不健康」与「采集通道失效」分开——这两者在原始日志里长得一样，正是本轮被漏掉的原因。`tools/ci/test_observe_report.py` 5 项覆盖全绿窗口、401 通道、断档+重启+未闭环、非法日志、设备侧离线段。

## 11. 回滚

### 11.1 决策树

1. 新镜像未启动或未执行任何迁移：把编排镜像改回旧 digest，重新部署 CORE；数据库不变。
2. 迁移已成功但应用异常：先判断是否与 V27/V28 无关（配置、代理、MQTT、会话卷）。V27 只加索引，V28 只加可空列和 CHECK，旧代码理论上可运行；仍以启动日志和冒烟为准，不能把“理论兼容”当作已验证。
3. 迁移失败或数据异常：停止 CORE，保留日志和数据库现场，按 11.2 用备份恢复；不要只切旧镜像假定数据库可回退。
4. 仅 HTTPS/SSE/上游错误：保持 CORE 与数据库不动，修 1Panel 代理；不回滚数据库。
5. MQTT 证书/端口错误：修证书路径和防火墙；不要重新运行初始化，否则新 CA 会让已有设备失联。

### 11.2 应用回滚（未迁移或兼容）

```sh
# 在 1Panel 编排中把 CORE image 改回记录的旧 digest，重新部署
docker inspect "$(docker ps -q \
  --filter "label=com.docker.compose.project=nexus-production" \
  --filter "label=com.docker.compose.service=core")" \
  --format '{{.Config.Image}}'
```

核对旧镜像仍在本地；若已删除，按旧 digest 重新拉取或从保留的离线包导入。回滚后重复第 10.2 节的状态、迁移历史与登录检查。

### 11.3 数据库恢复（迁移失败或必须回到旧 schema）

推荐恢复到新的隔离项目/卷，保留失败现场，不删除原 `postgres-data`：

```sh
# 1. 停止失败项目的 CORE 与 MQTT，释放 18088/8883；保留 PostgreSQL、卷和日志
docker stop \
  "$(docker ps -q --filter "label=com.docker.compose.project=nexus-production" --filter "label=com.docker.compose.service=core")" \
  "$(docker ps -q --filter "label=com.docker.compose.project=nexus-production" --filter "label=com.docker.compose.service=mqtt")"

# 2. 复制编排为回滚编排，只把 CORE image 改成旧 digest
cp /etc/nexus/docker/compose.1panel.yml /etc/nexus/docker/compose.rollback.yml
# 在编辑器中修改 compose.rollback.yml 的 image

# 3. 用新项目名启动独立 PostgreSQL 卷（端口暂不冲突，因为失败项目 CORE 已停止）
docker compose -p nexus-rollback -f /etc/nexus/docker/compose.rollback.yml up -d postgres
RB_PG=$(docker ps -q --filter "label=com.docker.compose.project=nexus-rollback" --filter "label=com.docker.compose.service=postgres")
until docker exec "$RB_PG" pg_isready -U nexus -d nexus; do sleep 1; done

# 4. 恢复已验证的 dump
docker exec -i "$RB_PG" pg_restore -U nexus -d nexus --no-owner --exit-on-error < "$DUMP"

# 5. 启动回滚项目的 CORE/MQTT；原项目已停止，18088/8883 端口可用
docker compose -p nexus-rollback -f /etc/nexus/docker/compose.rollback.yml up -d --wait --wait-timeout 180

# 6. 验证旧版本 schema、数据计数、登录和 MQTT
```

注意：

- `nexus-rollback` 会创建自己的 `postgres-data`、`console-sessions` 卷；不要挂载失败项目的卷。
- 配置 bind 路径仍指向 `/etc/nexus/docker`，CA、设备密码和 Broker ACL 保持不变；不要重新初始化。
- 原 `nexus-production` 的卷和容器保留为故障现场。清理必须在取证、复核和用户批准后单独执行，禁止对生产项目直接 `down -v`。
- 回滚成功后，何时切回生产项目、如何重新升级，另开变更；不能在同一个窗口反复升级/回滚。

## 12. 风险与边界

| 风险 | 触发 | 应对 |
| --- | --- | --- |
| 当前 HEAD 无已发布镜像 | 直接部署当前代码 | 先走 G0；或明确选择 `acc95fb` 并接受功能冻结 |
| 迁移锁表 | `deliveries` 很大，V27/V28 建索引/加约束 | 预检规模、留足窗口、停写迁移；不手工预建对象 |
| 迁移失败 | SQL 错误、磁盘满、锁超时 | 停止重试，保留现场，恢复备份；不改历史表 |
| GHCR 限流/TLS 超时 | 在线拉取失败 | 离线导入四个镜像；核对架构与 digest |
| 反代网络选错 | bridge 下用 `127.0.0.1` 当宿主 | 加 `edge` 网络，代理 `core:8088` |
| SSE 被缓冲 | 代理默认缓冲 | 关闭该接口缓冲、保留读取超时；轮询回退可用 |
| 会话卷缺失 | 旧编排未加卷 | 补 `console-sessions` 与目录 env；旧会话重新登录 |
| 误跑 init | `/etc/nexus/docker` 已存在 | 立即停止；不覆盖、不混用新密码与旧卷 |
| 设备失联 | 重新生成 CA/密码 | 保留原 CA、`device.json`、Broker ACL；不重新初始化 |
| 备份不可用 | 未做隔离恢复 | 升级前必须恢复验证；失败则终止 |
| 证据含密 | 截图/日志带 token | 统一脱敏；私有目录和备份不进 Git |

本计划不能证明：真实设备已连接、声学/断网/掉电稳定、生产供应商可用、24 小时无故障。ACC-01 完成后仍不能关闭 ACC-02。

## 13. 执行记录模板

执行人复制下表到变更单/台账，逐项填写；敏感值只写“已配置/已核验”，不写明文。

| 项 | 记录 |
| --- | --- |
| 变更编号/窗口 | ACC-01 / `<UTC 起止>` |
| 目标机/架构 | `<host>` / `<amd64/arm64>` |
| CORE 提交 | `<sha>` |
| 发布工作流 | `<run-id/url>` |
| CORE digest | `sha256:<...>` |
| init digest | `sha256:<...>` |
| 部署路线 | 在线 / 离线 |
| 场景 | 首次 / 既有升级 |
| 升级前迁移版本 | `<version>` |
| 备份文件/大小/sha256 | `<path/size/hash>` |
| 隔离恢复结果 | 通过/失败，关键表计数 |
| 升级后迁移版本 | `<version>`，failed=`<n>` |
| 健康/状态 | database=`<...>`，mqtt=`<...>` |
| HTTPS/SSE | `<status/headers>` |
| 会话重启 | 通过/失败 |
| 设备/消息冒烟 | 未执行/通过/失败（注明批准） |
| 偏差与处置 | `<...>` |
| 回滚是否触发 | 否/是，原因与结果 |
| 24h 观测 | 通过/失败，异常摘要 |
| 复核人/时间 | `<name/time>` |

非密证据建议保存到 `/var/log/nexus/acc01-<UTC>/`：`published-images.env`、净化 smoke 报告、容器状态、镜像 inspect 摘要、迁移 SQL 输出、备份 sha256、HTTPS/SSE 响应头、异常日志摘录。台账只写摘要和路径，不提交备份、私钥、`core.env`、`device.json`。

## 14. 配套文档

- [1Panel 快速部署](27-1panel-quickstart.md)
- [离线/本地镜像部署](../deployment/1panel/README.md)（其中 digest 为历史基线，ACC-01 执行时以 G0 产物替换）
- [部署预检与发布清单](../deployment/server-release.md)
- [CORE 1Panel 部署](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/1panel-deployment.md)
- [CORE Docker 部署、升级与恢复](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/docker-deployment.md)
- [云端互通步骤](12-cloud-interop.md)
- [CORE 实时更新与代理要求](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/console-events-and-setup.md)

## 15. ACC-02 变更记录：管理台与伙伴空间升级（2026-11-24）

范围：把 SPA 控制台更新（「伙伴」四分区、小舱与衣橱、回忆与收藏）与 CORE V29 伙伴空间迁移部署到 `pet.muyin.site`，并同时开启伙伴后端与 Broker 策略。场景判定为**既有安装升级**，项目名、卷、CA、设备配置与凭据均不变；未执行初始化镜像。

### 15.1 发布物（G0）

| 项 | 值 |
| --- | --- |
| 发布提交 | CORE `88d2e90`（含 `c0c0e06`；迁移新增 `V29__companion_space.sql`） |
| 发布运行 | [45393714296](https://github.com/liuyiwuqing/nexus-core/actions/runs/45393714296) `success`（validate 真实 Compose 部署检查 + 双架构发布） |
| CORE digest | `ghcr.io/liuyiwuqing/nexus-core@sha256:12b44f30377d745ea845d40f0d149b8780e0a0d1df032b9f281539ab582477ff` |
| init digest | `ghcr.io/liuyiwuqing/nexus-core-init@sha256:d30c5f91f49914e19334837e1ea8cff6c165f587510017ef82a9d40d094ea7c5`（本次未使用） |
| 冒烟 | 7/7 通过，报告 `published-image-smoke.json`（产物落 `/tmp/nexus-publish-45393714296`，本机临时目录，不入库） |
| 镜像内容核对 | 拉取 `linux/amd64` 后解出 `/app/core.jar`，`BOOT-INF/classes/db/migration` 含 V1–V29 共 29 个；`V29__companion_space.sql` SHA-256 `2ec44175…7cd9` 与源码一致 |

服务器到 GHCR 仍不可用（`pkg-containers.githubusercontent.com` 的 `TLS handshake timeout`，本次复现），继续走离线导入：

| 项 | 值 |
| --- | --- |
| 本机归档 | `/tmp/nexus-core-acc02-amd64.tar`，319,070,720 B，SHA-256 `baf76eb57a9d47e0197b3a0b800a56da7148131fecea0396f460240b3d51b02c` |
| 服务器归档 | `/home/nexus-deploy/images/nexus-core-acc02-amd64.tar`，尺寸与哈希一致 |
| `docker load` | 成功；镜像 ID `sha256:a68b4cbc941e5ecb31f7c9c2d63f755665ee987783af16f0c7b450631ce80e8e`，`linux/amd64`，`revision=88d2e90484a9…`，`RepoDigests=[]` |
| 本地 tag | `ghcr.io/liuyiwuqing/nexus-core:acc02-88d2e90`（编排按 tag + `pull_policy: never` 引用） |

### 15.2 备份与隔离恢复（G2）

- 备份前现状：迁移 V28、`devices` 1 行、`deliveries` 6 行、`device_telemetry_samples` 149 行；CORE 镜像 `sha256:a8f98e…`（`acc01-d2711f7`）。
- CORE 于 `2026-11-24T12:35:14Z` 停止写入；数据库 dump `~/nexus-backups/nexus-acc02-before-20261124T123517Z.dump`，108,281 B，SHA-256 `8c0dccbe114f4c9b72fe0294d25cf9d71859040d5099e901ea4f360f673a627a`。
- 私有目录包 `~/nexus-backups/nexus-private-20261124T123517Z.tar.gz`，10,330 B，SHA-256 `b5f5d8ad6da12e8220687e3fb7ba1dac28f0f8a7765d02e3afd0d2704714c80e`（内容为 `/etc/nexus/docker`，含令牌/密码/CA，仅留存本机回滚用，不入库不外传）。
- 隔离恢复：`--network none` 临时容器 + 临时卷 `nexus-restore-check-<epoch>`，`pg_restore --no-owner --exit-on-error` 退出 0；恢复后 V28 且 `devices`/`deliveries`/`device_telemetry_samples` 与备份前一致（1/6/149）；临时容器与卷已删除。

### 15.3 Broker 策略升级（先于 CORE 启动）

按 `tools/broker_policy.py` 当前标准（`GROWTH_V1_ACL` → 现行 `MANAGEMENT_ACL`）对在线 ACL 做受控升级，只增加下列规则，不改既有条目：

```text
topic read  nexus/v1/devices/+/companion_status
topic read  nexus/v1/devices/+/appearance_acks
topic write nexus/v1/devices/+/appearance
pattern write nexus/v1/devices/%u/companion_status
pattern write nexus/v1/devices/%u/appearance_acks
pattern read  nexus/v1/devices/%u/appearance
```

`mosquitto.conf` 仅 `message_size_limit` 2048 → 3072（`allow_anonymous false`、TLS、`max_packet_size 4096` 未动）。写入后 `SIGHUP` 重载，容器**未重建**（`Up 6 days` 不变），日志 `Reloading config`，容器内核对 `message_size_limit 3072`、ACL 含 4 处 `appearance` 规则。原始文件备份：`~/nexus-backups/broker-20261124/acl.20261124`（SHA-256 `8fe91211…ec43`）、`mosquitto.conf.20261124`（`0ee38f34…7387`）。

### 15.4 编排与重启

只改 CORE 两处：`image` 由 `acc01-d2711f7` 换成 `acc02-88d2e90`；`environment` 增加 `NEXUS_COMPANION_ENABLED: "true"`。PostgreSQL、Mosquitto、网络、安全限制、会话卷、令牌文件挂载均未动。

| 项 | 值 |
| --- | --- |
| 活动编排 | `/data/1panel/docker/compose/nexus-production/docker-compose.yml`（1Panel 文件驱动，未在面板另存旧内容） |
| 旧编排 SHA-256 | `ad3d73736ee32de6404a441074a463f8b5e8e2b594aceacba55aa65cca03a1c2`，备份 `/compose/docker-compose.yml.bak-acc02-20261124` 与 `~/nexus-backups/nexus-production.docker-compose.yml.acc02-backup` |
| 新编排 SHA-256 | `58c97511dc0b7f2a38f085d59af09a28b7b0bb5642d13172c15ee37f4943a029` |
| 重建方式 | 以本地 CORE 镜像作一次性 root 客户端挂宿主 `docker.sock`、`docker`、compose 插件、编排目录与 `/etc/nexus/docker`，执行 `docker compose -p nexus-production -f <宿主真实路径> up -d --no-deps --force-recreate core` |
| 停机窗口 | `12:35:14Z` 停 CORE → `12:36:18Z` 新容器启动 → `12:36:47Z` healthy，服务中断约 64 s（期间反代 502）；PostgreSQL/MQTT 全程运行 |

### 15.5 迁移与部署后检查

- Flyway：`Current version 28` → `Migrating to 29 - companion space` → `Successfully applied 1 migration`（0.081 s）；`flyway_schema_history` 最高 V29、`failed=0`；新建 `device_companion_observations`、`device_companion_boots`、`device_appearance_requests`；`devices`/`deliveries`/`device_telemetry_samples` 仍为 1/6/149（迁移不改行数）。
- `/api/v1/status`：`{"database":"ready","mqtt":"ready"}`；无令牌 401。
- 伙伴接口：`GET /api/v1/devices/dodo-1/companion` → `{"enabled":true,"online":false,"fresh":false,"snapshot":null,"requests":[]}`（开关已生效，设备离线故无快照）。
- 令牌文件模式保持：`GET /api/v1/access/admin-token` → `{"mode":"file","editable":true,"revision":2}`。
- 静态资源：`https://pet.muyin.site/` 200、`ssl_verify_result=0`，入口脚本 `assets/index-B6SjEAb6.js`，包内含「伙伴」入口与 companion 逻辑。
- SSE：`/api/v1/events` HTTP/2 200、`content-type: text/event-stream`、`cache-control: no-store`、`x-nexus-role: admin`；`curl -N` 收到 `event:ready` 与连续 `event:heartbeat`，无代理缓冲迹象。
- 端口与安全：`127.0.0.1:18088` 仅回环，无 5432 外部监听；CORE `65534:65534`、只读根、1 GiB、`cap_drop: ALL`、`RestartCount=0`；`/var/lib/nexus/sessions` 为 `65534:65534 700`。
- MQTT TLS（宿主 openssl 读私有 CA）：`TLSv1.3`、`subject=CN=pet.muyin.site`、`issuer=CN=NEXUS Local CA`、`Verify return code: 0 (ok)`；HTTPS 证书至 2027-02-13。
- 重启复核：`12:39:04Z` 主动 `docker restart`，9 s healthy，Flyway 输出 `No migration necessary`，`RestartCount=0`，状态仍双 ready。
- 资源：宿主 available 约 979 MiB，根分区 14 GB（84%）；CORE 227 MiB/1 GiB、PostgreSQL 44 MiB、Mosquitto 4.8 MiB。

### 15.6 当时遗留项（第 1 项已在 §16 修复）

1. ~~**每次启动 1 条 `MQTT input rejected: IllegalArgumentException`**~~：由设备 retained `telemetry` 触发——固件 `0.4.0-dev` 上报的 `config` 含 `sfxVolume`、`screen`，而 CORE `ManagementCodec.config()` 只认必需字段（+`muted`），判定 `unknown_management_field` 后整条拒绝。影响：新格式遥测不入库（当时 `device_telemetry_samples` 最新 `observed_at` 停在 `2026-11-23 12:56:47Z`），`online`/`last_seen` 由 `state`/LWT 维护不受影响。**已在 CORE `3fdca79` 修复并部署，见 §16。**
2. 设备 `dodo-1` 当时自 `2026-11-23T14:42:34Z` 离线，**换装端到端（appearance 下发 → 设备 ACK → applied）没有真实设备验证**；Broker ACL 只做了配置级核对，未发测试消息、未做真实发布验证。
3. 设备侧还需 `CONFIG_PET_COMPANION_SYNC_ENABLED` 才回传 `companion_status`，本次未烧录、未核对在网固件是否开启（§16 观测到仍未回传）。
4. 未执行：浏览器 UI 登录/权限页、24 小时观测、1Panel 面板内编排保存（本次直接改文件，面板读取的是同一路径）、ACC-02 硬件长稳。

回滚：迁移 V29 只新建三张表，不改变既有表结构，可仅把编排 `image` 改回 `acc02-88d2e90`→`acc01-d2711f7`、去掉 `NEXUS_COMPANION_ENABLED` 后重建 CORE；若同时要回退 Broker，用 `~/nexus-backups/broker-20261124/` 两个文件覆盖并 SIGHUP。数据库回退按 11.3，不删除 `postgres-data`。

## 16. ACC-03 变更记录：遥测契约缺口修复与重新部署（2026-11-24）

§15.6 第 1 项的修复与上线。范围仅 CORE：`ManagementCodec.config()` 接受共享 `device-config.schema.json` 里已声明、而 CORE 之前未实现的 `screen{timeoutS,idleBrightness}` 与 `sfxVolume`。不改主题、不改迁移、不动 Broker、不动设备。

### 16.1 代码与本地验证

| 项 | 内容 |
| --- | --- |
| 提交 | CORE `3fdca79`（`fix(management): accept screen and sfxVolume in device settings`） |
| 行为 | 两项可选：`sfxVolume` 0–100；`screen` 子对象必含 `timeoutS`(0–3600)/`idleBrightness`(0–100) 且禁止未知键；**设备没上报就不补默认值**，上报才原样保存，避免 CORE 替设备编造状态或用默认值覆盖设备端设置 |
| 顺带修正 | 控制台设置页不改这两项，但草稿是完整配置的深拷贝，过去一旦回传会被 CORE 丢弃→设备按默认值重写息屏/提示音；现在原样回传。前端仅补类型声明，产物哈希未变 |
| 用例 | `DeviceManagementFlowTest.telemetryKeepsFirmwareSettingsFieldsAndRejectsBadOnes`：真实 MQTT→CORE→PostgreSQL→HTTP 下验证接受字段、以及 `screen` 未知键、`sfxVolume 101`、`timeoutS 3601` 三种坏样本仍整条拒绝（先用有效样本作处理屏障，再断言快照未变） |
| red/green | 回退 `ManagementCodec` 后该用例失败（`telemetry: null`，消息被整条丢弃）；修复后通过 |
| 全量 | `python3 tools/run_local.py build`：197 项 0 失败 6 跳过，Vue 类型/token 检查与 Vite 构建通过 |

### 16.2 发布与部署

| 项 | 值 |
| --- | --- |
| 发布运行 | [45402756973](https://github.com/liuyiwuqing/nexus-core/actions/runs/45402756973) `success`，7 项镜像冒烟通过 |
| CORE digest | `sha256:45ca707ad5c900cbc552288cff28517ff743dde27136bcc7f85c97937a083233` |
| 离线归档 | 319,070,720 B，SHA-256 `f5adf38223744b8be3759fa035a0015547cdccd515d9a1ac55e9b6ac3a744d0d`，两端一致 |
| 镜像 | ID `sha256:c0831912…3313`，`linux/amd64`，`revision=3fdca79f…`，tag `acc03-3fdca79` |
| 备份 | `~/nexus-backups/nexus-acc03-before-20261124T133043Z.dump`，114,543 B，SHA-256 `3c0f8a87050567443d697b1ca002e21ca0231ac00fca749334581fbda47d201a` |
| 编排 | `58c97511…` → `4c956726…`，只改 `image` 一行；旧文件备份 `/compose/docker-compose.yml.bak-acc03-20261124`（写前先比对哈希确认面板未改写） |
| 停机 | `13:30:27Z` 停 CORE → `13:30:50Z` 新容器启动 → `13:31:04Z` healthy；PostgreSQL/MQTT 未停 |

### 16.3 部署后实测（含真实设备）

- 迁移：`No migration necessary`（仍为 V29），`RestartCount=0`，容器 `65534:65534`/只读未变。
- 告警：`MQTT input rejected` **0 次**（升级前每次启动 1 次），`ERROR` 0 次。
- retained 遥测被接受：`device_telemetry` 快照由 `2026-11-23 12:56:47Z` 前推到 `2026-11-24 13:30:57Z`，`configRevision=4`，`config.sfxVolume=20`、`config.screen={"timeoutS":60,"idleBrightness":40}`；`device_telemetry_samples` 149 → 152。
- 设备 `dodo-1` 在升级窗口内真实上线（`firmwareVersion=0.4.0-dev`，`wifiIp=192.168.5.235`）：`online=true`、`last_seen=13:31:21Z`，`/management` 返回 `fresh=true`、`configBlock=""`、`observedAt` 为实时值，30 秒级遥测持续入库——这是该契约缺口修复后第一次有真实设备的完整回读。
- 伙伴接口：`enabled=true`、`online=true`，但 `snapshot=null`、`device_companion_observations=0` → 在网固件未开 `CONFIG_PET_COMPANION_SYNC_ENABLED`。
- 站点：`https://pet.muyin.site/` 200，入口仍为 `index-B6SjEAb6.js`（仅类型变更，产物不变）。

### 16.4 边界

未向真实设备提交配置请求（会写 NVS 并改变息屏/提示音），因此配置回传链路仍只有协议级验证；换装端到端（appearance → ACK → applied）仍无真实设备证据，需先让固件开启伙伴同步；未做 24 小时观测与浏览器登录/权限页验收。

回滚：编排 `image` 改回 `acc02-88d2e90` 重建 CORE 即可，无迁移可回退（V29 未变）。（`acc01/acc02/acc03` 的本地 tag 在 2026-11-24T22:25Z 前一度都不在本机、这样回滚实际执行不了，已用归档恢复，见 §19.4）

## 17. ACC-04 变更记录：控制台可编辑屏幕与提示音（2026-11-24）

§16 修好契约后，把只做透传的三项接到控制台「设置与状态」。范围只有 CORE：前端表单、类型与一条 HTTP/MQTT 流程用例；后端校验沿用 §16 的实现，无迁移、无 Broker 改动、无设备改动。

| 项 | 内容 |
| --- | --- |
| 提交 | CORE `c88957e`（`feat(console): edit screen timeout, idle brightness and help volume`） |
| 行为 | 「更多设置」新增息屏超时（0–3600 秒，0=永不息屏）、待机亮度（0–100%）、提示音音量（0–100%）；三项只在设备上报过时出现且可编辑，未上报的设备显示“固件升级后可在此调整”，不补默认值；待机亮度高于亮度时给出“不会降亮度”提示；折叠摘要与配置记录都显示下发值 |
| 校验 | 前端 `fieldRule` 限制范围并在越界时阻止提交；后端 `config()` 仍按共享 schema 校验，HTTP 边界对 `sfxVolume 101`、`idleBrightness 101`、`timeoutS 3601`、`screen` 未知键返回 400 |
| 用例 | `DeviceManagementFlowTest.configurationRequestCarriesScreenAndSoundSettings`：真实 MQTT→CORE→PostgreSQL→HTTP，先发坏样本全部 400，再提交完整配置断言 202 且下发信封里 `screen.timeoutS=300`、`idleBrightness=50`、`sfxVolume=35` 与新音量同时存在 |
| 本地 | `npm run check`（vue-tsc + 83 个样式 token）通过；`python3 tools/run_local.py build` 198 项 0 失败 6 跳过 |
| 发布 | 运行 [45406480670](https://github.com/liuyiwuqing/nexus-core/actions/runs/45406480670) `success`，7 项冒烟通过；CORE digest `sha256:4bbac7ea9a2ebc067b20b83ad752172bd671595f0fbf55fc0b692370ec5dbc81` |
| 离线归档 | `/tmp/nexus-core-acc04-amd64.tar` 319,071,232 B，SHA-256 `bc9db419f84e65d03661e66112b0524cd7ea5ff73ca6a5f85067ec3728d1a93d`，服务器端一致；镜像 ID `sha256:70837052…9a56`，`revision=c88957e`，tag `acc04-c88957e` |
| 备份 | `nexus-acc04-before-20261124T135036Z.dump`，115,208 B，SHA-256 `5842904f0f7f4aa8febdcea3f42556aa25e40c4f16d6b5daa12961a909abdf19` |
| 编排 | `4c956726…` → `7d7381bc…`，只改 `image`；旧文件备份 `docker-compose.yml.bak-acc04-20261124` 与 `~/nexus-backups/…acc04-backup` |
| 停机 | `13:50:27Z` 停 → `13:50:43Z` 启动 → `13:50:51Z` healthy（约 24 秒），PostgreSQL/MQTT 未停 |

部署后实测：迁移 `No migration necessary`（V29），`RestartCount=0`，容器仍 `65534:65534`/只读；`MQTT input rejected` 与 `ERROR` 均为 0；设备 `dodo-1` 持续在线，快照 `observed_at=13:50:27Z`、`sfxVolume=20`、`screen={"timeoutS":60,"idleBrightness":40}`，样本 189→190，`/management` 返回 `fresh=true`、`canConfigure=true`；线上 `DevicesPanel-DgOt9p4u.js` 含「息屏超时」「待机亮度」「提示音音量」，旧构建哈希返回 404。

### 17.1 界面验证（2026-11-24 补充）

初版只做到类型检查与产物字符串，随后用浏览器补验，不新增项目依赖：`playwright-core`（约 1.5 MB，只装在 `/tmp`，不下载浏览器）+ 本机 Chrome，配一个临时 Vite harness 只挂载 `DeviceManagementPanel`、全局 `style.css` 与 `NConfigProvider` 浅色主题（漏掉全局样式会渲染成单列，第一次截图就是这样误判的），接口由 `/tmp` 的 fixture 服务提供两台设备：`dodo-screen` 上报 `screen{60,40}` 与 `sfxVolume 20`，`dodo-legacy` 不上报。

| 断言 | 结果 |
| --- | --- |
| 三项按真实值渲染 | 息屏超时 `60`、待机亮度「· 40%」、提示音音量「· 20%」，折叠摘要含「息屏 60 秒」 |
| 布局 | 1440×1000 下与原有时区/消息容量一致的两列栅格，说明文案在控件下方不重叠；390×844 无横向溢出（`scrollWidth - clientWidth = 0`） |
| 旧固件 | `dodo-legacy` 面板不渲染息屏超时字段，显示「当前设备未上报屏幕与提示音设置，固件升级后可在此调整」 |
| 编辑与提交 | 息屏超时改 300 后出现「有未提交的修改」，点击保存的载荷为 `screen:{timeoutS:300,idleBrightness:40}`、`sfxVolume:20`，音量/亮度/免打扰/消息等其余字段原样保留 |
| 运行期 | 无页面异常；修复 fixture 路由后请求全为 2xx |

截图（本机 `.screenshots/`，未入库）：`settings-desktop.png`、`settings-mobile.png`、`settings-detail.png`、`settings-legacy.png`。harness 与 fixture 均在 `/tmp`，验证后已删除仓库内临时目录。

边界：仍未向真实设备提交含新字段的配置请求（会写 NVS），设备端接受与否依据协议与固件校验逻辑，不是实机证据；浏览器验证用的是 fixture 数据而非生产接口，生产侧仅确认了接口返回真实值（`sfxVolume=20`、`screen{60,40}`）与线上产物含三项文案。对生产站点的验收见 §18。

回滚：编排 `image` 改回 `acc03-3fdca79` 重建 CORE；数据库无需回退。（`acc01/acc02/acc03` 的本地 tag 在 2026-11-24T22:25Z 前一度都不在本机、这样回滚实际执行不了，已用归档恢复，见 §19.4）

## 18. 生产控制台浏览器级验收与运行观测基线（2026-11-24）

§17.1 的浏览器验证跑在临时 harness 与 fixture 上，§16.4 与 §17 都把「浏览器登录/权限页验收」与「24 小时观测」列为未做项。本节补的是**真实站点**：`https://pet.muyin.site/`、真实管理员令牌、真实 PostgreSQL/MQTT 与在网的 `dodo-1`。方法仍是不加项目依赖的 `playwright-core`（装在 `/tmp`）+ 本机 Chrome（headless），1440×900 与 390×844 两套视口。

除登录本身，全部操作都是读：没有点「保存到设备」，没有改动设备配置、Broker 策略或数据库。

### 18.1 验收断言（16/16 通过）

| 断言 | 结果 |
| --- | --- |
| 登录页渲染 | h1「连接你的桌面伙伴」，与管理台一致 |
| 错误令牌被拒 | `POST /api/v1/session` 401，停留登录页，提示「管理令牌无效，请重新登录。」 |
| 正确令牌登录 | `POST /api/v1/session` 200，响应头 `X-Nexus-Role=admin` |
| 管理员导航齐全 | 概览/设备/权限/工具 均可见（`tools`、`access` 带 `admin: true`） |
| 设备页读到真实设备 | 「1 台设备 · 1 台在线」，列表含 `dodo-1`、固件 `0.4.0-dev`、Wi-Fi `502 · 192.168.5.235 · -44 dBm` |
| 折叠摘要含设备值 | 「Asia/Shanghai · 8 条 · 20 秒上限 · 息屏 60 秒」 |
| 息屏超时渲染 | 「息屏超时（秒）」，`#devicemanagementpanel-field-8` 值 `60` |
| 待机亮度渲染 | 「待机亮度 · 40%」，滑杆 `aria-valuenow=40` |
| 提示音音量渲染 | 「提示音音量 · 20%」，滑杆 `aria-valuenow=20` |
| 其余滑杆未串位 | 滑杆序列 `[20(音量), 80(亮度), 40(待机亮度), 20(提示音)]`，与快照 `volume=20`、`brightness=80` 一致 |
| 保存按钮存在且未点击 | 仅做存在性断言，未向设备下发任何配置 |
| 权限页渲染 | 「权限与审计 / 刷新权限与审计 / 修改管理员令牌 / 签发角色令牌 / 暂无角色令牌」 |
| 移动端登录页 | 390×844 下 `scrollWidth - clientWidth = 0` |
| 移动端设置页 | 经抽屉导航进入设备 → 设置与状态 → 更多设置，同样 0 溢出 |
| 无意外 4xx/5xx | 唯一 4xx 是登录前的 `/api/v1/session=401`（预期），无 5xx |

截图（本机 `/tmp/nexus-ui-prod/shots/`）：`prod-login.png`、`prod-login-rejected.png`、`prod-overview.png`、`prod-devices.png`、`prod-settings-expanded.png`、`prod-access.png`、`prod-login-mobile.png`、`prod-settings-mobile.png`。

两处只有真站点才会暴露的细节：一是三项在「更多设置」折叠区内，验证必须先展开，否则只看到摘要那行「息屏 60 秒」；二是移动端导航是 `NDrawer` 抽屉，`.nav-label` 在侧栏与抽屉里各有一份，直接按文本点会命中被 CSS 隐藏的侧栏副本，必须先点 `aria-label="打开导航"` 再点抽屉内的节点。

### 18.2 运行观测基线

| 项 | 值（2026-11-24 22:0x CST） |
| --- | --- |
| 容器 | `nexus-production-core-1` = `acc04-c88957e`，`StartedAt=13:50:42Z`，`healthy`，`RestartCount=0` |
| 日志 | 启动后 30 行，`ERROR` 0、`WARN` 0、`MQTT input rejected` 0 |
| 迁移 | Flyway `Successfully validated 29 migrations`，`Schema "public" is up to date` |
| 设备 | `dodo-1` `online=true`，`last_seen` 每 30 秒推进（观测时年龄 < 10 秒） |
| 遥测 | 样本 208 条（13:59:27Z）→ 219 条（14:04:57Z），相邻样本间隔恒为 30 秒 |
| 快照 | `config.screen={"timeoutS":60,"idleBrightness":40}`、`sfxVolume=20`、`volume=20`、`brightness=80`、`config_revision=4` |
| 伙伴/换装 | `device_companion_observations=0`、`device_appearance_requests=0`（在网固件未开伙伴同步） |
| 数据库 | `nexus` 9,702 kB，`device_telemetry_samples` 96 kB；PostgreSQL 已连续运行 522,173 秒（约 6 天） |
| 磁盘 | 根分区 79 GB 用 67 GB（85%，剩 13 GB） |
| 无令牌访问 | `/` 200（0.13s）；`/actuator/health`、`/api/v1/devices`、`/api/v1/devices/dodo-1/telemetry` 均 401 |

采样间隔恒为 30 秒、快照连续携带 `screen`/`sfxVolume` 说明 §16 的契约修复在真实链路上持续生效，不是一次性放行。

### 18.3 未完成与边界

- **验收自身的副作用**：五次登录尝试在 `access_audit` 留下 13 行 `POST /api/v1/session`（9 行 200、4 行 401），除此之外没有任何写入；没有建角色令牌、没有签发凭据、没有下发配置。本机存过的生产令牌副本用完即删。
- **24 小时观测仍未完成**：本节只有约 15 分钟基线（容器 8 分钟、样本 219 条）。`ERROR`/`WARN` 为 0 只代表这段时间；正式窗口已在 §19 启动（`2026-11-24T14:14Z` 起，96 条/15 分钟），到期才能判。
- **角色页只验了管理员**：生产只有管理员令牌（`tokens.json` `format=1`、`revision=2`，权限页显示「暂无角色令牌」）。`operator`/`viewer` 的界面可见性与写入拦截仍只由 `RoleAccessFlowTest` 等自动化覆盖，没有浏览器证据；要补得先在生产签发一个只读角色令牌，属于新增凭据，需用户确认。
- **设备侧仍未写**：没有向 `dodo-1` 下发含新字段的配置请求（会写 NVS）；换装端到端仍需固件开启 `CONFIG_PET_COMPANION_SYNC_ENABLED`。
- **磁盘 85%**：根分区剩 13 GB，本轮不处理，但再叠几次离线镜像归档（单个约 300 MB）前应先清理 `/tmp/nexus-core-acc0*.tar`。

回滚：本轮没有改动任何生产状态，无需回滚。

## 19. ACC-04 二十四小时观测（进行中，2026-11-24T14:14Z 起）

§18 只给了 15 分钟基线，24 小时口径按 §10.3 另起一轮。本轮窗口：

| 项 | 值 |
| --- | --- |
| 起点 | `2026-11-24T14:14:06Z`（22:14 CST），终点 `2026-11-25T14:14Z`，共 96 条（每 15 分钟） |
| 载体 | 服务器 `nexus-deploy@pet.muyin.site` 的 `~/nexus-observe.sh`，日志 `~/nexus-observe/observe-acc04-20261124T1414Z.ndjson`（目录 700） |
| 覆盖对象 | 当前运行物 `acc04-c88957e`（`StartedAt=2026-11-24T13:50:42Z`） |
| 只读性 | 只做 `docker inspect/stats/logs/cp`、`psql` 计数、`curl /api/v1/status`、`df`、`openssl s_client`；不写库、不重启、不改配置 |
| 字段 | `core/pg/mqtt` 状态与 `RestartCount`、`status`（MQTT 与数据库 ready）、`disk_avail_kb`/`disk_used_pct`、`pg_conn`、`core_mem`、`mqtt_reconnect_16m`、`http5xx_last200`、HTTPS/MQTT 证书到期 |

首条样本（`14:14:06Z`）：`core=running/healthy/0`、`pg=running/healthy/0`、`mqtt=running/0`、`status={"schemaVersion":1,"mqtt":"ready","database":"ready"}`、`disk_avail_kb=13,958,456`（84%）、`pg_conn=12`、`core_mem=250MiB/1GiB`、`mqtt_reconnect_16m=0`、`http5xx_last200=0`、HTTPS 证书到 `2027-02-13`、MQTT 证书到 `2029-02-17`。

### 19.1 采集脚本的两处修正

第一次启动就发现问题：首条样本 `status` 是 `{"error":"unauthorized"}`。原因是脚本用 `docker cp ... - | awk '/"token"/ {...}'` 从 **tar 流**里抓令牌，依赖 `tokens.json` 的排版——ACC-01 那轮能取到是排版运气，镜像重建后排版变了就取到空串。改成 `docker cp` 落到 `mktemp` 临时文件（`chmod 600`）、用 `python3 -c` 按结构取 `admin.current.token`、读完即删；环境变量 `NEXUS_ADMIN_TOKEN` 只作回退。另外把 `DURATION_S`（默认 86400）与 `INTERVAL_S`（默认 900）提成参数，便于先跑短窗口自检。

自检：`DURATION_S=30 INTERVAL_S=10` 跑出 3 条样本 + `stop`，`status` 恢复为 `{"schemaVersion":1,"mqtt":"ready","database":"ready"}`，随后删掉这份探针日志再正式启动。

脚本本身也已收进仓库：`deployment/observe/service_watch.sh` 与 `device_watch.sh`，与目标机在跑版本逐字节相同（见该目录 README 的哈希与约束）。在此之前它们只存在于服务器上，没有任何受控副本。

### 19.2 设备侧同期数据与一个不能糊过去的结论

服务器上另有一个 5 分钟周期的设备观测进程（`~/nexus-device-observe.sh`，写 `~/nexus-observe/device-dodo-1.ndjson`，只读查 `devices` 与 `device_telemetry_samples`）。从 `2026-11-23T14:32:48Z` 起的 284 条样本：间隔无断档（>5.5 分钟的空洞 0 个）、`query_failed` 0 次。

但这段跨度的结论是**负面**的：`dodo-1` 在 `2026-11-23T14:47:48Z` 进入离线，直到 `2026-11-24T13:08:16Z` 才回来，中间连续 22 小时 20 分钟 `fresh_online=false`；在线部分的 RSSI 为 `-46…-35 dBm`（中位 `-41`，注意离线期该值是最近一次上报的旧值）、`disconnect_events_16m` 全为 0。

所以这 23.59 小时**不能**当作「新版本 24 小时设备稳定性」——设备实际只在窗口最后约 1 小时在线。设备侧的 24 小时证据要等设备继续在网、并让观测进程积累一整段在线记录后才成立；这也是 §18.3 里「换装端到端无真实证据」的同一条根因：固件那侧没有持续在线的前提。

### 19.3 边界

- 窗口未结束，**不判定通过**；到期后要按日志逐条核对 96 条、确认 5xx/重连/重启计数与证书到期，再写结论。
- 脚本只覆盖服务端视角（容器、接口、磁盘、证书、5xx），不含设备实时性、功耗、音频等固件侧指标。
- 设备观测进程没有结束条件，是上一轮遗留的无限循环；窗口到期时应手动停掉，避免长期占用。
- 观测进程与本期部署无耦合：CORE 若在窗口内再次重建，`RestartCount` 归零会掩盖重启次数，届时应以 `StartedAt` 变化判定。
- 磁盘盘点（只读）：`docker system df` 显示镜像 15 个共 7.61 GB（可回收 583.9 MB）、容器 252.3 MB、卷 67.2 MB、构建缓存 0；`~/images` 1.2 GB 是 ACC-01~04 的离线导入 tar，`~/staging` 92 KB、`~/nexus-backups` 600 KB，journal 11.9 MB。剩余 13 GB 的大头不在这些目录里（同期还跑着 halo×4、postgres 18、rustfs 等与本项目无关的容器），清理只能拿回约 1.8 GB。

### 19.4 归档清理与回滚点修复（2026-11-24 22:25Z，用户授权）

用户要求清掉 `~/images` 的 1.2 GB tar。执行前先核对「这些 tar 对应的镜像还在不在本机」，发现的问题比空间本身严重：

| 归档 | 声明的 tag | 清理前本机是否有该镜像 |
| --- | --- | --- |
| `nexus-core-acc01-amd64.tar` | 无（保存时未打 tag） | 否 |
| `nexus-core-acc02-amd64.tar` | `acc02-88d2e90` | **否** |
| `nexus-core-acc03-amd64.tar` | `acc03-3fdca79` | **否** |
| `nexus-core-acc04-amd64.tar` | `acc04-c88957e` | 是（唯一在跑的） |

也就是说 §16/§17 写的「回滚＝改编排 `image` 回 `acc03-3fdca79` 重建」**当时根本执行不了**：编排是 `pull_policy: never`，本地没有那个 tag，容器会直接起不来；服务器拉 GHCR 又有 TLS 超时的前科。这三份 tar 是那几个版本唯一的离线副本——直接删就等于把回滚点一起删了。

处理方式（按用户选择的「先 load 再删」）：

1. `docker load` 三份归档。acc02/acc03 自带 tag；acc01 的 manifest `RepoTags` 为 `null`，按 revision 标签补成 `acc01-d2711f7`。
2. 用 `org.opencontainers.image.revision` 逐个核对：`d2711f7…`、`88d2e90…`、`3fdca79…`、`c88957e…`，与文档记录一致。
3. 占用只涨了 **80 MB**（7.607 → 7.687 GB）：四个版本共享基础层，差异只在应用层。
4. 删除三份 tar（acc04 的 `bc9db419…` 在镜像已加载、容器 healthy 且 `RestartCount=0` 的前提下先删）。

结果：

| 项 | 清理前（21:59 基线） | 清理后 |
| --- | --- | --- |
| `~/images` | 1.2 GB（4 份 tar） | 4 KB（空目录） |
| 根分区 | 67 GB 已用 / 13 GB 可用（85%） | 65 GB 已用 / 15 GB 可用（82%） |
| 可切回的版本 | 只有 `acc04-c88957e` | `acc01-d2711f7`、`acc02-88d2e90`、`acc03-3fdca79`、`acc04-c88957e`（**6 分钟后被外部 prune 清掉，见 §19.5**） |
| 回滚方式 | 需要 `docker load` 且当前无 tar 可 load | 改 `image` tag 后重建即可 |

分两步观察：先删已加载的 acc04 归档后为 66 GB / 14 GB（83%），再 load 三版镜像（+80 MB）并删余下三份 tar 后到 65 GB / 15 GB（82%）。净增加约 2 GB 可用空间，其中约 1.1 GB 来自 tar、其余同期其他容器与日志的释放。

运行态未受影响：`nexus-production-core-1` 仍是 `acc04-c88957e`、`running/healthy`、`RestartCount=0`；本轮没有重建容器。

边界：镜像只有 tag、没有 digest 校验记录（tar 的 sha256 已在 §16/§17 留档，删的是文件不是证据）；`acc01` 的 tag 是我按 revision 补的，不是原发布流程给的；回滚到 acc01/acc02 还要配套数据库备份（V28/V29 差异见 §15.2），单独换镜像回退不等于状态一致。

### 19.5 外部 prune 事件与回滚点加固（2026-11-24T22:31Z 发现）

§19.4 写完后 6 分钟，三个刚恢复的镜像就没了。`docker events` 记到两轮 `prune`：

```
2026-11-24T22:31:08Z  prune
2026-11-24T22:31:16Z  untag/delete  sha256:c0831912…  (acc03)
                      untag/delete  sha256:a8f98e16…  (acc01)
                      untag/delete  sha256:a68b4cbc…  (acc02)
                      untag/delete  sha256:3ea06e4c…  (alpine:3，我读 /etc/nexus 用的临时镜像)
2026-11-24T22:31:16Z  prune
```

也就是说 §19.4 里「可切回 4 个版本」这句在写下后几分钟就不成立了。触发源没有归因：宿主机上跑着 `1panel-core` / `1panel-agent`（非 systemd `1panel` 单元），`/opt/1panel` 用 root 容器挂载读不到内容，`1panel-mcp` 日志无记录；两次 `prune` 相隔 8 秒，像是面板「清理镜像」按钮或计划任务走的两步（镜像 + 构建缓存/悬空对象）。**需要操作者确认 22:31 是不是自己在 1Panel 里点了清理**——如果那是定时任务，任何「load 上去的历史镜像」都会被周期性扫掉。

**恢复**：开发机仍保有全部四个版本的镜像（`acc01` 在本机是 untagged 的 `a8f98e16`），用流式管道直接灌回去，无需落盘：

```sh
docker save a8f98e163337 | ssh nexus-prod 'docker load'   # acc01 落为 untagged，需补 tag
docker save ghcr.io/liuyiwuqing/nexus-core:acc02-88d2e90 | ssh nexus-prod 'docker load'
docker save ghcr.io/liuyiwuqing/nexus-core:acc03-3fdca79 | ssh nexus-prod 'docker load'
```

实测每个约 **51 秒**（319 MB，上行约 6.5 MB/s），三个共约 2.6 分钟；恢复后三个镜像只占 **80 MB**（共享基础层），根分区仍是 65 GB/15 GB（82%）。

**加固**：给三个历史版本各建一个**停止态容器**把镜像钉住，`docker image prune`（含 `-a`）会跳过被任何容器引用的镜像：

```sh
docker create --name rollback-acc03-3fdca79 --entrypoint /bin/true ghcr.io/liuyiwuqing/nexus-core:acc03-3fdca79
```

现为 `rollback-acc01-d2711f7`、`rollback-acc02-88d2e90`、`rollback-acc03-3fdca79` 三个 `Created` 状态容器（不运行、不占端口、不挂卷）。它们只防镜像 prune；**`docker container prune` 会先删掉它们**，那时镜像又变成可回收。

**回滚矩阵**（`grep` 镜像内 `core.jar` 的迁移清单实测，`docker run --rm --network none` 取得）：

| tag | revision | 最高迁移 | 数据库要求 |
| --- | --- | --- | --- |
| `acc01-d2711f7` | `d2711f7` | `V28__delivery_display.sql` | 需 V28 库，回退必须配 §15.2 之前的备份 |
| `acc02-88d2e90` | `88d2e90` | `V29__companion_space.sql` | V29 |
| `acc03-3fdca79` | `3fdca79` | `V29__companion_space.sql` | V29 |
| `acc04-c88957e` | `c88957e` | `V29__companion_space.sql` | V29 |

结论：acc02/03/04 三者之间可以只改 `image` 切换（同为 V29，无迁移差异），回退到 acc01 则必须同时回到 V28 的库。这比 §19.4 里那句「改 tag 即可」精确——**同 schema 才叫只改 tag**。
