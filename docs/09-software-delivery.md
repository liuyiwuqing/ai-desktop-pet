# 连续软件交付记录

2026-11-12：按用户授权连续推进软件部分。以下记录 `04` 首版交付范围；后续跨项目软件闭环见 [11](11-product-software-closure.md)。主机、目标构建和真实设备证据分别记录。维护操作见 [10 软件维护与恢复](10-maintenance-recovery.md)。

| 顺序 | 已实现范围 | 主要验证入口 |
| --- | --- | --- |
| 1 | 配置校验、NVS 原子包/回读、revision CAS、音量/亮度/静音/定时 DND、物理配置入口；凭据独立完整包 | `test_config_host.sh` |
| 2 | 100 ms 预缓冲、短响应提前开始、欠载补静音/计数/超时、音量与 10 ms 尾部淡出，隐私立即停止 | A4、WSS 测试 |
| 3 | 转写/回复文本、实际播放电平、说话口型与思考点头、旧轮次与隐私清理 | 协议、A4、LVGL 测试 |
| 4 | 有界 8 轮 RAM 上下文、裁剪与提取式摘要、人设/宠物状态 MCP 查询、本地清除 | 协议、上下文、A4 测试 |
| 5 | MCP 工具发现/调用、严格参数、请求去重与结果回送；温湿度/未读/成长/专注路由 | 协议与意图测试 |
| 6 | 完成对话奖励、25 分钟专注/暂停/继续/取消、持久去重与每日上限 | 成长服务与专注测试 |
| 7 | 温湿度数值、60 分钟均值趋势、告警/故障/过期显示及重试入口 | 采样管线、LVGL 测试 |
| 8 | 按类型展示消息、评论引用样式、提示音、普通延后/critical 抢占、静音/DND 仲裁；可独立启动提醒音拥有者 | 消息呈现、A4、LVGL 测试 |
| 9 | 物理诊断汇总堆、连续块、栈余量、推理/显示耗时、欠载、NVS 与故障；独立遥测主题 | 目标构建、Schema 检查 |
| 10 | 同设备成长导出/恢复、CRC/版本/CAS、防回档重复领奖；02 只读迁移方案 | 成长恢复/失败测试；维护文档 |
| 11 | 设备侧远程配置 CAS/时效/拒绝 retained、确认回复和 30 秒状态摘要；独立版本化契约 | 配置事务、网络主题与 Schema 测试 |
| 12 | ESP-SR NS/AEC 算法适配、参考帧时效/消耗门控、资源清理与半双工回退 | SR/DSP 测试、完整功能构建 |
| 13 | 设备白名单/本地确认/审计/未知结果查询、Mac Bridge 薄网关；舵机限位/速度/序列/急停/LEDC、持久去重与物理 arm | command/actuator/确认手势/Gateway 测试、完整功能构建 |
| 14 | 固件身份、签名 manifest、hash/板型/分区/模型/版本/序号检查、非活动槽 OTA、健康确认/回滚入口、离线签名打包和 USB 恢复清单 | OTA 事务、真实 OpenSSL 签名测试、完整功能构建 |

## 交付边界

这些条目的设备端与本仓软件已实现，以下范围仍须在部署或实机阶段验收：

- 首版交付时 NEXUS v1 消息/回执/在线状态保持兼容，新增配置与遥测当时只完成设备侧契约。后续已补齐 `nexus-core`、管理台与 ACL 升级工具并通过隔离回环，见 [11](11-product-software-closure.md) 和 [device-management-v1](../integration/contracts/device-management-v1/README.md)；真实部署仍待验收。
- 首版继续复用小智。云端是否调用上下文工具、保留多久及历史删除能力需要联调；本地清除只清本机 RAM，摘要不是云端 LLM 生成。当前 UI 仅支持 `zh-CN`，`en-US` 申请明确拒绝。
- 音频主链路保持半双工。当前 BSP 没有同步播放参考采集，AEC 自动回退；算法接线不等于声学消回声或说话时语音打断已验收。
- 扩展默认关闭。当前电脑适配接 Mac Bridge 三项白名单能力；手机平台及更多能力需要独立适配。舵机默认 GPIO=-1，启动无 PWM，须校准后显式物理 arm。
- OTA 默认关闭；完整功能构建使用临时测试公钥和 `.invalid` 地址，只证明编译接线。生产公钥、HTTPS 发布端、首次 USB 配套部署、真实掉电与回滚未执行。
- 未烧录 `04`，未连接真实 Broker/云端/Bridge，未执行设备或机械动作；I²S/SPI/模型识别、NVS 掉电和 24 h/7 d 稳定性不在本次证据内。

## 可重复验证

在根目录执行，先解析 ESP-IDF 6.1 锁定依赖：

```bash
python3 tools/ci/check_contracts.py
python3 tools/board_codegen.py --check
python3 tools/ci/check_deps.py
bash tools/ci/test_host.sh
bash tools/ci/test_ui_host.sh
bash tools/ci/test_ai_protocol_host.sh
bash tools/ci/test_ai_wss_host.sh
bash tools/ci/test_sr_frontend_host.sh
python3 tools/ci/test_nexus_contract.py
```

协议/WSS/SR 使用真实适配源码与锁定 SDK 头；OS、网络、存储驱动和算法二进制按测试范围替换。OTA 打包测试实际调用 OpenSSL RSA-3072-PSS 签名/验证，镜像输入为构造样本，不等于设备升级。

配置、OTA、协议、WSS 和 SR 还支持 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`。真实 LVGL 产出消息、温湿度、设备确认与动画帧，内存帧缓冲不代表 LCD/DMA 性能。

补充发布检查：默认目标编译镜像已实际离线打包并验签；完整 OTA 镜像使用不同签名钥会被打包器拒绝。临时私钥/发布包仅在临时目录用于验证，未发布。

## 本次验证结果

2026-11-12 已核对以下检查退出状态为 `0`：

- 契约生成一致性、8 个 Schema、6 个示例、板级生成文件及 19 个组件无循环依赖检查。
- 完整主机回归及 ASan/UBSan 检查，覆盖配置事务、成长恢复、消息限额、提醒音、控制守卫、舵机序列和 Gateway。末次文本隐私门控修改后，额外重跑 A4 全部既有场景及 WSS 33 个场景，通过 ASan/UBSan；清除请求到拥有者处理之间的旧文本回写已被回归测试拒绝。
- 协议、上下文、意图、ESP-SR/DSP 适配、OTA 事务及真实 OpenSSL 签名/验签测试；真实 LVGL 的 `message_view`、`pet_avatar` 两组测试。
- 使用 JDK 21 的 NEXUS 当前 Java 源码消息/回执契约往返测试；维护 CLI 的默认准备模式、Python 语法及文档链接检查。

ESP-IDF 6.1 最终顺序构建均成功，默认构建继续使用 `firmware/sdkconfig`，其余使用各自构建目录的隔离配置：

| 配置 | 构建目录 | app 镜像字节数 |
| --- | --- | ---: |
| 当前默认配置 | `firmware/build` | 2,883,328 |
| 完整功能编译配置：ESP-SR/DSP、维护、两项扩展、OTA | `firmware/build-a8-sr` | 3,590,384 |
| 业务模块关闭，保留物理维护入口 | `firmware/build-software-off` | 225,200 |
| 独立消息提醒音 | `firmware/build-software-tones` | 2,923,568 |

完整功能配置中的临时公钥和 `.invalid` 地址只用于编译验证；这些镜像未烧录，也不是已部署的生产发布。
