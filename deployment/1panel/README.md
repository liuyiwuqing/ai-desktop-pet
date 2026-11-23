# 1Panel：本地镜像部署

当前发布物为 CORE `d2711f7`（含 V27/V28），运行 [45103288550](https://github.com/liuyiwuqing/nexus-core/actions/runs/45103288550) 已发布并核验，CORE digest `sha256:bcf7f6191d61e2f1cd93592ff4e4f9c44eaa42e07688d7cb243254800049a037`、init digest `sha256:70d178fb6236b68939438a5f22b151deb374bc72968c78efa2793216237c8055`。本文件与编排仍以 `e76732b` 本地路线为已验证基线；采用时应按 [ACC-01 执行计划](../../docs/29-acc01-production-deployment-plan.md) G0 产物替换镜像标签/digest，并在本机重新核对架构与启动。

[docker-compose.yml](docker-compose.yml) 适用于当前 Debian x86_64 服务器，CORE 固定使用 `e76732b3792313ea5f3744b09970a7972fc89a36` 发布的多架构 digest `sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba`，要求本机已有 `linux/amd64` 镜像；`NEXUS_AUTH_SESSION_DIRECTORY` 把登录会话写入 `console-sessions` 卷。CORE 的 `pull_policy: never` 禁止自动访问 GHCR，镜像缺失会直接报错；PostgreSQL 17 和 Mosquitto 2.0.22 沿用已有编排的 digest，仅在本机缺失时拉取。这是 CORE 本地镜像方案，首次部署仍可能下载两个基础服务镜像。

配置复用 `/etc/nexus/docker`，管理台仅绑定 `127.0.0.1:18088`，MQTT/TLS 发布 TCP 8883；数据库使用内部网络和持久卷。HTTPS 由已有 1Panel/OpenResty 提供，不占用额外的 80/443。文件不包含密码，不需要源码、构建步骤或另行填写面板环境变量。

## 1. 准备 CORE 镜像

在服务器执行，确认镜像存在且为 `linux/amd64`：

```sh
docker image inspect \
  --format '{{.Os}}/{{.Architecture}} {{.Id}}' \
  ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba
```

[发布运行 44094002890](https://github.com/liuyiwuqing/nexus-core/actions/runs/44094002890) 为 success，多架构索引 digest 即上述值；2026-11-17 本机回拉的 amd64 镜像 ID 为 `sha256:aa3a2b56291ddf7b86bf38bdc63fc431e2433d5348012bb9c5c5dacec928c238`，与索引 digest 不是同一值。若尚未下载，可用前述 `docker pull`，或在网络较好的 Mac 上拉取 `--platform linux/amd64`，使用 `docker save` 导出该镜像后上传，在服务器 `docker load -i <镜像包路径>`。导入后再次核对上述 amd64 镜像 ID；digest 引用本身不可变，仍建议核对。

不要对这份编排执行全量 `docker compose pull`，也不要在 1Panel 勾选强制拉取；启动命令的拉取策略参数可能覆盖文件中的策略。更新 CORE 时先准备并核对新镜像，再明确修改其完整标签。

## 2. 首次生成私有配置

仅在 `/etc/nexus/docker` 尚不存在时执行。若已有完整配置，直接进入下一步。不要预先创建这个目录，也不要通过删除旧目录重新生成数据库密码或 CA；残留/不完整目录应先保留排查。

以下使用服务器已下载的初始化镜像，在 root 终端执行；更换域名时同时替换两个参数：

```sh
(
set -e
install -d -m 700 /etc/nexus
docker run --rm --pull=never --read-only --cap-drop ALL \
  --security-opt no-new-privileges:true --tmpfs /tmp \
  --mount type=bind,source=/etc/nexus,target=/etc/nexus \
  ghcr.io/liuyiwuqing/nexus-core-init:sha-e76732b3792313ea5f3744b09970a7972fc89a36 \
  --data-dir /etc/nexus/docker \
  --domain pet.muyin.site \
  --mqtt-host mqtt.muyin.site \
  --device dodo-1 \
  --image ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba
)
```

初始化会生成 `core.env`、`postgres.env`、MQTT 配置/证书和 `device.json`，拒绝覆盖已有目录。它同时生成的 `compose.1panel.yml` 属于原在线方案；本次部署使用本目录的新 `docker-compose.yml`，其中明确设置了本地镜像策略。

## 3. 在 1Panel 创建编排

在「容器 → 编排 → 创建编排」中，名称填写 `nexus-production`，选择编辑方式，粘贴本目录 `docker-compose.yml` 的全部内容。关闭强制拉取，部署后等待 PostgreSQL、CORE 健康且 MQTT 正常运行。

文件里的绝对路径均指向服务器 `/etc/nexus/docker`。缺少 env 文件时先完成初始化，不要创建空文件；缺少镜像时先完成导入。CORE 保留非 root、只读文件系统、健康检查、1 GiB 内存上限及有限日志。

如选择终端管理，将本文件上传为 `/root/docker-compose.yml`，执行以下命令即可；与面板方式二选一，避免创建另一套项目/数据卷：

```sh
docker compose -p nexus-production -f /root/docker-compose.yml config -q
docker compose -p nexus-production -f /root/docker-compose.yml up -d --wait --wait-timeout 180
docker compose -p nexus-production -f /root/docker-compose.yml ps
```

如已有运行中的安装，在原编排中更新并保留原项目名、数据卷和配置；先备份数据库和私有目录。上面的新建步骤仅适用于首次安装，不能借改项目名迁移数据库。不要删除 `postgres-data` 卷。

## 4. 配置代理与登录

将 `pet.muyin.site` 解析到服务器，在 1Panel 创建 HTTPS 反向代理，启用有效证书：

| OpenResty 网络 | 代理地址 |
| --- | --- |
| host | `http://127.0.0.1:18088` |
| bridge | 将 OpenResty 加入实际的 CORE edge 网络，再使用 `http://core:8088` |

edge 网络在项目名为 `nexus-production` 时通常叫 `nexus-production_edge`，以面板实际名称为准；bridge 容器内的 `127.0.0.1` 指向 OpenResty 自身。为 `/api/v1/events` 保留流式响应，关闭代理缓冲/缓存，读取超时至少 30 秒。

生产已切换为受保护文件模式：管理员令牌在 `/etc/nexus/docker/core-auth/tokens.json` 的 `admin.current.token`，`core.env` 的 `NEXUS_ADMIN_TOKEN` 不再生效。用安全方式读取当前令牌登录管理台，确认数据库和 MQTT 均为 `ready`。未带令牌访问 `/api/v1/status` 应返回 401。开放云安全组及主机防火墙的 80/443；需要接入设备时再开放 TCP 8883，并将 `mqtt.muyin.site` 直连解析到服务器。

`device.json` 提供设备的 Broker 地址、专属凭据和 CA；登记设备不等于设备已联网。AI/Halo/记忆/Bridge/清理/OTA/成长同步沿用现有默认开关，不会因部署本文件自动开启。

## 从 `230c809` 升级到 `e76732b`

[docker-compose.nexus-production.yml](docker-compose.nexus-production.yml) 是线上实例（`pet.muyin.site`）编排的升级目标，结构与初始化镜像生成的 `compose.1panel.yml` 一致，差异只有五处：`core.image` 换成 `e76732b` 的完整提交标签并加 `pull_policy: never`（本地导入路线，不访问 GHCR）；`core.environment` 增加 `NEXUS_AUTH_SESSION_DIRECTORY: /var/lib/nexus/sessions`；`core.volumes` 增加 `console-sessions:/var/lib/nexus/sessions`；顶层 `volumes` 增加 `console-sessions:`。其余服务、端口插值、只读根、`cap_drop`、健康检查与绝对路径不变。

会话卷不是可选项。新镜像把浏览器会话写在私有目录，`read_only: true` 下该目录位于只读根文件系统，缺少可写挂载时创建会话抛 IO 错误并映射为 `503 session_store_unavailable`——表现是管理台登录直接失败，而不是仅仅“重启后掉登录态”。`/var/lib/nexus/sessions` 在镜像内已为 UID 65534 且 0700，挂卷后权限与判等条件（必须恰为 0700）保持不变。

`e76732b` 相对 `230c809` 另有三个 Flyway 迁移 `V21`–`V23`（设备遥测历史表，以及网络/WiFi 质量列），只有 `CREATE TABLE` 与 `ADD COLUMN`，CORE 启动时自动执行。升级前仍必须备份数据库和私有目录；升级后不能直接把镜像回滚成 `230c809`，旧镜像的迁移集合里没有 V21–V23，Flyway 校验会失败并拒绝启动，需要还原备份或显式关闭该校验。

## 本地导入镜像（GHCR 直连慢时）

CORE 镜像只有 `linux/amd64` 一份需要（服务器是 Debian x86_64），在任意能正常访问 GHCR 的机器上取一次，导出后拷进服务器即可；不要用 `docker compose pull` 在服务器上重试。

导出方（Apple Silicon 必须显式指定平台，否则拿到的是 arm64）：

```sh
docker pull --platform linux/amd64 ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba
docker image inspect --format '{{.Os}}/{{.Architecture}} {{.Id}}' \
  ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba
docker tag ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba \
  ghcr.io/liuyiwuqing/nexus-core:sha-e76732b3792313ea5f3744b09970a7972fc89a36
docker save ghcr.io/liuyiwuqing/nexus-core:sha-e76732b3792313ea5f3744b09970a7972fc89a36 \
  | gzip -6 > nexus-core-sha-e76732b-amd64.tar.gz
shasum -a 256 nexus-core-sha-e76732b-amd64.tar.gz
```

必须打上完整提交标签再 `docker save`：直接按 `@sha256:` 引用保存，`docker load` 之后镜像没有可用的 `RepoTags`，编排里的 `image:` 匹配不到。预期镜像 ID 为 `sha256:aa3a2b56291ddf7b86bf38bdc63fc431e2433d5348012bb9c5c5dacec928c238`，与发布回拉验证报告一致；包内 `manifest.json` 的 `Config` 指向 `blobs/sha256/aa3a2b56…`，所以导入后的 ID 可直接核对。

服务器 root 终端（先备份，再导入与校验，最后只重建 CORE）：

```sh
cd /etc/nexus/docker
cp compose.1panel.yml compose.1panel.yml.bak-$(date +%Y%m%d)
docker ps --format '{{.Names}}' | grep postgres        # 确认实际容器名，下面按它替换
docker exec <postgres 容器名> pg_dump -U nexus -d nexus -Fc > /root/nexus-$(date +%F).dump
gzip -dc nexus-core-sha-e76732b-amd64.tar.gz | docker load
docker image inspect --format '{{.Os}}/{{.Architecture}} {{.Id}}' \
  ghcr.io/liuyiwuqing/nexus-core:sha-e76732b3792313ea5f3744b09970a7972fc89a36
docker compose -p nexus-production -f compose.1panel.yml config -q
docker compose -p nexus-production -f compose.1panel.yml up -d
docker compose -p nexus-production -f compose.1panel.yml ps
```

`docker load` 必须打印 `Loaded image: ghcr.io/liuyiwuqing/nexus-core:sha-e76732b…`，`inspect` 必须是 `linux/amd64` 且 ID 与上面一致；两者不符就不要继续，编排里的 `pull_policy: never` 会让缺失或 arch 不符时直接报错，而不是偷偷联网拉取。`docker save` 从 25.0 起输出的是 OCI 布局归档（`oci-layout` + `manifest.json` + `blobs/sha256/`），`docker load` 在 25.0 以上可直接读取；服务器为 27.3.1，满足要求，低于 25 的旧引擎需要先升级。

需要改回在线拉取时，把 `core.image` 换成 `ghcr.io/liuyiwuqing/nexus-core@sha256:0310e733…` 并删掉 `pull_policy: never` 即可。

已有 `230c809` 安装时，用 `docker-compose.nexus-production.yml` 替换 `compose.1panel.yml` 或在 1Panel 编排里粘贴后再执行。保留原项目名、`postgres-data` 卷、`core.env` 与 `/etc/nexus/docker` 目录，不重新运行初始化镜像。新卷首次创建为空，升级后需要重新登录一次；之后在同一卷、有效期内重启可恢复。启动后用 `docker compose logs core` 确认 V21–V23 迁移与健康检查，再核对 `/api/v1/status` 的 database 与 mqtt 均为 ready。

包已改为 Public，需要在线拉取时也不需要 `docker login`；若后续改回私有，才需要在服务器与 1Panel 配置只读凭据。

## 可选：管理员令牌改为受保护文件（`NEXUS_AUTH_TOKEN_FILE`）

先说清适用范围。管理台“权限”页签发的**独立角色令牌**（operator/viewer，给脚本或他人用）存在数据库里，与这个变量无关，登录后直接签发即可，不需要改环境或重启。`NEXUS_AUTH_TOKEN_FILE` 只解决两件事：管理员令牌的 current/previous 平滑轮换，以及让“权限 → 修改管理员令牌”在网页可改。默认 env 模式下这两项都不可以用。

启用步骤：

1. 在能运行 Python 3 的受控环境生成 store（本次生产宿主为 Python 3.11.2；初始化镜像只打包了 `init_docker.py`/`init_local.py`/`broker_policy.py`，不含该工具）。`init` 会**保留现有管理员令牌原值**，只把它挪进文件，`adminActor` 取其 SHA-256，所以既有审计归属和登录令牌都不变。命令只从环境读秘密，不接受 `--token` 参数，也不打印秘密：

```sh
NEXUS_ADMIN_TOKEN='<服务器 core.env 里那个值>' \
  python3 tools/credential_store.py init --file ./auth/tokens.json
python3 tools/credential_store.py status --file ./auth/tokens.json   # 只显示 revision 与 id，不显示令牌
```

2. 拷到服务器并交给容器用户（CORE 以 `65534:65534` 运行）。`init` 会在同目录留下 `tokens.json` 和 `tokens.json.lock`，**两个都要拷**：Java 侧用 `FileChannel.open(..., WRITE)` 打开锁文件、不带 `CREATE`，缺锁文件时网页修改会直接失败。

```sh
install -d -m 700 -o 65534 -g 65534 /etc/nexus/auth
install -m 600 -o 65534 -g 65534 tokens.json      /etc/nexus/auth/tokens.json
install -m 600 -o 65534 -g 65534 tokens.json.lock /etc/nexus/auth/tokens.json.lock
```

3. 编排里加挂载与环境（目录挂载，不能只挂单个文件，否则网页改完后宿主的原子替换不会反映进容器）：

```yaml
    environment:
      NEXUS_AUTH_TOKEN_FILE: /var/lib/nexus/auth/tokens.json
    volumes:
      - /etc/nexus/auth:/var/lib/nexus/auth
```

父目录必须是 0700 且属主为 65534，文件 0600；容器只读根不影响 bind 挂载。若不想开放网页改写，把挂载改为 `/etc/nexus/auth:/var/lib/nexus/auth:ro`，此时页面会提示“凭据文件不可写”，轮换继续走 CLI。

4. 重建 CORE（`up -d`，不是 `restart`）。首次切换需要这一次重启，之后轮换和网页修改都不再重启。

本次 ACC-01 生产采用：宿主 `/etc/nexus/docker/core-auth`（0700，`65534:65534`）挂到容器 `/etc/nexus/core-auth`，`NEXUS_AUTH_TOKEN_FILE=/etc/nexus/core-auth/tokens.json`；`tokens.json` 与 `tokens.json.lock` 均为 0600、属主 65534。2026-11-23 切换后 `GET /api/v1/access/admin-token` 返回 `{"mode":"file","editable":true,"revision":1}`，CORE healthy，管理员登录 200。切换后 `core.env` 的 `NEXUS_ADMIN_TOKEN` 不再参与校验，只保留用于回滚。

网页自定义管理员令牌：用当前令牌登录 `https://pet.muyin.site/`，进入“权限”，点“修改管理员令牌”，填入当前令牌与新令牌（32–256 个可见 ASCII 字符），保存后旧令牌立即失效，**不需要重启 CORE**。新令牌只显示在输入框中，页面不回显历史值；请用受保护方式保存。

三个必须知道的失败方式：文件必须是**绝对路径**且能被 65534 读到，否则 CORE **启动即失败**（日志 `NEXUS_AUTH_TOKEN_FILE is unavailable or invalid`），不是降级运行；文件损坏或不安全更新在请求期固定返回 503，不回退到 env；容器健康检查也读 `admin.current.token`，文件配错会让容器一直 unhealthy 反复重启。

## 设备 MQTT 凭据（DEVICE-CRED-01）

管理台「设备 → 展开设备 → 设置与状态 → MQTT 凭据」可以签发、轮换和撤销每台设备的台账凭据。CORE 只保存密钥的 SHA-256 指纹并一次性显示明文，**不会写 Mosquitto 配置**：本编排把 `/etc/nexus/docker/mosquitto/config` 以只读方式绑进 Broker，CORE 容器也没有该路径。签发后按下面三步在宿主机执行：

```bash
# 1. 用一次性密钥写入密码文件（路径与编排里的 bind source 一致）
sudo mosquitto_passwd -b /etc/nexus/docker/mosquitto/config/passwd dev-<deviceId> '<一次性密钥>'
# 2. 在同一目录的 acl 追加该设备命名空间（按页面显示的原文粘贴）
echo 'topic readwrite nexus/v1/devices/<deviceId>/#' | sudo tee -a /etc/nexus/docker/mosquitto/config/acl
# 3. 让 Broker 重新读取配置
docker compose -f /etc/nexus/docker/docker-compose.yml restart mqtt
```

注意：

- ACL 用 `#` 覆盖该设备的全部子主题；`*` 只匹配一级，不要用它替代。
- 设备端要使用同一用户名与密钥（Web 配网或串口配置）；换密钥后设备必须重连一次才会生效。
- 轮换会给出宽限期（默认 24 小时）。在这段时间新旧密钥都应保留在 `passwd` 中，等设备切换完成后再执行 `mosquitto_passwd -D passwd dev-<deviceId>` 删除旧条目——CORE 不会代删，理由同上。
- 撤销只更新台账状态；Broker 里的账号仍需手工删除，否则旧密钥依然能连。
- 台账状态（`none`/`active`/`rotating`/`revoked`）不代表 Broker 实际配置，接入引导顶层的 `credentialsProvisioned` 因此仍是 `unknown`。

接口与语义见 CORE 仓 `docs/device-credentials.md`。

## 验证与边界

2026-11-15 本地 `docker compose -f deployment/1panel/docker-compose.yml config --no-env-resolution -q` 退出 0，Compose 版本为 `v5.5.0`。隔离运行使用本机缓存的真实发布 amd64 CORE（当时为 `230c809`）、arm64 PostgreSQL/Mosquitto 和本地 arm64 初始化镜像；临时编排只替换私有目录和随机回环端口，使用独立项目/卷，通过 `up -d --no-build --wait --wait-timeout 240` 启动。

7 组检查通过：原文件三服务/拉取策略/端口解析、断网初始化生成私有配置、缺少本地 CORE 时直接拒绝、真实发布 amd64 CORE 非 root/只读/内存约束、HTTP 401/鉴权后数据库与 MQTT ready/页面 200、严格 MQTT CA/主机名验证、全部重建后隔离消息记录及原凭据保留。实际执行了 `down` 后重建；收尾仅删除该随机测试项目/卷/私有目录，报告留在忽略目录 `build/1panel-local-image-verification.json`。3 段 shell 命令经 `bash -n` 检查，本次所涉说明中的 30 个本地链接及 `git diff --check` 通过。

2026-11-17 把本地编排升级到 `e76732b` 后重新核对：`config --no-env-resolution -q` 退出 0（Compose v5.5.0）；隔离项目使用上述 amd64 镜像 ID、公开 init 镜像与临时私有目录，`up -d --no-build --wait` 后三服务健康。7 项检查通过：未带令牌 `/api/v1/status` 返回 401，带令牌 database/mqtt 均 ready，页面 200；Flyway `V21`–`V23` 执行；`POST /api/v1/session {"remember":true}` 返回 `persisted:true`，仅凭 Cookie 可读会话；会话卷目录为 `65534:0700`、会话文件 `0600`；重启 CORE 后同一 Cookie 仍返回 admin 且状态 ready；CORE 保持 UID 65534、只读根、1 GiB、`cap_drop ALL`；MQTT TLS 1.3 证书 `CN=mqtt.muyin.site` 经私有 CA 校验通过。实际执行了 `down -v` 并删除临时私有目录；未连接目标服务器，未执行线上部署或真实数据库迁移。

本项只调整本地编排与使用说明，未改变 CORE 或设备代码；项目事实沉淀到本目录和[任务台账](../../docs/18-requirements-and-progress.md)，无需更新通用 Skill。服务器端 Docker 为用户提供的 27.3.1，实际 Compose/1Panel 版本、HTTPS/网络、真实数据库和设备仍须目标环境核对；本地模拟不是 Debian 目标机部署或远程 CI。Docker `pull_policy` 和启动策略以[官方定义](https://docs.docker.com/reference/compose-file/services/#pull_policy)为准。
