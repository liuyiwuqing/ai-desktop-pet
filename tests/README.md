# tests

跨模块契约与集成测试。

## 分层

- 主机测试：纯逻辑、状态机、解析器、成长公式、CRC、配置 schema。
- 契约测试：MQTT 信封、HTTP Webhook、设备控制意图、Bridge 白名单。
- 集成测试：服务端 + Broker + mock 设备；不替代 HIL。
- HIL：真实屏幕、音频、传感器、断网、掉电、24 h/7 d。

## 规则

- 先写失败反馈，再实现；测试必须能在无硬件环境运行（除 HIL）。
- 不把主机测试通过描述为硬件验收通过。
- 测试不读取真实凭据；使用临时配置与 mock。
- 详细验收项见 [`../docs/07-roadmap-acceptance.md`](../docs/07-roadmap-acceptance.md)。

## 当前执行入口

- `tools/ci/test_host.sh`：展示状态机、v1/v2/v3 快照兼容、原子收件/回执、离线已读、PUBACK 超时、回执积压、并发升级、写入/活动代切换失败、损坏回退与禁止自动擦除。`test_message_service` 运行真实 service/store/codec/storage，只有 OS、NVS 驱动和网络出口为内存边界替身；`save-read`/`restore-read` 分两个进程验证重启恢复，临时镜像随后删除。S5-F 另覆盖完整投递身份、错设备/主题、retained 拒收、满收件箱保留旧消息、过期原因、已确认终态重投与更换设备身份不误发。
- `tools/ci/test_ui_host.sh`：运行真实 LVGL 与消息/成长/AI 视图，检查中文、长文滚动、缺字、最大正文、确认错误、任务与空态布局；角色测试逐帧比较八种画面、动作变化、绘制边界和隐藏停帧，生成 `firmware/build-ui-preview/*.ppm`。需要先通过固件构建取得锁定的 LVGL 组件，主机无需 IDF 编译器。

同一 UI 入口还运行 `test_recovery_ui` 8 个独立场景：真实恢复任务、LVGL、字体和手势策略，只替换 SDK 调度/时钟/内存及注入的显示/按键。覆盖完整双长按/松手重试、初始化/刷屏/最终背光失败、无 BOOT、分配和任务创建失败、重复启动和清理后的拥有者释放。支持用独立 CMake build 目录设置编译/链接 sanitizer flags；实际恢复适配场景通过 ASan/UBSan，未包含 LeakSanitizer 或物理重启验证，见 [14](../docs/14-recovery-and-device-names.md)。
- `tools/ci/build_firmware.sh`：只构建默认固件；变体使用各自 `SDKCONFIG` 和 `build-*` 目录。
- `bash tools/ci/test_ai_protocol_host.sh`：A5 小智协议与 codec SDK 边界测试，先用 `idf.py -C firmware reconfigure` 获取锁定依赖；`build_firmware.sh` 在固件构建后自动运行。`PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` 启用 ASan/UBSan。
- `bash tools/ci/test_ai_wss_host.sh`：WSS/A4 接线的 34 个线程调度场景，含正式应用工具注册与上下文跨连接读取，要求 `IDF_PATH` 与锁定依赖；构建入口自动运行。可分别设置 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` 或 `PET_HOST_TSAN=1`，不运行真实网络/Opus/I²S。
- `bash tools/ci/test_sr_frontend_host.sh`：A8 使用真实前端代码与锁定 ESP-SR 头文件，推理二进制/分区/密码学为边界替身；支持 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`。包校验 CLI 测试由 `test_host.sh` 执行；两者不能代替真实模型推理或硬件验收。

主机 UI 用内存帧缓冲代替 LCD，不代表 SPI、DMA 时序、GPIO、掉电与物理显示验收。

- `JAVA_HOME="$(/usr/libexec/java_home -v 21)" python3 tools/ci/test_nexus_contract.py`：可选的源码契约回环，读取同级 nexus-core 当前 Java 源码与已有 JAR 依赖；真实 C service/store/codec 与 Java producer/consumer 对接，SQL/网络/Flash 驱动为替身，不代表真实端到端联调。详见 `integration/README.md`。

