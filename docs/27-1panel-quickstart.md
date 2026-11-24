# 1Panel：直接拉取镜像部署管理台

2026-11-15 已验证版本：CORE `230c809`，支持 `linux/amd64`、`linux/arm64`。发布与按 digest 回拉的 [运行 43620052738](https://github.com/liuyiwuqing/nexus-core/actions/runs/43620052738) 全部成功，7 项无源码初始化/编排/鉴权/静态资源/TLS/重建持久性检查通过。当前发布物为 CORE `3fdca79`（含 V29 伙伴空间与遥测契约修复），发布运行 [45402756973](https://github.com/liuyiwuqing/nexus-core/actions/runs/45402756973)，digest 见下方命令；上线前基线为 `88d2e90`（[45393714296](https://github.com/liuyiwuqing/nexus-core/actions/runs/45393714296)）与 `d2711f7`（V28，[45103288550](https://github.com/liuyiwuqing/nexus-core/actions/runs/45103288550)）。2026-11-24 已按本页在 `pet.muyin.site` 完成两次既有安装升级（离线导入、V29、伙伴空间启用、遥测契约修复），执行证据见 [ACC-01 §15/§16](29-acc01-production-deployment-plan.md)。完整门禁与迁移步骤同见该计划。

下文命令中的 digest 为历史基线，部署前按发布运行产物替换。

## 首次安装

如果已通过 `docker save/load` 导入镜像，或服务器访问 GHCR 镜像层出现 TLS 超时，使用新增的[本地镜像部署文件](../deployment/1panel/docker-compose.yml)和[配套步骤](../deployment/1panel/README.md)。它以完整提交标签引用 CORE，并设置 `pull_policy: never`；私有初始化、基础服务下载、代理和已有卷保护按该说明操作。下文保留按 digest 在线拉取的原方案。

GHCR 当前为私有镜像。在终端登录，并在 1Panel 的镜像仓库配置相同 GHCR 读取账号；登录密码使用有包读取权限的 `read:packages` PAT，不要放进命令、编排或聊天：

```sh
docker login ghcr.io -u liuyiwuqing
```

确认 `/etc/nexus/docker` 尚不存在，执行以下命令。已有目录不能覆盖或重新初始化，按下节升级。域名沿用已提供的 `pet.muyin.site` / `mqtt.muyin.site`：

```sh
(
set -e
NEXUS_CORE_IMAGE='ghcr.io/liuyiwuqing/nexus-core@sha256:bcf7f6191d61e2f1cd93592ff4e4f9c44eaa42e07688d7cb243254800049a037'
NEXUS_INIT_IMAGE='ghcr.io/liuyiwuqing/nexus-core-init@sha256:70d178fb6236b68939438a5f22b151deb374bc72968c78efa2793216237c8055'
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
)
```

无需源码包、宿主 Python/Java/Node/Maven。初始化镜像只生成自己的凭据、证书、设备配置与编排，不连接设备或供应商，已有/残留目录会原样拒绝覆盖。

在 1Panel「容器 → 编排」创建 `nexus-production`，粘贴 `/etc/nexus/docker/compose.1panel.yml` 内容。该编排已经包含三个服务、镜像 digest 和绝对文件路径，不依赖源码、`compose.env` 或 `env_file.format`。`postgres.env/core.env` 此时已由初始化镜像生成，勿手工创建空文件凑数。

为 `pet.muyin.site` 配置 HTTPS 反向代理。若 OpenResty 为 host 网络，上游为 `http://127.0.0.1:18088`；若为 bridge 网络，需让 OpenResty 加入 CORE 的 edge 网络后用 `http://core:8088`，不能将 OpenResty 自身的 127.0.0.1 当作宿主机。CORE/数据库不直接公开。MQTT 8883 是否开放按是否接入设备配置；DNS、防火墙、证书和实际代理网络须在目标机核对。

通过安全编辑器读取私有 `/etc/nexus/docker/core.env` 的 `NEXUS_ADMIN_TOKEN` 登录。管理员可在“权限”签发 operator/viewer 独立令牌。首次引导区分登记、凭据、连接、接收与展示；登记成功不代表设备已联网。SSE 若被代理缓冲，按 [实时更新说明](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/console-events-and-setup.md) 关闭该接口的代理缓冲并保留合理读取超时；页面同时保留轮询回退。

## 已有安装升级

先备份原私有目录与 PostgreSQL 卷/数据库，并在独立库验证恢复。保留原项目名、卷、凭据、CA、设备配置和原编排；只把 CORE 的 image 改为上述 CORE digest，然后拉取/重新创建 CORE。**不再运行初始化镜像，不混用新密码与旧数据库卷。** 新镜像会按 Flyway 顺序补齐缺失迁移（当前分支最新 V28）；升级前核对 `flyway_schema_history`，完整备份、迁移与回滚步骤见 [ACC-01 执行计划](29-acc01-production-deployment-plan.md)。旧镜像回退前必须确认数据库兼容，换镜像不等于回退数据库。

RSS/Atom 和室外天气默认关闭。按 [拉取来源配置](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/pull-connectors.md) 修改私有 `core.env` 的 `NEXUS_PULL_FEEDS_JSON` 后重新创建 CORE，再在接入页测试/启用；单纯 restart 不重新读取 env。Webhook 来源凭据在管理台创建后仅显示一次。Halo/AI/记忆/Bridge/清理/OTA/成长同步仍遵循各自默认开关，不因本次升级自动接入真实服务。

伙伴空间（V29 起）默认关闭。开启需同时满足三处，缺一项都无法换装：编排加 `NEXUS_COMPANION_ENABLED: "true"`；Broker ACL 按 `tools/broker_policy.py` 的当前标准增加 `companion_status`/`appearance_acks` 读、`appearance` 写（含设备侧 `pattern`），并把 `message_size_limit` 提到至少 3072 后 SIGHUP 重载；设备固件开启 `CONFIG_PET_COMPANION_SYNC_ENABLED`。只改 CORE 环境变量会出现“页面可见但不能应用”。

完整使用、备份及恢复见 [CORE 镜像部署](https://github.com/liuyiwuqing/nexus-core/blob/codex/core-baseline/docs/1panel-deployment.md)。真实部署、生产迁移/HTTPS、供应商、设备与长期稳定性验收尚未执行。
