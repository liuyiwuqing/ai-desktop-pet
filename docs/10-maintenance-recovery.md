# 软件维护与恢复

适用 `04`，2026-11-12。以下命令在根目录执行。脚本默认仅准备命令；只有 `--send --port <port>` 才发送。本文未执行烧录、真实凭据写入、设备控制或机械动作。

隔离构建预设、崩溃分区读取/离线解码见 [13](13-operational-software.md)，连续崩溃后的屏幕/BOOT 恢复见 [14](14-recovery-and-device-names.md)。诊断预设需要显式启用；恢复模式只额外尝试一次 LCD/BOOT，失败或再次崩溃后保留 USB 维护。两次长按并最终松手才重启；串口手工清锁不会立即重启。

## 物理维护入口

固件需开启 `CONFIG_PET_PROVISION_CONSOLE_ENABLED`。串口 115200，命令无回显，仅输出 `PET_RESULT`；不开启时不接收维护命令。发送时主机需安装 `pyserial`，脚本禁用 DTR/RTS 自动复位。

```bash
python3 tools/configure_device.py get
python3 tools/configure_device.py status
python3 tools/configure_device.py diagnostics
python3 tools/configure_device.py settings --file config/examples/device-config.example.json --revision 0
```

先 `get` 获取 revision，再提交完整配置；旧 revision 拒绝，无变化不加版本。单 NVS blob 原子替换及回读校验；损坏或提交不明锁写，不自动清空。默认音量 25%，支持手动/跨午夜定时 DND 和固定 UTC 偏移；当前 UI 仅中文，`en-US` 返回 `ESP_ERR_NOT_SUPPORTED`。

首次配网准备权限 `600` 的私有 JSON 文件，再用 `provision --file <private.json> --revision <status返回值>`；字段见 [config](../config/README.md)。完整凭据包原子替换、不导出，已存在设备身份不能通过此入口更换。成功返回 `restartRequired=true`，由维护者另行重启。超时不能直接视为失败，应先读取 revision/状态。

## 成长备份与恢复

```bash
python3 tools/configure_device.py backup --send --port <port> --output <new-backup.json>
python3 tools/configure_device.py restore --file <backup.json> --revision <当前成长revision>
```

备份信封为 `format/deviceId/revision/payload`，只含成长存档，不含凭据、消息、音频或对话。输出文件以 `600` 排他创建。当前成长 revision 通过新的 backup 获取。

设备校验同设备身份、CRC/版本/数值与当前 revision；恢复复用双槽事务。保留已发奖励水位、已领成就及当前日配额，避免回档重复领奖；这是受保护的状态恢复，不承诺逐字节回档。拒绝、写入失败和不确定分支已有主机覆盖，物理掉电恢复需实机验证。

## OTA 与发布包

`firmware/version.txt` 固定版本，构建嵌入 project/board/model/partition 身份。首次 USB 部署时开启 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`、`CONFIG_PET_OTA_ENABLED`，配置固定 HTTPS 根及 RSA-3072 公钥 DER。DER 采用 PKCS#1 `RSAPublicKey` 格式；私钥由发布者保管，不入库。改变公钥、分区或模型需配套 USB 部署。

```bash
python3 tools/package_release.py create --build <build-dir> --private-key <owner-only-private.pem> --sequence <递增整数> --output <新发布目录>
python3 tools/package_release.py verify --package <发布目录> --public-key <public.der>
python3 tools/configure_device.py ota-apply --release <安全目录标识>
python3 tools/configure_device.py ota-reboot
```

离线打包/验证不上传、不烧录。manifest 使用 RSA-PSS/SHA256（salt=32），固定固件名并声明大小、hash、版本、板型、分区、模型、配置与成长版本。USB 恢复计划另行签名，记录 bootloader/分区/otadata/模型等文件 hash 与偏移；验证器检查完整配套，输出不覆盖；OTA 构建还会检查签名公钥与镜像内置公钥一致。

设备从固定 HTTPS 根请求安全目录，拒绝跳转，下载总预算 120 秒。先验签和身份，再写非活动 app 槽，检查镜像/回读 hash，持久预留 releaseSequence 后切换启动槽。不写活动 app/model、不擦 NVS；切换失败可能保留序号，需更高序号重试。成功停在 `pending_reboot`，显式 `ota-reboot` 才重启。新固件连续 10 秒基础健康才确认，30 秒不通过请求 SDK 回滚；真实设备回滚未验收。

model 只有一个 1 MiB 分区，app OTA 只允许相同模型/分区身份；模型更新使用完整配套 USB 包。生产部署需要真实签名钥、可信 HTTPS 端及首次 USB 安装，构建测试钥不能作为生产钥。

## 02 旧工程迁移

只读来源：`02-desktop-pet/main/profile.h`、`profile.c`、`pet_hardware.c`、`partitions.csv`。`dodo_pet/profile_v1` 是 48 字节，`DODO` 魔数、版本 1、小端字段，末尾为前 44 字节 FNV-1a。包含 coins、bond、装扮、游戏成绩、每日任务/成就；光敏为独立 `dodo_pet/light_v1`，网络凭据不在成长 blob 中。

02 NVS 为 `0x9000/0x6000`，04 为 `0x9000/0x10000`；扩大范围覆盖 02 原 otadata，app 布局也不同。禁止直接重刷后宣称旧存档可用，禁止整段旧 NVS 拷到 04。

先确认设备实际固件/分区，只读备份原 Flash/分区与独立成长数据，记录 hash 并在副本解码。coins/bond 仅作候选人工映射；解锁位、成就、游戏分数和每日任务语义不同，不自动映射，不编造 EXP、对话/专注历史和请求水位。映射规则确认后再实施离线转换器，经 04 导入验证，保留原始备份。当前交付为迁移方案，没有读取实物 Flash、修改 02 或自动迁移数据。

USB 恢复先验签及全部 hash，再核对容量/旧分区/备份，只写已确认恢复计划中的偏移，不全片擦除。保留 NVS 须与旧布局兼容，实际恢复执行留待上板阶段。