G1 新增 `test_growth` 与 `test_growth_service`（同由 `test_host.sh` 执行）：前者检查成长规则与长期数值边界，后者运行真实 engine/store/service，只替换时钟、锁、存储 I/O 和事件出口，覆盖提交失败/不确定、损坏/未知版本、离线对时与并发幂等。桌面模拟器用真实文件原子替换补充进程重启检查；两类验证均不代表物理 NVS 掉电验收。

G4 新增 `test_pet_motion`（同由 `test_host.sh` 执行），验证帧间隔、状态优先级、稳定窗口、隐藏/恢复、睡眠切换、瞬态结束、毫秒回绕与长时间姿态范围。UI 入口另运行 `test_pet_avatar`，输出八种状态各 42 帧；`python3 tools/export_pet_preview.py` 可生成自包含 HTML 预览。图元和调度使用真实固件源码；主机帧缓冲验证不代表实屏帧率或动态资源峰值。

G5 扩充真实成长服务测试：一次性成就与金币原子提交，覆盖失败/重试、提交不确定、重复/并发、精确门槛、封顶及同时达成、82 天衰减/换日/重启后不重复发奖。冻结 v1/v2 及旧格式门槛样本验证 v3 兼容；UI 检查成就页面、达成数、布局和停帧。CLI 两进程验证金币、任务和成就共同恢复，不代表实物 NVS 掉电。

G6 扩充 `test_growth_service`：可信质量/门槛/错误码/非有限值，持续环境暴露、同时不适不叠加、子秒累计、重放/未来/乱序、30 s 过期、32 位回绕与长时间断流恢复、存档失败/不确定及重启不外推环境。UI 逐帧检查四种环境彼此不同、画布边界，并比较睡眠/生病主体像素保留；CLI 检查连续切换、断流/失效和两个进程的心情恢复。真实 engine/store/service/environment 通过 ASan/UBSan；macOS 本次使用 `detect_leaks=0`，不包含 LeakSanitizer 验证。

`test_growth_runtime` 单独运行真实固件启动/路由代码，OS、事件与成长 API 为边界替身；允许新任务在创建返回前执行，验证订阅就绪前不结算、初始化/队列/任务/订阅失败清理、错误来源/类型/长度拒收、最新值覆盖、工作任务消费与重复启动。气候开关两种配置均运行，不能据此认定真实 FreeRTOS 多核调度或物理 NVS 已验收。


A1 在 `test_host.sh` 新增 `test_audio_pcm`、`test_audio_buffer`、`test_audio_loopback`、`test_audio_fake` 与 `test_bsp_audio`：真实 C 逻辑与 BSP，只有 I2S 驱动/时钟为边界替身，测试对齐/满幅、衰减、电平、有限 FIFO、半双工、失败/短帧/取消/清理失败与重试、DMA 尾部回调门控、无进度超时、重启播放静音与 DHT 自定义引脚冲突。5 组程序通过 ASan/UBSan（macOS `detect_leaks=0`）；`test_board_codegen.py` 验证跨外设冲突、端口与 GPIO 范围。此证据不代表物理麦克风/功放、GPIO、DMA 时序、音质或实时内存验收。

A2 在同一入口新增 `test_ai_session` 与 `test_voice_simulator.py`：真实会话引擎和 PCM 逻辑覆盖 300 ms 端点/短噪声、15 s 上限、按住说话、打断、静音、错误/恢复、旧轮次/重复帧/时钟、断流和响应超时；CLI 验证组合流程与非法参数退出码。会话 C 测试通过 ASan/UBSan（macOS `detect_leaks=0`）。检测、响应和 DMA 完成为显式注入，不能作为 WakeNet、模型 VAD、云端、音频执行或实机证据。

A3 新增 `test_ai_runtime`：真实会话引擎通过同步 sink 与前端端口替身执行，覆盖空闲唤醒、VAD 帧、清理/启动/转发/结束顺序、响应播放、旧轮次、乱序 PCM 预检、采集启动/传输/VAD 失败回送与一次性清理、清理失败继续执行及阻止新轮次启动。该测试通过 ASan/UBSan（macOS `detect_leaks=0`）；不替代真实 WakeNet/VAD、I²S/FreeRTOS 任务、WSS 或实机验收。

