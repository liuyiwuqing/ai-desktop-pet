# config

非敏感配置以 `schemas/` 为事实源，生成 C/TS 类型。NEXUS v1 消息、回执、在线状态保留在 `integration/contracts/nexus-v1/`，本目录镜像由 CI 检查漂移。新增设备配置/遥测契约见 [device-management-v1](../integration/contracts/device-management-v1/README.md)。

- `schemas/`：设备配置、板型、扩展、消息、成功/拒绝回执、在线状态、遥测，共八个 Schema。
- `profiles/`：boardId 与功能开关；不放引脚和凭据。
- `examples/`：非敏感配置与协议样例。

```bash
python3 tools/schema_codegen.py
python3 tools/schema_codegen.py --check
python3 tools/ci/check_contracts.py
```

生成物 `firmware/components/contracts/include/pet/config_generated.h` 与 `integration/contracts/generated/pet-contracts.ts` 入库，不手改。

## 运行配置

`svc_config` 存于 `pet_cfg/settings_v1`，单 blob 包含版本/revision/CRC/规范 JSON，提交后回读；CAS 防并发覆盖。首次缺省使用默认值，损坏/未知版本/不确定提交锁写，不自动擦除。UI 通过后台拥有者写入，NVS 不在显示回调写。

音量、亮度、静音、手动/定时 DND、消息容量/时长和固定 UTC 偏移生效。容量缩小不删除已接收消息，只限制新收件。时区只支持 Asia/Shanghai、UTC/Etc/UTC、UTC±HH:MM，不自动计算 DST。当前 UI 只有 `zh-CN`；Schema 保留 `en-US` 标识，但固件申请时明确拒绝，不返回假成功。

物理 CLI 使用 `get/settings`，远程只接收同设备、非 retained、有时效的完整配置，均使用 expectedRevision。详情见 [维护手册](../docs/10-maintenance-recovery.md)。

## 私有凭据包

凭据文件不放本目录/仓库。使用权限 `600` 的私有 JSON，完整替换 `pet_provision/bundle_v1`：

| 分组 | 必填 | 可选 |
| --- | --- | --- |
| `net` | `wifi_ssid` | `wifi_pass` |
| `nexus` | `device_id`、`mqtt_uri`、`mqtt_user`、`mqtt_pass` | `mqtt_ca` |
| `xiaozhi`（整组可省） | `ws_uri`、`ws_token`、`device_id`、`client_id` | `ws_ca` |
| `control`（整组可省） | `api_uri`、`token`（至少 32 字符） | `ca`、`allowed_apps`、`allowed_projects` |

MQTT/WSS/控制分别要求 `mqtts://`、`wss://`、`https://`；CA 省略时使用 SDK 信任包。allowed_apps/projects 为逗号分隔白名单 ID，最多 16 项，每项 `[a-zA-Z0-9_-]{1,40}`，并须与控制网关一致。配置入口拒绝未知/重复字段、深层 JSON 和超长输入，不导出凭据，日志不输出秘密。

新包不存在才兼容旧 namespace/key；包存在但损坏/缺必填键，不拼接旧凭据。设备身份已存在时禁止更换，避免历史回执错发。凭据成功后需显式重启；首次配置串口默认关闭。发布签名私钥与供应商密钥始终留在维护者/服务端，不下发设备。
