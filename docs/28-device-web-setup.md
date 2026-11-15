# 设备热点配网与小智绑定

适用于 ESP32-S3 基础发行硬件，`CONFIG_PET_SETUP_PORTAL_ENABLED=y`。设备插电后自行联网、运行；手机或电脑只用于首次设置，Mac 不承担日常转发。

## 手机设置

1. 未保存 Wi-Fi 的设备首次启动进入“网络设置”。连接屏幕显示的 `Qitomo-xxxx` 热点，输入屏幕上的密码；手机提示无互联网时选择保持连接。
2. 浏览器手动打开 `http://192.168.4.1`。选择或填写家庭 **2.4 GHz** Wi-Fi，输入密码并点“连接并保存”。设备取得 IP 后才保存；连接失败保留旧凭据，可以重试。
3. 如使用小智官方平台，点“获取小智绑定码”。出现数字码后，在 [xiaozhi.me](https://xiaozhi.me) 添加设备并输入该码。手机可暂时离开设备热点改用移动网络，也可使用另一台联网电脑；开发板继续通过家庭 Wi-Fi 核对绑定结果。
4. 等待设备页面提示“语音配置已保存”，再点“完成，返回宠物”。设备重启关闭热点，并使用保存的配置联网。需要时重新连接设备热点查看进度。

不接管理台时无需填写 MQTT。可展开“管理台连接（可选）”填写已部署 NEXUS 的 MQTT/TLS 地址、设备 ID、用户名、密码和可选 CA；该入口不替代服务端设备注册/ACL。已有 NEXUS 身份不能通过此处更换。

后续修改：主页长按 BOOT 打开菜单，短按选择“网络设置”，长按执行；设备重启进入设置窗口。配网时长按 BOOT 2 秒可返回宠物。窗口限时 **15 分钟**，退出或超时保留已保存凭据和成长存档，并记住本次离线选择，避免每次开机强迫配网。家庭 Wi-Fi 临时故障不会自动开放热点；通过菜单重新进入。

热点 SSID/密码每次进入重新生成，最多连接两台客户端。仅支持 2.4 GHz 开放网络或 WPA2/兼容网络，不支持企业认证；密码为空表示开放网络，非空需 8–63 字节或 64 位十六进制 PSK。不存在自动弹出网页的保证，始终可以手动输入上述 IP。

## 绑定与凭据边界

设备通过启用证书验证的 HTTPS 请求固定官方入口 `https://api.tenclass.net/xiaozhi/ota/`，使用 STA MAC 和独立持久化 client UUID。等待真实 SNTP 时间后发起请求，单次超时 5 秒、绑定轮询限时 5 分钟。响应必须提供合法数字绑定码或完整 `wss://` 连接信息；WSS URI/token、设备身份写入私有 NVS，不要求用户粘贴令牌。当前支持官方 `Activation-Version: 1` 与 WSS 配置，不支持需要 eFuse/HMAC 的另一种激活方式，也不把只有 MQTT 配置的响应假装成语音就绪。

官方入口名称含 `ota`，本功能只读取绑定和语音连接配置，**不下载、不烧写供应商固件**，响应的 `firmware` 字段不参与升级。正式 OTA 继续使用原有受控流程。协议参考为[小智官方固件实现](https://github.com/78/xiaozhi-esp32/blob/main/main/ota.cc)；真实官方平台响应和语音对话必须另行验收。

Wi-Fi、小智和 NEXUS 分组独立提交，沿用 `pet_provision/bundle_v1` 的 revision/CAS、完整校验、原子写入及回读。修改一组保留其他组；旧 namespace 凭据仅在尚无 bundle 时完整迁入，存在但不完整的旧分组拒绝覆盖。原 USB 全量 provision 继续要求 `net` 和 `nexus`；热点可只保存 `net`，此时正常模式启动 Wi-Fi/SNTP，NEXUS MQTT 保持禁用。仅 Wi-Fi 不意味着语音或管理台已经可用。

服务端密码、Wi-Fi 密码、WSS token、原始配置和录音均不写入 Git 或诊断输出。本机模板仍可用于维护，日常设置优先使用热点。

## 本地 HTTP 契约

HTTP 仅绑定 SoftAP 接口，固定 Host `192.168.4.1`（可带 `:80`），不在家庭网络接口提供配置服务。无跨域许可；写请求要求匹配 Origin、随机 128 bit 会话 nonce 和当前 `expectedRevision`，JSON 最大 10,000 字节，拒绝未知/重复字段、深层 JSON 和不完整请求。配置和激活任务排队到独立拥有者，UI/HTTP 回调不执行 Flash 写入或长时间联网。

| 接口 | 行为 |
| --- | --- |
| `GET /` | 内置离线页面，无 CDN/外部脚本 |
| `GET /api/state` | 状态、revision、窗口时间、保存标记、短期绑定码与当前页面会话 nonce；不返回已保存密码/token |
| `GET /api/scan` | 最多 20 个网络；SSID 按文字显示 |
| `POST /api/wifi` | `expectedRevision/ssid/password`；取得 IP 后保存 |
| `POST /api/services` | `expectedRevision/nexus`；仅更新 NEXUS 分组 |
| `POST /api/activate` | `expectedRevision`；连接官方平台并保存完整 WSS 分组 |
| `POST /api/finish` | `expectedRevision`；保留凭据并重启返回主页 |

错误会显示失败状态，允许重试；处理中禁用重复提交，退出仍可用。revision 冲突先重新读取状态，不能直接覆盖。`PET_DIAGNOSTICS` 中的 `setup` 只包含状态编号、错误码、revision、剩余时间及保存标记，不包含热点密码、nonce 或家庭凭据。

## 验证记录（2026-11-15）

主机验证覆盖 Wi-Fi-only/分组保留/CAS/旧凭据迁入、HTTP Host/Origin/nonce 策略、SSID/密码边界、绑定码/WSS 响应及忽略供应商固件、首次启动/离线选择/损坏标记/后台重试。真实 LVGL 覆盖网络菜单跳过/选择及设置页布局。浏览器以实际 HTML 和本机模拟 API 检查手机宽度、错误密码重试、特殊 SSID 文本、绑定展示、重复操作与退出；该检查不替代真实 Wi-Fi 或官方平台。

ESP-IDF v6.1 基础维护配置启用热点和启动语音、关闭 DSP/可选扩展/开机音频诊断，应用首次构建 3,187,104 B，SHA256 `444e3e26784d45455d1cf6a84911a6e7f9fbaff3e495eb7e8c86fa9e707be7c2`；配套 bootloader/分区表/OTA 初值/模型与已烧版本相同。仅写应用分区并独立回读校验，不重新初始化 NVS。设备已启动热点，HTTP 绑定 AP 接口；用户手机打开页面并确认 Wi-Fi 已连接保存，USB 读到 revision 1、`wifiSaved=true`、NVS 失败 0。

后续修订、最终应用摘要、官方绑定/真实语音、提交/推送及远程 CI 以 [18](18-requirements-and-progress.md) 的 HW-06/HW-07 记录为准。尚未完成的实机项目不能由主机/浏览器替代。

HW-07 修正后，用户明确确认能够听到真实回答，结束后不再显示“连接失败”；设备记录与该轮一致，完成真实下行音频与 DMA drain。当前维护固件已启用启动语音；普通 `default` 预设仍保留不开机启动 AI 的原有选择，构建其他设备时需按 [10](10-maintenance-recovery.md) 明确启用 `PET_AI_XIAOZHI_ON_BOOT`，不能只保存凭据就声称语音可用。