A4 新增 `test_ai_agent`：主机 pthread 运行真实拥有者服务、runtime、session 与 PCM，FreeRTOS API/HAL/传输为边界替身。25 个独立场景覆盖创建前运行门控、启动失败释放/重试、半双工、短读短写与同毫秒 PCM 暂存、最后一帧、队满与并发、旧轮次、打断/静音、读写/drain/清理失败、空/断流响应及超时。全场景通过 ASan/UBSan（`detect_leaks=0`），并有针对性 ThreadSanitizer 检查；主机调度不代表 ESP32 SMP、模型、物理 I²S/音质、栈水位或云端验收。

A5 新增 `test_xiaozhi` 与 `test_opus_port`：真实协议/媒体逻辑和 cJSON，codec 使用真实 SDK 头文件加外部函数替身；覆盖握手格式、session 安全转义、20/60 ms 聚合/拆分、末包补齐、固定包长、播放 `peek/commit` 与提前结束、取消清空、旧连接、逐字节截断及 JSON/TOC 边界，另检查 SDK 配置、完整消费/输出格式、部分分配失败清理及关闭失败锁定。普通和 ASan/UBSan 检查通过（`detect_leaks=0`）；目标强制链接核对真实 vendor 函数，不表示实际执行 Opus、WSS/TLS、云端对话或实机验收。

A6 新增 `test_xiaozhi_wss`：通过公开 WSS/A4/启动 API，验证异步初始化、严格 NVS 读取与显式录音触发、IP/SNTP、复制所有权、SDK 分块和 RFC continuation、每轮新连接、队满取消、新轮首帧、真实 A4 队列背压/独立 drain、完整响应后关闭、播放停滞、解码/销毁失败及失败通知竞争/重试。SDK/FreeRTOS/I²S 使用边界替身；退出的 pthread 对应 FreeRTOS 自删除任务，不保留待 join 资源。实际 TLS、codec 算法、设备 SMP、音质与云端验收仍未覆盖。

A7 扩充 `test_ai_agent`：优先静音/取消、PTT 绑定松开、独立 DND/静音、critical 门控、初始 DND、采集中途隐私到达丢帧、播放中断/清理失败、并发请求与按字段策略更新。`test_growth_input` 覆盖同一 BOOT 菜单中的语音/成长导航、保持按住与抢占后的松开保护；`test_message_presenter` 验证活动语音延后普通弹窗而 critical 抢占。LVGL 入口输出 `ai-*.ppm`，验证五类状态、电平归零、初始化/静音待处理与故障文案及布局；WSS 入口补充真实 A4 隐私取消和新轮 PTT 场景。均不替代 UI 全任务、实际按键/屏幕、真实音频停止延迟或云端验收。

A8 扩充 `test_ai_agent` 至 42 场景，覆盖前端 open/reset/close、部分失败回滚、推理期间隐私/取消到达与检测失败锁定回收；LVGL 增加空闲唤醒监听状态。`test_sr_frontend` 检查真实窗口拼接、VAD 标签保持、模式/断流清理、时间与格式边界、模型包不符和资源回收；`test_sr_models.py` 检查合法包、越界/重叠、缺失/额外模型及失败不替换旧头。目标构建另验证真实模型包和模型 API 链接，识别率、设备密码学、时延与堆/栈水位仍待实机。

## 连续软件交付回归（2026-11-12）

设备首版范围见 [09](../docs/09-software-delivery.md)，跨项目管理闭环见 [11](../docs/11-product-software-closure.md)，当前运行维护与构建证据见 [13](../docs/13-operational-software.md)。新增配置 CAS/远程重投、预缓冲/欠载/提示音、上下文/MCP、对话/专注奖励、受保护备份恢复、分钟均值图表、设备白名单/确认/审计、舵机限位/序列/急停，以及签名 OTA/事务/恢复计划检查。

