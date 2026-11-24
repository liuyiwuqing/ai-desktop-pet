# 生产观测脚本（只读采集）

ACC-01 的 24 小时观测与后续每轮发布窗口用的两段采集脚本。它们**只在目标机的 `nexus-deploy` 账号下运行**，不进入应用、不改数据库、不改 Broker 配置，唯一的产出是 `~/nexus-observe/` 下的 ndjson。

此前这两个脚本只存在于服务器上，仓库里只有文档提到它们；因为一次静默的令牌抓取失效（见 [ACC-01 §10.3.3](../../docs/29-acc01-production-deployment-plan.md)）才发现「在跑的东西不受版本控制」。本目录是它们的受控副本。

| 仓库文件 | 目标机路径 | SHA-256（2026-11-24 与在跑版本一致） | 周期 |
| --- | --- | --- | --- |
| `service_watch.sh` | `~/nexus-observe.sh` | `7402bb39e8b80f0530a8daac8d8d5c8e1881afd6a46fcf0eb007d37db2f336ae` | 900 s |
| `device_watch.sh` | `~/nexus-device-observe.sh` | `8a0f32142e7d514d27bbc54befaf7ac7ff54a813c584d71cb73994477df3ea7e` | 300 s |

两个文件与目标机在跑版本**逐字节相同**（`sha256sum` 两侧一致）。改动其中任何一个都必须同时在目标机替换并重算哈希，否则这里的哈希和「受控」两个字就没有意义。

## 启动与停止

```sh
# 服务端窗口：默认 24 小时、15 分钟一条（DURATION_S / INTERVAL_S / LOG_FILE 可覆盖）
LOG_FILE=$HOME/nexus-observe/observe-acc04-$(date -u +%Y%m%dT%H%MZ).ndjson \
  nohup sh $HOME/nexus-observe.sh >/dev/null 2>&1 </dev/null &

# 设备连通性：5 分钟一条。注意当前版本把设备与日志路径写死为 dodo-1 / device-dodo-1.ndjson，
# 不读环境变量；换设备要改脚本（见「计划中的改动」）。
nohup sh $HOME/nexus-device-observe.sh >/dev/null 2>&1 </dev/null &

# 先自检再正式跑：30 秒窗口、10 秒一条，确认首条样本 status 是双 ready 而不是 unauthorized
LOG_FILE=/tmp/probe.ndjson DURATION_S=30 INTERVAL_S=10 sh $HOME/nexus-observe.sh
```

停止**不要**用 `pkill -f nexus-observe.sh`：远端命令本身含同一字符串，`pkill` 会把自己的 shell 一起杀掉（表现为 `Exit code 255`）。用 `ps -eo pid,cmd | grep 'sh /home/nexus-deploy/nexus-observe.sh'` 找 PID 后 `kill`。

`device_watch.sh` 目前没有结束条件，会一直跑；窗口收尾时需要手动停。

## 只读契约

只做 `docker inspect/stats/logs/cp`、`psql` 计数查询、`curl /api/v1/status`、`df`、`openssl s_client`。不写库、不重启容器、不改配置、不向设备下发任何东西。令牌只用于这一次本机 `curl`，不落盘（`mktemp` 0600 读完即删）、不写日志。

## 服务端窗口字段

`ts`、`core`/`pg`/`mqtt`（`状态/健康/重启数`，Mosquitto 没有 healthcheck 所以是 `状态/重启数`）、`status`（`/api/v1/status` 原始 JSON）、`disk_avail_kb`/`disk_used_pct`、`pg_conn`、`core_mem`、`mqtt_reconnect_16m`、`http5xx_last200`（openresty access.log 最近 200 行的 5xx **滚动计数**，不是增量）、`https_cert_not_after`/`mqtt_cert_not_after`。

## 结论怎么看

```sh
python3 tools/observe_report.py ~/nexus-observe/observe-<窗口>.ndjson \
  --device ~/nexus-observe/device-dodo-1.ndjson
```

退出码 `0` 通过、`2` 待复核、`1` 失败。报告会把「服务不健康」和「采集通道 401」分开——两者在原始日志里长得一样。

## 已知坑（都真实踩过）

- **令牌从 tar 流里抓**：`docker cp ... -` 输出的是 tar，早先版本用 `awk '/"token"/ {print $2}'` 取令牌，只有 `tokens.json` 是多行缩进时才碰巧正确。该文件被重写成单行紧凑 JSON 后，每条样本都 401，而容器健康、5xx 为 0，日志表面完全正常——ACC-01 的窗口因此瞎了 23 小时。现版本改用 `mktemp` + `python3` 按结构取 `admin.current.token`。
- **`pkill -f` 自杀**：见上。
- **`status` 为 `null` 与 `{"error":"unauthorized"}` 意思不同**：前者是没取到令牌/请求没发出去，后者是令牌被拒（通常是轮换或抓取错了），都不等于服务不可用。
- **重启计数在容器重建后归零**：窗口内若重建过 CORE，`RestartCount` 会掩盖重启次数，应以 `StartedAt` 变化判定。
- **不要原地改正在运行的脚本**：`sh` 会按偏移继续读同一个文件，覆盖成长度不同的内容会让在跑的进程执行到错位字节。改脚本先停进程，或换文件重起。

## 历次窗口

| 窗口 | 窗口文件 | 覆盖对象 | 结论 |
| --- | --- | --- | --- |
| ACC-01 | `observe-20261123T074228Z.ndjson`（97 条） | `sha-230c809…` 基线 | REVIEW：容器与资源平稳，`status` 仅 4 条有效（见 §10.3.3） |
| ACC-04 | `observe-acc04-20261124T1414Z.ndjson`（进行中） | `acc04-c88957e` | 未到期 |

## 计划中的改动（未落地，落地后同步哈希）

- 两个脚本写 PID 文件，`--stop` 子命令代替手工 `kill`，顺带消除 `pkill -f` 自杀风险。
- `device_watch.sh` 增加 `DURATION_S`，与 `service_watch.sh` 对齐，避免无限循环长期占用；同时把设备与日志路径提成 `DEVICE`/`LOG_FILE`，不再写死 `dodo-1`。
- `service_watch.sh` 记录容器 `StartedAt`，用于识别窗口内的重建。
