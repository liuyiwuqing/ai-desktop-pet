# device_control

2026-11-12：软件执行链已实现，默认关闭，未连接真实 Mac Bridge。适配当前三项能力：

| 能力 | 风险与参数 | 执行要求 |
| --- | --- | --- |
| `system.info` | L0，只读，无参数 | 设备身份与令牌通过 |
| `project.status` | L0，`projectId` 白名单 | 不接受任意路径 |
| `app.open` | L1，`applicationId` 白名单 | 屏幕展示具体动作，新的 BOOT 长按确认 |

`device.discover` 仅用于发现目录。L2 守卫保留在纯命令引擎，不注册破坏性操作；L3 任意 shell/提权不注册。手机平台、媒体控制和其他能力未适配。

`command.h/command.c` 负责严格参数、最多 16 能力、单调请求 ID、去重/冲突、60 秒本地确认、30 次/分钟与 L2 3 次/10 分钟限速、审计。`control_service.h/control_esp.c` 单拥有者队列调用 HTTPS，执行前持久预留水位。结果不明使用 UNKNOWN，重启/超时不重执行，只通过 GET reconcile 查询。云端只得到排队/确认/结果状态，不能生成确认 nonce 或代替本地按键。

固件开启 `CONFIG_PET_EXT_DEVICE_CONTROL`，需要真实网络和 UI。凭据 `control/api_uri`、token、CA 和 apps/projects 白名单从物理配置入口注入。`api_uri` 为网关 HTTPS 根地址。Gateway 部署与权限见 [server](../../server/README.md)；当前路由目标固定 `mac`。

真实 LVGL 确认页显示设备、应用、动作和倒计时；进入时已有按住无效，需先松开再长按。短按取消，过期/屏幕故障/critical 消息取消。`test_host.sh` 运行命令引擎与手势测试，`test_ui_host.sh` 渲染确认页；这不代表真实按键、TLS、Mac 应用启动或结果送达已验收。