`test_host.sh` 的 C 目标支持 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`，配置与 OTA 子脚本同样支持。协议/WSS/SR 独立脚本支持该选项。使用真实 OpenSSL 验证签名，不以替身证明密码算法；模型/codec/Flash/HTTP/电机硬件等具体边界仍按各测试说明替换。

新增 LVGL 温湿度图表、消息评论与控制确认帧已纳入 UI 入口，生成到 build-ui-preview。实际 LCD、GPIO、音频、云端/Bridge、物理存储及回滚均需独立验收。

跨项目收口另增加以下入口：

- `python3 tools/ci/test_management_contract.py`：真实 Java HTTP/PostgreSQL/MQTT 与本仓 C 配置事务互通，C 的 OS/Flash 为边界替身；`PET_HOST_SANITIZE=1` 可插桩 C 端。服务端完整检查在 `nexus-core` 执行 `python3 tools/run_local.py test`。
- WSS `app-tools`：正式注册表发现、完成播放后的上下文读取、非法参数、清除期间禁止发送、重新连接后空历史与 MCP 诊断计数；普通、ASan/UBSan 与 TSan 已通过，具体边界见 [12](../docs/12-cloud-interop.md)。
- `python3 tools/ci/test_deployment.py`：Docker/Caddy 2.10.2 的两份 TLS 模板校验、隔离 HTTP 路由/鉴权转发/体积限制/POST 不重试；不申请证书或安装服务。
- `bash tools/ci/test_management_ui.sh`：真实 Vue + ego-browser 的 38 项回归，含记忆检索的加载/空/失败/部分核验/关闭/纯文本与链接，以及设备更名的草稿/冲突/慢回读/结果未知核对；使用仅在回环地址运行的 HTTP 状态夹具。此入口不证明 Java/数据库/MQTT 的行为，跨项目验证使用上方 management contract。
- `python3 tools/ci/test_build_matrix.py`、`python3 tools/ci/test_crash_dump.py` 与 `test_recovery`：命令计划默认不写/不连接设备、非法输入拒绝、恢复阈值和健康窗口。实际 raw dump 采集与成功解码仍需配套故障设备数据。
- CORE `RetentionFlowTest`：隔离 PostgreSQL + HTTP 验证默认关闭、7/30 天清理、失败回滚/占用、在途结果防回写与 Halo 窗口；外部模型/Halo 为 HTTP 夹具，不访问真实服务。
- CORE `DeviceManagementFlowTest` 新增更名公开 HTTP 流程：鉴权、校验、幂等重试、旧名称冲突和两个并发请求只有一个成功；验证用隔离 PostgreSQL/MQTT，不修改真实设备登记。

CORE `MemoryLifecycleFlowTest` 和 Inbox 公开流程验证显式保存、受限检索、改文/非公开失效、持久删除重试、本地清理后的来源核验及在途写入补偿。使用隔离 PostgreSQL 与外部 HTTP 夹具；不据此声称真实 OpenViking 删除、真实迁移或语音会话接入。当前范围和结果见 [15](../docs/15-memory-lifecycle.md)。

公开记忆 MCP 另覆盖默认关闭、独立鉴权、管理员 API 隔离、严格 JSON/参数/UTF-8、来源变化与故障结果，以及真实 stdio 子进程的发现/检索。`MemoryToolConfigurationTest` 检查启用时缺失/复用/非法凭据导致启动失败。2026-11-12 标准 CORE 构建为 43 项、41 通过、2 条件跳过；stdio 另有 6 项隔离进程检查。接口和真实联调边界见 [16](../docs/16-voice-memory-tools.md)。

2026-11-13 新增桥接准备与本地 TLS 回归，见 [17](../docs/17-voice-bridge-regression.md)。`test_memory_voice_prepare.py` 5 项、`test_memory_voice_bridge.py` 5 项、原 stdio 6 项均通过。CORE 使用 `NEXUS_VOICE_BRIDGE_TEST=true python3 tools/run_local.py test`，44 项中 42 通过、2 条件跳过；桥接项确实启动实际脚本、stdio 和 Java/PostgreSQL，验证公开内容与转非公开后的结果以及重新连接。供应商 WSS、Halo、模型与 OpenViking 仍为隔离边界夹具。桥接检查还覆盖不可信 TLS、进程组退出、异常 stdio 恢复、不重放与环境/日志隔离；未重复前端、浏览器或固件构建，前轮证据保留。
