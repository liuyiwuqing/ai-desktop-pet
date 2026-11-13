# 21 · 设备成长只读同步

2026-11-13，SW-06-A。设备成长数据上传 CORE 并在管理台展示；本地 v4 存档与 CORE 专注档案各自拥有数值。服务端奖励下发仍为 [04 §5.3](04-growth-game.md#53-服务端同步二期) 的二期范围。

## 公开接口与数值归属

- MQTT `nexus/v1/devices/{deviceId}/growth_status`，QoS 1、非 retained、UTF-8 JSON 不超过 1536 B。Schema 与示例在 `config/schemas/growth-status.schema.json`、`config/examples/growth-status.example.json`。
- `GET /api/v1/devices/{id}/growth`：`enabled/deviceId/fresh/snapshot`；设备不存在返回 404，未收到数据时 `snapshot=null`。沿用管理令牌鉴权。
- `GET /api/v1/pet/devices`：最多 100 个已注册设备，含名称、设备启用状态及上述快照。成长页展示只读设备档案，同时保留 CORE 专注入口和计时/结算规则。
- `growthSchema=4` 表示当前设备模型版本。快照含本级经验、门槛、金币、亲密、心情、精力、饱食、健康、任务、成就及存储/时间状态；不是可导入的备份，不含凭据或对话。
- 本地等级 1–99，门槛 `80+40*(level-1)+8*(level-1)^2`；CORE 每 100 经验升一级。CORE 不相加两边总量、不基于快照发奖、不回写设备 NVS。既有专注 API、奖励额度和设备备份恢复保持。

## 顺序、重复与故障

设备启动时生成 128 bit 随机 `bootId`，同次启动 `reportSequence` 单调增加；15 秒上传一次，未对时、未连网或成长快照暂不可读时跳过。上传不强制刷盘，不增加成长 NVS 写入；`unsaved=true` 表示变化可能在掉电后丢失。序号耗尽后停止本次启动的上报，不绕回旧序号。

CORE 严格拒绝未知/重复键、尾随 JSON、越界值、主题与 `deviceId` 不匹配及未来超过 5 秒的数据。设备必须已注册且启用；Broker ACL 只允许设备发布自己的成长主题，CORE 仅读取。

同一启动只接受更大序号且时间不倒退的报告；新启动可从 1 开始，但已经退役的 `bootId` 不能再次成为当前启动。观察记录与已见启动集合在 V12 同事务持久化，设备行锁串行化处理。未知的新启动仍依赖已认证设备与可信时间，不构成硬件远程证明。

以设备 `observedAt` 计算 90 秒新鲜度，不用重复收到旧消息延长有效期。过期数据仍可读；管理台明确显示过期、禁用、未同步、存档不可写、回退恢复、未对时及未落盘。刷新失败保留上次数据并显示错误。成长报告不修改消息链路的在线心跳。

## 成套启用

默认设备 `CONFIG_PET_GROWTH_SYNC_ENABLED=n`、CORE `NEXUS_GROWTH_SYNC_ENABLED=false`。full 验证预设启用设备适配；正式部署需显式启用两端并完成 V12 和 Broker ACL 配套。

```sh
# CORE：默认只读预览；--apply 仅在运维确认后备份并更新指定配置
python3 tools/upgrade_broker_policy.py --directory /srv/nexus/broker/config
```

识别 legacy、management-v1、remote-ota-v1 及当前标准 ACL；自定义规则要求人工合并。实际应用使用独立 `.growth-sync-v1-backup`，保留先前 `.management-v1-backup` 与 `.remote-ota-v1-backup`；本次只对临时目录验证，未修改真实 Broker。新增规则为 CORE `topic read .../+/growth_status`、设备 `pattern write .../%u/growth_status`，无设备跨端读取或写入权限。

已有离线 ZIP 保持其旧快照；本项配套新包见下节。关闭功能后保留镜像数据，不伪装新鲜，也不擦除设备存档。

## 配套离线交付包

DEL-04 使用 CORE `3e38ee5b53d3b722aa8094695270615d313aad26` 和 04 `69b6308dbbfc0c7e67efbe753a9250078776e9d2` 的干净快照，生成 `build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-3e38ee5.zip`（format=2、18 文件，含 121 份 CORE 源文件）。SHA256：`8f888362855fcedbad186e4e9efef76a61859b22d7703599dae15a3a1cc17237`。包内 JAR、工具与所列提交一致；不含真实配置或凭据，不提交 ZIP。

```sh
python3 tools/server_release.py create --core ../nexus-core \
  --output build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-3e38ee5.zip
python3 tools/server_release.py verify \
  --bundle build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-3e38ee5.zip
python3 tools/ci/test_server_linux.py --auth-rotation \
  --bundle build/server-release-20261113/nexus-server-0.1.0-SNAPSHOT-3e38ee5.zip \
  --evidence build/server-release-20261113/linux-auth-rotation-3e38ee5.json
```

三条命令均退出 0。同包实际 JAR 在 Linux UID 65534、只读根文件系统下完成首次启动和主动重启，V1–V12 清单与包内迁移资源一致，PostgreSQL/MQTT/TLS 就绪，两份管理台资源与包内字节一致。admin 凭据文件由同 UID 的隔离 CLI 原子替换，过渡、到期、撤销、作用域隔离、损坏/宽权限拒绝及修复通过；热轮换期间进程未重启。临时容器、私有卷及网络已清理。

`913b3af` / `9559429` 旧包哈希保持不变。该检查没有启用 Linux Memory MCP，也未验证目标 systemd、生产 TLS、真实供应商或设备成长链路；成长协议的实际 C/Java 回环证据见下节。

## 验证与边界

- 先以 HTTP 404 建立失败反馈，再通过真实 Java/PostgreSQL/Mosquitto 的公开流程；覆盖独立档案、同启动重复/乱序、旧启动、非法数值/字段、设备隔离、过期及禁用。
- `python3 tools/run_local.py build`：64 条中 59 通过、5 条条件跳过；新增固件 producer 可选用例已用 `PET_HOST_SANITIZE=1 python3 tools/ci/test_growth_sync_contract.py` 单独通过。实际设备引擎/JSON → MQTT → Java/数据库/HTTP 回环通过；未连接实机 NVS。
- `PET_HOST_SANITIZE=1 bash tools/ci/test_host.sh` 通过，含新格式化边界；旧 Java→C→Java 消息协议回归通过。新增 Schema/示例/生成代码检查通过。
- `bash tools/ci/test_growth_sync_ui.sh`：真实 Vue/浏览器 18 项通过，含失败/恢复、禁用、空数据、各存档状态、名称转义及 390 px 布局；HTTP 为隔离夹具。ACL 维护 CLI 4 项、交付 CLI 10 项通过。
- 四种固件预设 default/full/off/tones 均退出 0；每份证据的 363 份源码与当前文件一致，6 个产物哈希逐项核对。应用分别为 2,885,120 / 3,622,384 / 225,760 / 2,925,840 B。首次构建期间更新配置说明触发输入快照保护，冻结后重新构建并核对，未采用混合来源证据。真实 Broker/TLS、设备 Wi-Fi/时钟/堆栈水位及掉电稳定性尚未验收；只读快照不能替代恢复备份或奖励流水。

CORE 配套 `3e38ee5` / 04 实现 `08a8ad3` 均已推送并核对远程。后续 04 `69b6308` 对应 [CI 43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 六个 job 全部 success，含四固件预设及 full 的主机/协议/DSP/LVGL 检查；`08a8ad3` 的先前运行被并发策略取消，未计为成功。该远程 CI 不含配套 CORE 仓库的 Java/C 联调，后者使用上述本地真实数据库/Broker 回环证据。

后续若做服务端奖励，应先定义持久档案身份、事件来源、独立去重水位、配额与存档迁移兼容性。不得把两个档案直接相加，也不得借用本地对话/专注去重水位接收云端自报完成。
