# 19 · 远程 OTA 管理

2026-11-13，SW-04。设备、CORE、管理台、Schema 与升级工具成套交付；功能默认关闭。沿用签名下载、分区、回滚与健康门控，不更换公钥、模型或分区。CORE 配套提交 `2808ed7` 已推送，04 代码和本文同项交付。当前未部署生产服务，也未升级真实设备。

## 公开接口与边界

管理员 HTTP → 受限 MQTT → 设备持久事务 → 状态回读。完整字段与主题见 [remote-ota-v1](../integration/contracts/remote-ota-v1/README.md)，当前实现分别为设备 `svc_ota/remote.c` / `app_ota_remote.c`、CORE `RemoteOtaService` 和管理台 `RemoteOtaPanel.vue`。

| 接口 | 行为 |
| --- | --- |
| GET `/api/v1/ota/releases` | enabled 与最近 100 个发布目录；关闭时返回空目录 |
| POST `/api/v1/ota/releases` | `{releaseId,manifest}`，首次 201，同目录同 manifest 200，异内容 409 |
| GET `/api/v1/devices/<deviceId>/ota` | 最新完整回读、fresh、canInstall/installBlock、canReboot、最近 25 次部署及命令 |
| POST `/api/v1/devices/<deviceId>/ota/deployments` | `{requestId,releaseId,bootId}`，首次 202，同内容重试 200，冲突 409 |
| POST `/api/v1/devices/<deviceId>/ota/deployments/<requestId>/reboot` | `{requestId:独立命令ID,bootId,confirm:"REBOOT_DEVICE"}`；首次 202，同命令重试 200 |
| GET `/api/v1/devices/<deviceId>/ota/deployments/<requestId>/audit` | 最多 200 条 admin/core/device 状态审计，不保存令牌 |

HTTP 沿用管理员 Bearer 鉴权，没有增加多用户/RBAC。CORE 校验 manifest 格式并登记元数据，**不把登记当作签名认证**；设备安装时使用构建固定的公钥验签。发布目录不可换包，实际文件仍须先放到设备已固定的 HTTPS 发布根。命令不接受 URL、公钥、模型写入或任意路径。

## 设备事务与结果核对

- 目标固定为 releaseId、version、imageSha256、imageSize、releaseSequence。已签名 manifest 的五项必须完全匹配，再进入现有写入流程；板型、模型、分区与配置/成长 Schema 仍受原身份校验保护。
- 每次启动生成 128 bit 随机 bootId。命令绑定当前启动、设备身份、UTC 秒及最多 300 秒有效期。未对时、retained、未知/重复字段、错设备、旧启动或非法目标一律拒绝。
- 安装先写入带版本、长度和 CRC 的 NVS 事务账本并完整回读，再排队。写入调用报错但回读完全一致可认定持久化；无法确认则停止新动作。损坏记录公开 unavailable，不自动清空或执行。
- 同一安装 ID/目标重投只回答已有结果，不再次排队；另一在途请求被拒绝。后续新请求还要求更晚的 issuedAt 和可接受的序号。CORE 保留完整历史，设备账本保留最近事务；二者都不靠 MQTT QoS1 提供动作去重。
- 下载完成进入 pending_reboot。管理员另行勾选并确认重启；重启命令有独立 ID，先持久化再排队。排队失败记 unconfirmed；重投不重复重启，启动恢复也不自动重放安装/重启。
- 新启动重新核对活动分区的实际哈希、版本与身份标记，并等待 ESP-IDF 镜像健康状态；verifying 后才能报告 healthy。曾经 healthy 的记录在后续启动仍会重新检查，其他镜像记 rolled_back。Flash、签名、公钥、启动器和健康验收仍需实际设备验证。

| 状态层 | 关键含义 |
| --- | --- |
| CORE pending / sent | 已保存 / 已预留下发尝试，尚不证明设备接收 |
| accepted | 设备确认保存命令，尚不证明安装或重启成功 |
| installing / pending_reboot | 设备报告安装进度 / 等待管理员确认重启 |
| rebooting / verifying / healthy | 已提交或观察到重启 / 新启动核验中 / 活动镜像已健康确认 |
| failed / rejected / interrupted / rolled_back | 有明确设备失败、拒绝、中断或回退证据 |
| expired / unconfirmed | 从未下发即到期或回读证明未执行 / 曾尝试下发而结果未知 |

CORE 每设备只允许一个在途或未知部署，先锁设备行和持久化发送预留，再发布相同 ID/信封；默认每 15 秒重投，到期不改 ID 强行动作。ACK 只确认命令接受，成功必须由匹配目标的完整回读证明；healthy/verifying 还必须来自不同于安装时的启动。

回读非 retained，每 5 秒发布；90 秒内才可用于新动作，未来超过 5 秒拒绝。同一启动只接受更高 reportSequence 且时间不倒退；曾出现的旧 bootId 不能重新成为当前启动。回读不更新原 state/LWT 的在线心跳。

