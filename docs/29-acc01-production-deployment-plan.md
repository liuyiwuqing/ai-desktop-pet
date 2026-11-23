# ACC-01 生产部署与数据库迁移执行计划

2026-11-20 制定，2026-11-23 执行 G0。基线：CORE `d2711f7`（迁移 V1–V28）、04 `042ed1f`。**本文件主体是执行计划，不是部署记录**：G0 已发布并核对镜像（见 3.4）；仍未连接目标服务器，没有生产库、真实凭据或设备，也没有执行任何部署、迁移、消息发送或烧录。计划中的命令由部署者在目标机执行，结果回填到第 13 节。

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
| 最近已发布镜像 | CORE `acc95fb`，发布运行 [44270541149](https://github.com/liuyiwuqing/nexus-core/actions/runs/44270541149) `success`，包含 V1–V26 |
| 已发布 CORE digest | `ghcr.io/liuyiwuqing/nexus-core@sha256:e52c214aabe6edb603930a29a89be53885b78299a27ecc26abbf51fd007760d8` |
| 已发布 init digest | `ghcr.io/liuyiwuqing/nexus-core-init@sha256:7cfaf917ad0a0794ac3d2f3934c5d978e205b4d3d8c722f56f32ef772d20280e` |
| 域名/设备 | `pet.muyin.site`、`mqtt.muyin.site`、设备 `dodo-1`，均为用户已提供值，执行前再确认 |
| 目标机 | Debian/1Panel，尚未连接；是否存在旧安装未知，G1 判定 |
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

只导入 CORE 和 init 不够；启动时仍会尝试拉取 PostgreSQL/Mosquitto，离线环境会失败。

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
- `mqtt.muyin.site` 缺 A 记录；证书 SAN 已覆盖，需决定新增 A 记录或把设备 Broker 改为 `pet.muyin.site:8883`。
- 迁移量很小（V24 涉及 146 行，V27/V28 各 6 行），但仍按第 6 节备份并在维护窗口停 CORE 写入。
- 目标机可用内存约 860 MB，重启 CORE 前需释放内存或扩容，避免 1 GiB 限额与同机服务争抢。

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
  --mqtt-host mqtt.muyin.site \
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

在 1Panel 编排编辑器中把 CORE 的 `image` 替换为 G0 冻结的 digest；活动编排就是唯一副本，不再维护 `/etc/nexus/docker/compose.1panel.yml` 第二份。若旧编排没有登录会话卷，按 [CORE 1Panel 部署说明](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/1panel-deployment.md) 补齐：

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
| MQTT TLS | `openssl s_client -connect 127.0.0.1:8883 -servername mqtt.muyin.site -verify_hostname mqtt.muyin.site -CAfile /etc/nexus/docker/mosquitto/certs/ca.crt -verify_return_error </dev/null` | `Verify return code: 0 (ok)` |
| SSE | `timeout 5 curl -sSi -H "Authorization: Bearer $TOKEN" https://pet.muyin.site/api/v1/events \| sed -n '1,12p'` | `200`、`text/event-stream`、`X-Accel-Buffering: no`；超时退出 124 属正常 |
| 登录 | 浏览器打开 HTTPS，管理员令牌登录 | 进入管理台；不把令牌写进 URL/本地存储 |
| 权限 | “权限”页签发 operator/viewer（按需） | 新令牌可用，旧令牌按策略失效 |
| 重启持久性 | 浏览器登录后重启 CORE，再刷新 | 8 小时内会话仍在；否则检查会话卷 |
| 设备 TLS | 经批准后把 `device.json` 交付测试设备并连接 | 设备上线；不把 CA 私钥/密码贴到聊天 |
| 消息冒烟 | 仅经用户批准，用测试设备发一条可回滚消息 | 投递、接收、回执可见；未批准则跳过，不能记为通过 |
| 日志 | `docker logs --tail 200 ... core` | 无持续异常、无迁移错误、无认证风暴 |

管理员令牌读取必须在受控终端完成，例如 `read -rsp 'Admin token: ' TOKEN; echo`，用完 `unset TOKEN`；不要把 `core.env` 输出到屏幕、日志或命令历史。

### 10.3 24 小时观测

- 每 15 分钟检查一次容器健康与 `/api/v1/status`；至少覆盖一个业务低峰。
- 观察磁盘增长、PostgreSQL 连接数、CORE 内存、MQTT 重连、5xx 与证书到期时间。
- 记录一次异机备份完成与校验结果。
- 不启用真实清理、OTA、AI、Halo、Bridge、成长同步；这些默认关闭，ACC-01 不改变默认值。
- 实机在线时长、声学、断网掉电与传感器稳定性属于 ACC-02，不以 24 小时管理台观测替代。

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
