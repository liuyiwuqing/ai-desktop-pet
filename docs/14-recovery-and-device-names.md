# 屏幕恢复与设备名称

2026-11-12，继续补齐 [13 运行维护软件](13-operational-software.md)留下的屏幕/BOOT 恢复入口，并实现管理台设备重命名。本文为这两项的当前交付记录；版本仍为固件 `0.4.0-dev`、CORE `0.1.0-SNAPSHOT`，没有提交、推送、部署或烧录。

## 恢复启动

`CONFIG_PET_RECOVERY_ENABLED` 仍需物理维护控制台；四套构建中 full 启用此功能。连续三次未达到健康窗口的 panic/watchdog 等重启后，先启动配置/串口维护，再隔离网络、语音、传感器、成长和扩展。

UI 开启时，恢复模式额外只启动 LCD 和 BOOT。240×320 屏幕显示恢复状态、暂停的能力、USB 诊断入口及当前按键步骤，不清空存档：

1. 开机若已按住 BOOT，必须先松开，旧的按住状态不能作为确认。
2. 新增长按 3 秒，松开后进入确认。
3. 10 秒内再次长按 3 秒；短按、确认超时或时钟异常会取消确认。
4. 最后松开 BOOT，并确认最终提示已成功刷屏后，清除恢复锁并重启尝试正常启动。按住 BOOT 时不会调用重启，避免 GPIO0 在重启时仍处于下载电平。

按键使用 40 ms 去抖。BOOT 初始化失败时仍显示 USB 维护入口，不响应重试动作。显示、背光、内存或任务创建失败时停止屏幕路径，保留先启动的串口/配置维护；最终提示的刷新失败同样禁止重启。

屏幕尝试位在初始化 LCD 前写入带校验的 RTC 记录。同一恢复锁只允许一次屏幕启动；若恢复屏幕再次引发崩溃，下一次启动直接进入 USB 维护，避免显示驱动形成另一轮死循环。显式 `PET_RECOVERY_CLEAR` 或屏幕确认重试会清除下次启动的保护，当前维护任务不会因此恢复业务服务。冷上电会清除 RTC 历史，不宣称掉电持久保护。

诊断 `recovery.displayAttempted` 只表示尝试位已消耗，不表示 LCD 已显示成功。正常界面与恢复界面共用 LVGL 拥有者门控。恢复模式不确认 OTA 候选固件健康，原有回滚逻辑保持有效。

## 设备重命名

设备卡片“修改名称”使用现有管理员 Bearer 鉴权，接口为：

```http
PATCH /api/v1/devices/{deviceId}/name
Content-Type: application/json

{"name":"书房伙伴","expectedName":"原名称"}
```

服务端去除名称首尾空白，限制 80 字符，拒绝空白和控制字符。事务内锁定设备并核对原名称；冲突返回 `409 device_name_conflict`，不存在返回 `404 device_not_found`。当前名称已经等于目标时返回成功，支持响应丢失后的相同目标重试。响应包含 `deviceId/name`。

只更新管理台显示名称，不改变设备身份、启停、Broker 账号、配置版本或投递关系；不新增数据库迁移。两个并发编辑使用同一原名称时只能有一个不同目标成功。

界面在轮询刷新时保留草稿。名称已由其他操作更新时，显示冲突并要求“加载当前名称”；保存期间禁用重复操作。HTTP 中断保留草稿并提示结果未确认，通过刷新核对，页面不会自动重发更名。保存/取消后恢复键盘焦点。

## 验证与产物

| 检查 | 实际结果 | 边界 |
| --- | --- | --- |
| `PET_HOST_SANITIZE=1 ... tools/ci/test_host.sh` | 通过 | 含 RTC 恢复引擎和双长按策略，C 目标启用 ASan/UBSan |
| `tools/ci/test_ui_host.sh` | 普通及 `PET_HOST_SANITIZE=1` 均 10/10 通过 | 2 组既有渲染/动画 + 8 个真实恢复适配场景；SDK/显示/按键为边界替身 |
| 恢复页视觉 | idle、confirm、BOOT unavailable 三帧已查看 | 真实 LVGL/内存帧缓冲；不替代物理 LCD |
| 四套固件预设 | default/full/off/tones 全部构建成功 | ESP-IDF 6.1，逐项校验预设与 6 个配套产物，full 编译恢复屏幕 |
| CORE `mvn ... verify` | 29 项，27 通过、2 条件跳过，0 失败/错误 | 隔离 PostgreSQL/MQTT；含名称冲突、重复及并发单赢家 |
| Vue/浏览器 | 类型检查、生产构建与 29 项固定回归通过 | 真实 Vue/DOM + HTTP 夹具；含慢回读、草稿、冲突和结果未知 |
| 管理台视觉 | 桌面与 390×844 手机画面已查看，无横向溢出 | 仅此页面/状态；不是完整移动端验收 |
| 打包 | 最终 JAR 打包成功，3 个前端文件逐字节与当前构建一致 | 打包 `-DskipTests`，测试结果沿用上方完整验证 |
| 静态检查 | 契约/codegen/板级一致、19 组件无环、Python/Bash 语法、Markdown 本地链接、`git diff --check` 通过 | 无真实凭据或外部状态变更 |

UI 入口此次补齐 sanitizer flags 和独立输出目录；macOS Bash 3.2 的空数组 `nounset` 分支也已复现并修正，普通与插桩入口重新通过。`ASAN_OPTIONS=detect_leaks=0`，不声称 LeakSanitizer 验证。

构建根仍为 `firmware/build-matrix-20261112`，新证据的 `sourceTreeSha256` 为 `19d1452eebda40d867e0516f6d8b8ad98161612717c5cb92077642092b4cc63b`。实际输出与各目录 `build-evidence.json` 中的 6 个文件大小/hash 全部一致：

| 预设 | 应用字节数 | `nexus_pet.bin` SHA256 |
| --- | ---: | --- |
| default | 2,884,960 | `2c722b419edce2cab5c3fdd3f6b8fca7b249794ff564519d185cbe0e8f6f0474` |
| full | 3,612,624 | `6e8498e397c54e9853f3206d96e62b8c7e9db8b31f18f332036f7a1009f7d210` |
| off | 225,760 | `0edf81549c38d209d879295393ef33606876287a15c1b84e0e66e7957b984305` |
| tones | 2,925,680 | `35d3f994962ad259f75e0d04e3aa1dcdc74bee3315308b69b313b436ca346f2c` |

full 使用编译测试公钥和 `.invalid` 发布地址；不是生产发布。此前 [13](13-operational-software.md)的独立重建只验证旧版本的 6 个配套产物，本轮新镜像没有再次执行独立重建，不将旧结论套用到新 hash。

最终 CORE JAR 为 26,380,252 B，SHA256 `cbe646779cc71dae6208eb133ba36be5804b6ba1cd196c1d0accfcf2c19f6782`。主机检查覆盖软件行为，目标构建覆盖链接及配置组合；均不能替代真实 BOOT、LCD、RTC 重启、panic/watchdog 或 OTA 回滚演练。

本轮未扩展数据保留、外发范围或奖励合并规则。后续长期记忆检索与副本失效已在 [15](15-memory-lifecycle.md)补齐；跨端成长合并、凭据轮换、远程 OTA 管理、资源/语言和更多平台仍待推进。
