# tools

构建、契约生成、资源打包、迁移与诊断脚本。

## 已落地

- `ci/test_growth_sync_contract.py`：实际成长引擎/JSON → MQTT/PostgreSQL/CORE HTTP；`ci/test_growth_sync_ui.sh`：真实 Vue/浏览器的只读成长状态与移动宽度回归。边界见 [21](../docs/21-growth-sync.md)。
- `server_release.py create/verify/preflight/probe`：生成 CORE 源码/JAR/部署模板离线包、SHA256 完整性校验、私有配置/目标主机预检和只读鉴权状态探测；不安装、启动或迁移数据库。`ci/test_server_release.py` 验证 CLI 边界；`ci/test_server_linux.py` 使用包内实际 JAR 验证 Linux 启动/重启/资源，`--auth-rotation` 验证管理员私有文件重载，`--memory-rotation` 再覆盖 Memory MCP/同一 stdio 进程的轮换和异常恢复（上游为空 HTTP 夹具）。见 [交付说明](../deployment/server-release.md)。
- `schema_codegen.py`：读取 `config/schemas/`，生成 `firmware/.../config_generated.h` 与 `integration/contracts/generated/pet-contracts.ts`；`--check` 用于 CI 检查过期。
- `ci/build_firmware.sh`：依赖图检查 → 契约校验 → 主机测试 → `idf.py build` → `idf.py size` → 小智协议/codec、WSS 与模型前端边界主机测试。
- `ci/check_deps.py`：组件依赖图无环。
- `ci/check_contracts.py`：schema、profiles、examples、扩展 manifest、生成产物一致性。
- `ci/test_host.sh`：可选摇杆/光敏的真实 BSP GPIO/ADC 适配器（独立/组合/关闭/反相）、回中/菜单导航/按住说话与调光策略；契约、温湿度、消息解析/队列/存储、展示状态机、成长规则/事务/时钟、动画调度、音频 PCM/FIFO/录放音、显式并行诊断/隐私取消与 I²S 边界故障、AI 会话/CLI 及扩展的主机测试。
- `simulate_growth.sh`：用主机 C 编译器运行同一套成长服务；可互动、推进时间、查看成就、注入环境（`climate 温度 湿度`）、模拟失效/断流（`climate-off` / `climate-loss`）、持久保存和重启恢复，不需要 ESP-IDF 或硬件。用法见 `docs/04-growth-game.md` G1/G3/G5/G6。
- `simulate_voice.sh`：用主机 C 编译器运行同一套 AI 会话引擎；注入唤醒、合成 PCM、响应/播放完成与错误，演练端点、手动打断、静音及旧轮次。`wait` 不生成静音，输出 actions 仅为控制意图，无实际录音、网络或播放。用法见 `docs/01-ai-agent.md` A2；`ci/test_voice_simulator.py` 已纳入主机测试。
- `ci/test_host.sh` 中的 `test_ai_runtime`：运行单一拥有者边界测试，验证 sink 动作顺序、WakeNet/VAD 注入、旧帧预检与音频/传输端口失败回送；替身不代表真实模型、FreeRTOS 或 I²S。
- 同一入口的 `test_ai_agent`：主机线程场景运行实际服务代码，验证队列、HAL 消费、同毫秒帧暂存、半双工、打断/静音、drain、并发队满、模型生命周期/失败锁定与推理期间隐私门控；OS/HAL/传输为边界替身，不代表实机或云端。
- `ci/test_ai_protocol_host.sh`：使用 04 锁定的 cJSON 源码和 codec SDK 头文件，验证小智 v1 握手/控制、帧转换、背压、取消与旧连接、畸形 JSON/TOC 和 SDK 失败；codec 二进制函数为边界替身。先 `idf.py -C firmware reconfigure` 解析依赖，可用 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` 开启 ASan/UBSan，不依赖 02/03、不连接云端。
- `ci/test_ai_wss_host.sh`：真实 WSS/A5/A4 逻辑与 pthread 调度，SDK WebSocket、codec 二进制、网络和 I²S 为边界替身。需要 `IDF_PATH` 与已解析的锁定依赖；验证 NVS 启动、分片、背压、打断、故障通知与资源锁定。可分别用 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`、`PET_HOST_TSAN=1` 运行内存/线程检查；已接入 `build_firmware.sh`，不连接云端。
- `check_sr_models.py`：验证固定 WakeNet/VAD 模型包、1 MiB 分区与 960 KiB 镜像上限，生成配套 SHA256 头；由启用 ESP-SR 的构建调用。`ci/test_sr_models.py` 的 CLI 边界测试已纳入 `test_host.sh`。
- `ci/test_sr_frontend_host.sh`：真实前端适配代码与锁定 ESP-SR API 头，验证窗口拼接、模型状态清理和部分初始化失败回收；推理/分区/密码学为边界替身。支持 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0`；构建入口自动运行，不执行真实模型推理。
- `board_codegen.py`：由 `firmware/boards/dodo-s3-lcd/board.json` 生成 BSP 头和 Kconfig 默认引脚；`--check` 检查漂移。
- `generate_fonts.py`：用固定 `lv_font_conv@1.5.3` 从 Noto Sans SC 生成 16/24 px 字体（仅重新生成需要 Node/npm），产物与哈希入库。
- `ci/test_ui_host.sh`：使用固件锁定的 LVGL 与字体运行消息/成长视图、八种动画与恢复 UI 的主机检查；含真实恢复适配任务的 8 个失败/确认场景，产出 PPM 供布局检查。
- `export_pet_preview.py`：运行 UI 测试后执行 `python3 tools/export_pet_preview.py`，将八种状态和四种环境的真实 LVGL 帧导出为自包含 `firmware/build-ui-preview/pet-animation.html`，可切换状态/暂停；只依赖 Python 标准库，预览为 10 fps 抽样。
- `build_matrix.py`：default/full/off/tones/duplex 隔离预设，默认只输出计划；`--execute` 使用 ESP-IDF 6.1 构建并记录配置、源码/产物哈希与依赖许可证。full 需显式提供 OTA 公钥，duplex 是显式启动的有限音频诊断，测试镜像不自动发布；见 [运行维护记录](../docs/13-operational-software.md)及[音频诊断](../docs/22-audio-duplex-diagnostic.md)。
- `crash_dump.py`：默认只输出读取/离线解码计划，`--execute` 才执行；ROM 读取固定崩溃分区且不自动复位，解码须绑定 ELF SHA256。敏感输出排他创建为 600，不自动上传。
- `ci/test_build_matrix.py`、`ci/test_crash_dump.py`：公开 CLI 的只读计划、隔离路径与非法输入拒绝；已纳入 `test_host.sh`。恢复状态机同入口覆盖早期崩溃阈值、健康窗口、RTC 损坏、冷上电及手工清锁。
- `ci/test_management_ui.sh`：构建真实 Vue 管理台，通过 ego-browser 运行 38 项固定浏览器回归。loopback HTTP 夹具仅模拟边界状态，不使用 CORE、数据库或 Broker；覆盖草稿/冲突/未知结果同 ID 重试、只读/过期禁用、清理提示与窄屏溢出。服务退出时自动停止，页面在成功后关闭，失败时保留以便诊断。
- `ci/test_message_compose_ui.sh`：真实 Vue 的测试发送专项 33 项；注入保存后/保存前断线、明确拒绝、503 和无效确认，检查同 ID/正文重试、导航恢复、退出清理与 390px 布局。使用同一回环 HTTP 夹具的独立端口，不代表真实消息投递；见 [管理台验收记录](../docs/23-console-completion.md)。
- `ci/test_list_filters_ui.sh`：真实 Vue 的消息已读/未读、投递七状态筛选专项 63 项；验证共享计数/分页、导航/刷新保持、两类末页操作、加载/失败/空结果与迟到响应。回环 HTTP 仅用内存夹具，独立端口 18805，不代表真实 CORE 或设备状态流转。

## 计划内容

- 更多板型与语言预设；当前有四种常规功能组合和一种维护用并行音频诊断，UI 仍仅支持中文。
- 资源打包：字体子集、图片转 RGB565、动画精灵表、LittleFS 镜像。
- 旧工程迁移：02 格式核对与受控映射，当前仅提供只读方案；实物分区备份与回读待上板阶段。
- 更多硬件诊断：I2C/SPI 扫描与实物读数；当前快照、崩溃读取/离线解码工具已实现，实际崩溃采集待上板。

## 规则

- 默认不烧录、不擦除 Flash；危险操作必须显式参数与二次确认。
- 不在脚本中硬编码令牌、Wi-Fi 密码、设备证书。
- 生成物放 `build/` 或临时目录，不入库；契约、板级生成物与经许可的字体资源例外，必须入库并校验一致性。
- 契约与板级生成脚本只依赖 Python 标准库；遇到不支持的 JSON Schema 关键字必须失败，不能静默忽略。

## NEXUS v1 契约回环

`ci/test_nexus_contract.py` 使用 JDK 21、主机 C 编译器和同级 nexus-core 已有 JAR 的运行库，编译当前 Java producer/consumer 与真实 C 消息服务；校验源码指纹、JSON Schema 和回执状态推进。所有测试产物在 `firmware/build-nexus-contract/`，不写服务端、不连接 Broker/SQL、不烧录。命令及可选路径参数见 [`integration/README.md`](../integration/README.md)。

A7：`test_host.sh` 覆盖 PTT/隐私/抢占与独立策略更新，`test_ui_host.sh` 生成 AI 状态帧，`test_ai_wss_host.sh` 增加隐私取消/新轮 PTT。固件默认仍不启动 AI；所有主机检查均无真实录音或联网。

## 2026-11-12 配置、诊断与发布工具

- `configure_device.py`：get/status/settings/provision/diagnostics/backup/restore/ota-apply/ota-reboot/servo-arm/servo-estop/recovery-clear。默认仅准备；`--send --port` 才发送，禁用自动复位。凭据文件权限 600，输出只转发允许的状态字段，备份文件排他创建。详细参数和恢复约束见 [维护说明](../docs/10-maintenance-recovery.md)与[崩溃恢复](../docs/13-operational-software.md)。
- `package_release.py create/verify`：离线打包与验签；真实 OpenSSL RSA-3072-PSS，校验固件/板型/模型/分区身份与配套 USB 文件，不上传、不烧录。生产钥由维护者提供。
- `build_identity.py`：从板型、模型头和分区表生成不可变应用身份，由构建自动调用。
- `ci/test_config_host.sh`：真实配置 codec/service/provision/remote，覆盖 CAS、重复/非法、DND、失败与锁写。
- `ci/test_ota_host.sh`：真实 manifest/传输事务及 OpenSSL 打包测试；覆盖签名/内容篡改、身份错误、截断、超时、预留/切换失败。
- `ci/test_control_gateway.py`：本地 HTTP/Gateway 与 Bridge 边界替身，覆盖鉴权、重复字段、同设备结果、确认/白名单、重启去重与 UNKNOWN 查询。
- `ci/test_host.sh` 已收录上述检查及 context/focus/command/actuator/确认手势等；`PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` 对 C 主机目标启用 ASan/UBSan。Python 子进程自身的编译按各脚本设置，不据此声称全部子进程均被插桩。

当前 UI 只有中文；远程配置已接通 CORE、管理台和设备 C 服务的隔离回环，见 [产品闭环](../docs/11-product-software-closure.md)。AEC 保持半双工回退。

`ci/test_management_ui.sh` 固定运行 38 项 Vue/ego-browser 回归，新增更名草稿、冲突、慢回读和结果未知后的只读核对。使用 HTTP 边界夹具；实际 CORE 的更名事务/并发在其 Maven 检查中验证。含记忆检索的加载/空/失败/部分核验、纯文本/链接、关闭及窄屏。当前记录见 [15](../docs/15-memory-lifecycle.md)，屏幕恢复与更名见 [14](../docs/14-recovery-and-device-names.md)。

- `ci/test_management_contract.py`：真实 Java HTTP/PostgreSQL/MQTT 与 C 配置服务回环，支持 `PET_HOST_SANITIZE=1`。
- `ci/test_deployment.py`：使用 Docker/Caddy 2.10.2 校验两份 TLS 代理模板，并用独立 HTTP 夹具验证鉴权转发、路由、请求体限制及 POST 不重试；不安装服务或申请证书。
- WSS `app-tools` 场景运行正式工具注册表与已完成对话的跨连接上下文读取，见 [云端联调](../docs/12-cloud-interop.md)。

`python3 tools/ci/test_memory_mcp_stdio.py` 使用同级 `nexus-core/tools/memory_mcp_stdio.py`，以真实子进程和隔离 HTTP 验证初始化/通知/请求 ID、凭据不外泄、禁止重定向和自动重试、异常响应、体积/Unicode/配置边界。只需 Python 标准库，不启动供应商桥接或访问真实实例。CORE `MemoryLifecycleFlowTest` 另运行真实 stdio→CORE→PostgreSQL/外部 HTTP 夹具回环；构建和部署方式见 [16](../docs/16-voice-memory-tools.md)，文件轮换与撤销见 [20](../docs/20-credential-rotation.md)。

`python3 tools/memory_voice.py prepare --directory build/memory-voice` 下载并校验固定桥接与两个依赖 wheel，创建独立 venv；`verify` 离线检查；`python3 tools/memory_voice.py run --directory build/memory-voice --stdio ../nexus-core/tools/memory_mcp_stdio.py` 使用受控环境启动。工具不读取私有 env 文件，凭据由环境 token 或 `NEXUS_MEMORY_MCP_TOKEN_FILE` 绝对路径注入；只有 `run` 才建立配置的 WSS。`ci/test_memory_voice_prepare.py` 验证准备/启动边界；准备后用 `build/memory-voice/.venv/bin/python tools/ci/test_memory_voice_bridge.py` 运行实际桥接+stdio+临时 TLS 端点回归。真实 CORE 全链测试需额外启用，命令与边界见 [17](../docs/17-voice-bridge-regression.md)。

远程 OTA 检查：`PET_HOST_SANITIZE=1 bash tools/ci/test_ota_remote_host.sh`、`PET_HOST_SANITIZE=1 python3 tools/ci/test_remote_ota_contract.py`、`bash tools/ci/test_remote_ota_ui.sh`。分别覆盖实际 C 事务、真实 CORE/数据库/Broker 与 C 回环、真实 Vue/浏览器 + HTTP 边界夹具；均不烧录、不执行真实升级，见 [19](../docs/19-remote-ota.md)。

CORE `tools/credential_store.py` 提供受保护文件的 init/status/rotate/revoke/export-memory；04 发布包 format=2 纳入此 CLI，预检/probe 支持文件模式，verifier 保留 format=1 兼容。秘密仅在权限文件中流转，不进入 CLI 参数/输出。详见 [20](../docs/20-credential-rotation.md)。

`observe_report.py`：读服务端观测窗口 ndjson（`~/nexus-observe/observe-*.ndjson`，可选 `--device` 读设备侧日志），输出覆盖/断档/重启/通道/资源/5xx/证书与逐项判定；把「服务不健康」与「采集通道 401」分开，避免把采集器故障当成停机。退出码 0 通过、2 待复核、1 失败。`ci/test_observe_report.py` 覆盖全绿窗口、401 通道、断档+重启+未闭环、非法日志与设备离线段；用法与结论见 [ACC-01 §10.3.3](../docs/29-acc01-production-deployment-plan.md)。
