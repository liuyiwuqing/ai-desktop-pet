# 运行维护软件补齐

2026-11-12，在 [11 产品软件闭环](11-product-software-closure.md)之后推进消息保留、构建预设/CI、崩溃诊断/恢复与浏览器回归。本页为该轮交付快照；后续屏幕/BOOT 恢复和设备更名及最新产物见 [14](14-recovery-and-device-names.md)。本页构建哈希和独立重建记录仅属于屏幕恢复实现前的时点。

## 消息保留

CORE Flyway V9 新增正文清理时间与只记录计数的审计。策略默认关闭，`NEXUS_RETENTION_ENABLED=true` 才允许执行和定时清理；本次只在独立 PostgreSQL 测试容器执行迁移，未升级现有实例。

启用前先备份数据库，用原管理员令牌读取 `GET /api/v1/retention` 核对预览及数量，再决定启用。预览始终只读；执行为 `POST /api/v1/retention/runs`，请求体必须是 `{"confirm":"DELETE_EXPIRED_MESSAGES"}`。默认定时为 UTC 04:00，`NEXUS_RETENTION_CRON=-` 可关闭定时、保留手工受控执行。

- 按 CORE 的 `received_at` 起算，7 天清除标题、正文、摘要、发送者、原文链接、附加 metadata 以及分析/回复草稿/本地记忆导出记录；投递结束，未完成自动化任务取消。
- 30 天删除消息、投递及对应消息事件/自动化运行元数据；不清理其他领域的规则、专注或成长记录。清理审计保留 30 天。
- 一次事务最多删除 200 条元数据、再清除 200 条正文；积压需要多次执行并核对预览。事务内任一步失败整批回滚；并行运行返回 `retention_busy`，不得把 HTTP 失败视作已清理。
- 启用后，超期内容立即拒绝新分析、重试和记忆导出，即使还没轮到下一次清理。后台领取同样检查期限；在途分析的旧 lease 不能把已清理内容写回。
- Halo 自动轮询限定到最近 30 天的公开批准时间，防止元数据删除后反复导入旧评论。明确指定的单条补录仍可执行，使用新的接收时间开始保留周期。
- 已领取的在途外部任务不能由清理撤销，请求仍可能完成；清理使其本地旧 lease 失效。已外发给模型或已存入外部 OpenViking 的副本不由本地清理撤回或删除，本功能不声称删除供应商历史。

管理台对已清理消息显示原因和清理时间，隐藏分析入口。保留元数据期间仍可标记已读。

## 隔离构建与发布证据

`tools/build_matrix.py` 默认只输出命令计划；`--execute` 才构建。固定 ESP-IDF 6.1、锁文件与 `APP_REPRODUCIBLE_BUILD`，使用独立 sdkconfig，不读取/覆盖 `firmware/sdkconfig`。首次使用新的 build 根目录；预设、锁文件或已生成设置变化时拒绝静默复用，改用新的目录。

```bash
python tools/build_matrix.py --profile default
python tools/build_matrix.py --all --ota-public-key /absolute/path/public.der --execute --jobs 2
```

四套预设为 default、full、off、tones；full 编译 SR/DSP、维护、两种扩展、OTA 和诊断/恢复，GPIO=-1 且仍需物理校准授权。full 必须显式提供 RSA-3072 PKCS#1 公钥 DER。默认 OTA 根地址为 `.invalid`，测试钥与测试镜像不得当生产发布。其他预设可加 `--diagnostics` 单独启用崩溃功能。

实际构建后生成 `build-evidence.json`（版本、Git 基线/变更文件清单、输入/源码及配套镜像/ELF 哈希、配置、SDK 与编译器）和 `dependency-licenses.json`（锁定依赖、声明许可证和许可证文件哈希）。变更清单描述相对 HEAD 的整个工作区状态，不将本轮修改与原有未提交修改混为一次提交。`NOASSERTION` 表示没有可直接读取的 SPDX 声明，需人工核对列出的许可证文本；清单不是法律结论或漏洞扫描。离线签名发布仍使用现有 `package_release.py`，构建脚本不签名、不烧录、不上传。

`.github/workflows/software.yml` 使用固定的 ESP-IDF 容器 digest 和 Actions commit，四套构建隔离执行；full 运行主机、协议、WSS、DSP 与 LVGL 检查。产物标记为未签名测试输出，保留 7 天。工作流只在本地创建，尚未推送或由 GitHub 执行；不包含同级 CORE、真实云端或硬件验收。

## 崩溃诊断与 USB 恢复

`diagnostic.defaults` 把 coredump 写入既有 `0xF20000/64 KiB` 分区，最多抓取 4 个任务，不抓取整个 DRAM/堆；空间有限不保证任意未来任务栈都能完整收集。默认产品预设不启用。

`PET_RECOVERY_ENABLED` 需要物理维护控制台。RTC 校验记录统计未连续健康运行 30 秒前的 panic/watchdog/CPU lockup 重启；连续 3 次后进入维护启动，不启动音频、网络、传感器、成长或扩展，串口和配置维护仍可用。此轮最初为 USB 维护路径；后续 [14](14-recovery-and-device-names.md)补齐一次性屏幕/BOOT 尝试及 USB 降级。正常模式的健康门槛包含配置存储、已启用的成长/UI/语音初始化。没有自动擦除数据，也不写 NVS 计数。