安装确认丢失后，未知结果阻止新请求。到期后的同启动、空闲且完整可用回读可证明未接收安装，才解除。重启结果未知时也不能换命令 ID 重发；只有到期后的匹配 pending_reboot 回读、空 rebootId 证明原命令未接收，才允许管理员重新确认。无法证明时继续阻止，通过设备诊断核对，不提供跳过账本的“强制升级”。`devices.enabled` 继续只控制新消息。

管理台轮询不自动安装或重启；提交结果未知时保留原 ID，提供“重试同一请求”。启动标识变化会取消尚未提交的重启确认。关闭面板后键盘焦点回到设备操作入口。状态过期、不可用、读取失败和其他在途部署均阻止新动作。

## 成套启用与回退

1. 备份 CORE 数据库与 Broker 配置，部署配套 CORE，按正常 Flyway 流程迁移 V11；当前仅在独立测试库执行过迁移。
2. 预览 Broker 升级：在 CORE 执行 `python3 tools/upgrade_broker_policy.py --directory <配置目录>`。明确执行 `--apply` 才写入，先建立 `.remote-ota-v1-backup`，保留先前 `.management-v1-backup`；只接受已知旧模板，自定义 ACL 需人工合并，脚本不重启 Broker。
3. 设备需有首次 USB 安装的回滚启动器、现有 OTA 分区和生产公钥/HTTPS 根。启用 `CONFIG_PET_OTA_ENABLED=y`、`CONFIG_PET_OTA_REMOTE_ENABLED=y`；远程开关默认 n。`full.defaults` 仅用于编译全部适配器，其 `.invalid` 地址与临时测试公钥不能用于发布。
4. 对相应设备账号核对只允许自身 ota 订阅以及 ota_acks/ota_status 发布；清除可能残留的 retained OTA 命令。CORE 的三个主题权限必须同时就绪，再设置 `NEXUS_OTA_ENABLED=true` 并按部署流程重启 CORE。
5. 等待新鲜升级回读，在设备“固件升级”中登记签名 manifest、选择匹配版本并安装；观察 pending_reboot 后另行确认重启，核对新启动、实际镜像健康和审计。

停止新远程升级可关闭 CORE/固件的远程开关；此操作不会撤销已经排队或切换启动分区的动作，也不能当作回滚证明。已有发布包仍是其原基线快照；需要包含本功能时应重新构建并生成新的服务端包，不能沿用旧包名称冒充新版本。

## 已执行验证

| 检查 | 结果与边界 |
| --- | --- |
| C 事务公开 API，普通及 ASan/UBSan | 通过；持久安装/重启、重复隔离、错时/retained/忙碌、后续启动健康重验、损坏账本、写入错误回读与重启排队未知；Flash/SDK 为托管边界 |
| CORE 完整构建 | 50 项测试中 46 通过、4 条按环境跳过；含 Vue 类型/生产构建、真实 Java 21/PostgreSQL 17/Mosquitto 与 V11 隔离迁移；不读取真实实例配置 |
| 默认关闭 HTTP 与重新打包 | 独立新增用例通过：未启用返回 enabled=false / 空目录，登记拒绝 404；随后 package 通过 |
| 四种固件隔离预设 | default/full/off/tones 均退出 0，源码清单与产物 SHA256 核对一致；full 含远程 OTA，产物仅本地测试，未签名发布/烧录 |
| OTA 专项 HTTP/MQTT | 5 通过，C peer 用例在普通 Maven 调用中按条件跳过；含身份/目录不可变、ACL 隔离、旧启动、安装/重启未知与回读；随后 C peer 单独执行通过 |
| 实际 Java→MQTT→C→回读 | `PET_HOST_SANITIZE=1 python3 tools/ci/test_remote_ota_contract.py` 通过；同一实际 C 事务处理重复安装，安装动作一次，重启后由 SDK 边界提供健康状态；未执行真实下载/Flash/重启 |
| 管理台浏览器 | OTA 22 项通过；原管理/更名/记忆回归 38 项通过。真实 Vue/DOM，HTTP 为隔离状态夹具；含文件选择、未知结果、确认失效、键盘焦点和 390 px 布局 |
| 原消息契约、ACL 工具、服务端交付 CLI | Java→C 存储/ACK→Java 契约通过；ACL CLI 3 项、交付 CLI 7 项通过；没有改原 inbox/acks/state 契约 |

固件四种当前隔离预设与本轮远程 CI 的最终结果在 [18](18-requirements-and-progress.md) 继续登记，不能用本地结果替代远端执行。目标机生产 TLS、真实签名发布、设备 NVS 掉电/切换分区/健康回滚、真实网络中断与整机稳定性尚未验收。

常用验证：

```sh
PET_HOST_SANITIZE=1 bash tools/ci/test_ota_remote_host.sh
PET_HOST_SANITIZE=1 python3 tools/ci/test_remote_ota_contract.py
bash tools/ci/test_remote_ota_ui.sh
JAVA_HOME=$(/usr/libexec/java_home -v 21) python3 tools/ci/test_nexus_contract.py
python3 tools/ci/check_contracts.py
# CORE
python3 tools/test_broker_policy.py
python3 tools/run_local.py build
```