RTC 历史只跨支持的热重启保留，冷上电会清除，不宣称掉电持久保护。后续恢复屏幕在初始化前消耗 RTC 尝试位，防止显示故障形成恢复死循环。`PET_RECOVERY_CLEAR` 只清除下次启动的保护锁，当前维护启动继续隔离；先诊断修正，再人工重启。OTA 候选进入维护模式仍以不健康状态等待已有回滚逻辑，不确认失败固件。物理 panic、看门狗、回滚及 64 KiB 实际采集仍待设备验证。

```bash
python tools/configure_device.py diagnostics
python tools/configure_device.py recovery-clear
python tools/crash_dump.py read --port /dev/example --output /private/path/core.bin
python tools/crash_dump.py decode --core /private/path/core.bin --elf /path/nexus_pet.elf \
  --elf-sha256 <设备诊断appElfSha256或配套构建记录> --output /private/path/report.txt --execute
```

上述配置和读取命令默认只准备；实际发送分别需要 `--send --port`、`--execute`。raw dump 读取使用 ROM、无 stub、前后不复位，只读指定分区，需人工进入下载模式。离线 decode 不打开串口，用当前 SDK 的 `esp_coredump`；拒绝空/截断输入、错误 ELF 和不匹配哈希。哈希绑定所选 ELF，仍须核对其与故障设备的版本一致。

崩溃栈可能含文本、令牌或口令。原始文件和报告均排他创建为 600，不在终端转储内容，不自动外发；不提供擦除崩溃分区的快捷命令。诊断快照只增加是否存在/体积、启动状态及应用 ELF 哈希。

## 当前验证

| 检查 | 本轮结果 | 证据范围 |
| --- | --- | --- |
| CORE 完整 Maven 测试 | 27 项，25 通过、2 条件跳过，0 失败/错误 | 独立 PostgreSQL/MQTT；真实供应商及托管 C peer 在普通全套中跳过 |
| 保留策略 HTTP 流程 | `RetentionFlowTest` 6 项通过；默认关闭另在 Inbox 流程验证 | 预览不写、7/30 天、幂等、批量/占用、失败回滚、超期分析/导出拒绝、在途分析防回写、Halo 自动窗口及单条补录 |
| Vue 类型与生产构建 | 通过；随后 Maven `-DskipTests package` 成功 | JAR 包含最新前端，打包步骤未重复计作测试 |
| 固定浏览器回归 | 21 项通过；页面与 HTTP 夹具已退出 | 真实 Vue/DOM，HTTP 状态夹具；不替代真实 CORE/MQTT 联调 |
| 主机入口 | 通过，C 目标启用 ASan/UBSan | 含恢复状态机；构建/崩溃 CLI 各 2 项公开命令检查通过，不声称 Python 本身被插桩 |
| 四种隔离预设 | ESP-IDF 6.1 全部构建成功，预设配置逐项核对通过 | full 包含 flash coredump/恢复；只是目标编译，没有物理故障采集 |
| 默认预设独立重建 | 两个 build 目录的 6 个配套产物 SHA256 与逐字节比较全部一致 | 同一主机/SDK；不外推到其他预设或跨主机可复现性 |
| 契约与结构 | 10 schemas、1 profile、8 examples、2 extensions；19 组件无环；板级/codegen 一致 | 静态及生成物检查 |
| CI 工作流 | YAML 与四套矩阵检查通过 | 尚未推送或执行 GitHub 工作流 |

本轮构建根为 `firmware/build-matrix-20261112`，各目录都有 `build-evidence.json`、`dependency-licenses.json` 和日志；模型、bootloader、分区、otadata 与应用/ELF 共 6 个配套产物均核对实际字节数与 SHA256。版本仍为 `0.4.0-dev`，未创建 release/tag。

默认预设另在全新 `firmware/build-matrix-repro-20261112/default` 构建，和原目录直接比较所有 6 个文件的字节；结果记录在原构建根的 `reproducibility.json`。默认预设未启用 ESP-SR，该项比较的是 4 B 模型占位包；full 的真实模型包已单独记录哈希，不声称其做过第二次独立重建。

| 预设 | 应用字节数 | `nexus_pet.bin` SHA256 |
| --- | ---: | --- |
| default | 2,884,848 | `b47b32698fd1b37111c47087fe09b9344ad2e59db5e9b5382fe0a400790e31a7` |
| full | 3,608,976 | `29dd10f14abd7cf57c042562e26d3fce0bb7dbea72764329f2e3a5ffc4d69b93` |
| off | 225,728 | `ecaad0b28c563c414aa389f19296205217c8889a087e520a9aef2a6b0d7c5671` |
| tones | 2,925,568 | `866fad45eb69d5cf9248a611701b4d5b6fe87e39f94e6a10938c2b1241fd868a` |

CORE 版本仍为 `0.1.0-SNAPSHOT`，最终 JAR 为 26,285,010 B，SHA256 为 `1ca37bb0e684b27fe7a0f60ed0207accdd7e8d6e849a88afdd1e4ef51af8468e`。这些哈希只识别本轮实际输出；full 使用编译测试公钥与 `.invalid` 地址，不能作为生产发布。

本轮未迁移/清理真实库、修改运行中 ACL、部署、签发证书、烧录或执行设备动作。真实云端、硬件崩溃采集/成功解码、回滚/掉电、声学和 24 h/7 d 验收仍未执行；屏幕/BOOT 的后续软件实现见 [14](14-recovery-and-device-names.md)，物理显示/按键仍待验收。第三批长期记忆、跨端成长等后续能力未纳入本页交付范围。
