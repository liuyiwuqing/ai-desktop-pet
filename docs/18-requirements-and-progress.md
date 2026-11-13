# 18 · 当前需求、任务台账与交付约定

更新：2026-11-13。本页是后续工作的状态入口；各模块设计仍在 01–06，产品验收标准仍在 [07](07-roadmap-acceptance.md)。历史交付记录 09–17 保留当时证据，不覆盖本页的当前判断。

## 需求与范围

目标是在 ESP32-S3 N16R8 上集成语音、信息接收、温湿度、成长和扩展控制。设备插电联网后独立运行，Mac 仅用于开发，Mac 控制为可选功能。CORE 部署到常在线 Linux；基础语音仍由设备直接连接小智，CORE 不代理音频。

| 需求 | 当前软件交付 | 尚缺的产品验收 |
| --- | --- | --- |
| R1 语音与表现 | 唤醒/VAD、半双工、WSS/Opus、缓冲、上下文与 MCP、文字/表情、隐私门控；见 [01](01-ai-agent.md)、[12](12-cloud-interop.md) | 真实供应商协议/工具采用、真实音频、声学打断、延迟/连续对话 |
| R2 信息接收 | CORE 消息/管理台、MQTT/TLS、持久队列/去重、回执、DND、远程配置/回读/审计；见 [02](02-message-hub.md)、[11](11-product-software-closure.md) | 真实 Broker ACL/设备显示/断网掉电、生产迁移和消息时间线 |
| R3 温湿度 | DHT11 接口、采集/异常恢复、60 分钟图表、告警与成长联动；见 [03](03-climate-sensor.md) | 真实传感器接线、误差、刷新/故障恢复与长期采集 |
| R4 成长 | 本地互动/每日任务/成就/动画、对话与专注奖励、双槽存档/受控恢复；CORE 专注与设备只读镜像；见 [04](04-growth-game.md)、[21](21-growth-sync.md) | 实机掉电/7 天；两端独立结算，服务端奖励下发为二期 |
| R5 扩展控制 | 固定能力、确认/审计、Mac Bridge 与舵机软件/限位/急停；见 [05](05-extension-device-control.md)、[06](06-extension-servo.md) | 实际白名单控制、舵机型号/供电/机械校准；其他平台为后续范围 |
| R6 维护与交付 | 物理配置/诊断/备份、签名及远程 OTA/回滚逻辑、CORE/记忆工具双令牌、恢复 UI、隔离构建/CI、服务端发布包/预检 | 生产钥/HTTPS、真实服务凭据维护、目标机与实机恢复 |

首版不做本地大模型、任意 shell、自动旧 NVS 迁移、多设备集群或舵机闭环反馈。资源管线、多语言和更多控制平台保留为后续工作，不能将目录预留当作实现。

## 当前任务台账

“软件已验证”只代表所列本地边界，不代表真实部署或整机验收；提交/推送以对应仓库 Git 历史与远程分支为准。

| 编号 | 工作项 | 状态与证据 | 后续动作 |
| --- | --- | --- | --- |
| SW-01 | 设备首版与远程配置 | 软件已验证；设备提交 `959a4a4`，14 项设备切片及管理闭环见 [09](09-software-delivery.md)/[11](11-product-software-closure.md) | 保留硬件验收，不重新列为待编码 |
| SW-02 | 运行维护、恢复、更名、保留策略 | 软件已验证；见 [13](13-operational-software.md)/[14](14-recovery-and-device-names.md) | 真实库未迁移/清理，崩溃采集与恢复待实机 |
| SW-03 | 公开记忆及语音桥接 | 本地契约与 TLS 全链已验证；见 [15](15-memory-lifecycle.md)–[17](17-voice-bridge-regression.md) | 真实 Halo/OpenViking/供应商采用待验收 |
| DEL-01 | CORE 版本保护与离线交付 | CORE 基线 `913b3af`；CLI 7 项、CORE 41 通过/3 跳过、包内实际 JAR Linux 启动/重启通过；见 [交付记录](../deployment/README.md) | 两仓已推送；04 文档/工具提交 `7943cc6`。保留该基线 ZIP，后续包另记 DEL-02/DEL-04 |
| DEL-03 | Linux 凭据文件轮换验证 | 04 `69b6308` 已推送；实际包内 JAR/CLI、Linux 私有卷/UID 65534 的 admin 过渡/到期/撤销/异常拒绝/修复与重启通过；见 [20](20-credential-rotation.md) | Memory MCP/stdio 后续验证见 DEL-05；目标 systemd/真实用户目录仍待验收 |
| DEL-04 | 成长镜像配套离线包 | CORE `3e38ee5` / 04 `69b6308` 干净快照，18 文件；包内实际 JAR 的 Linux 启动/重启、V1–V12、admin 文件轮换通过；见 [21](21-growth-sync.md) | 包仅保留本地忽略目录；目标机、生产 TLS、真实供应商及实机仍待验收 |
| DEL-05 | Linux Memory MCP/stdio 热轮换 | 04 `f99a118` 已推送；同一 `3e38ee5` 包实际 JAR/CLI/stdio 通过过渡、到期、撤销、范围隔离、异常拒绝/恢复；同一 stdio 进程不重初始化、不转发/重放失败请求；见 [20](20-credential-rotation.md#linux-memory-mcp-与-stdio-验证) | 原 admin 模式/交付 CLI 10 项通过；[43384944971](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43384944971) 六个 job 全绿；真实 provider、生产 TLS/systemd/不同用户目录仍待验收 |
| CI-01 | 首次远程 CI 与容器 Git 所有权 | 首次运行的交付/桥接检查通过；四预设在 Git 源码清单处退出 128，尚未编译。真实 Git 所有权模拟复现并建立回归 | `5183050` 修复 Git 所有权，`43169dd` / `1ad19cf` 修复 Linux C17 的 `strnlen` / `strlcpy` 依赖；本地主机检查通过；远程 OTA `0910e5f` 的 [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 六个 job 全绿 |
| SW-04 | 远程 OTA 管理 | 软件已验证；CORE 配套 `2808ed7` 已推送，协议、设备事务、管理台/审计/ACL 见 [19](19-remote-ota.md)；四固件预设、主机/实际 Java–MQTT–C、浏览器通过 | 04 `0910e5f` 已推送、远程 CI 六个 job 全绿；生产钥/HTTPS 与实机 OTA 仍待验收 |
| SW-05 | CORE/记忆工具平滑凭据轮换 | 软件已验证；双令牌/到期/撤销、稳定任务归属、stdio 文件重载、CLI 与发布预检见 [20](20-credential-rotation.md) | CORE `9559429` / 04 `ccd3afa` 已推送；设备及上游服务凭据仍走各自维护流程，不经普通配置下发秘密 |
| SW-06 | 成长跨端同步 | SW-06-A 只读镜像软件已验证，CORE `3e38ee5` / 04 `08a8ad3` 已推送；四预设、实际 C–MQTT–CORE、主机及浏览器见 [21](21-growth-sync.md) | 后续 `69b6308` 的 [43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 六个 job 全绿；奖励下发仍为二期，先定义身份/来源/去重/迁移，不直接相加覆盖旧档 |
| SW-07 | 全双工/播放中声学打断 | SW-07-A 并行 HAL/有限诊断软件已验证，`9376eda` 已推送；主机 ASan/UBSan、隐私门控、五预设及源码/产物核对通过；见 [22](22-audio-duplex-diagnostic.md) | 后续 `82edd36` 的 [43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 七个 job 全绿；普通 AI 仍半双工，真实并行 I/O、参考对齐/漂移/声学打断仍待验收 |
| SW-08 | 资源/多语言/更多平台 | 后续范围；中文 UI 和 Mac 扩展已实现 | 当前优先运维闭环，不擅自引入新基础设施 |
| CONSOLE-01 | 测试消息未知结果重试 | CORE `07e3c53` 已推送；固定逻辑提交 ID/正文、导航恢复、确认后释放、拒绝/退出清理。新增浏览器 33 项与既有 78 项通过，CORE 59 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md) | 页面刷新/关闭后不恢复正文，应先核对列表；不是跨浏览器或设备展示保证。04 配套检查/文档同项提交 |
| CONSOLE-02 | 管理台完成度与验收清单 | `65eb2ef` 已推送；已按八导航、原完整规划及真实控制器核对；当前功能/验证/缺项见 [23](23-console-completion.md#console-02--当前实现与完整规划的差距) | 完整规划尚未全部实现；文档核对不算缺项完成。状态筛选已单独交付 CONSOLE-03 |
| CONSOLE-03 | 消息/投递状态筛选 | CORE `b85f704` / 04 `ff3ce39`、契约补充 `f4c4b7e` 均已推送；HTTP/隔离库 3 项、浏览器新增 63/既有 111 项通过；完整构建 62 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md#console-03--消息与投递状态筛选) | [43391298661](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43391298661) 七个 job 全绿；来源/设备/时间筛选、导出/取消/时间线仍待后续 |
| ACC-01 | 真实云端与常在线部署 | 等待接入条件；尚无目标服务器/域名及供应商桥接凭据 | [部署预检](../deployment/server-release.md)、[云端步骤](12-cloud-interop.md) |
| ACC-02 | 实机及稳定性 | 未开始，当前不烧录 | P1–P7、NVS 掉电、真实 OTA/回滚、24h/7d，见 [07](07-roadmap-acceptance.md) |

04 当前版本 `0.4.0-dev`、CORE `0.1.0-SNAPSHOT`，均为开发版本，尚无生产发布或整机验收结论。误回退的 22 文件已恢复；覆盖来源未查明，见 [17](17-voice-bridge-regression.md)，不把恢复成功写成原因调查完成。

## 每项工作必须闭环

1. 明确本项公开接口、验收条件和受影响仓库；读取相应设计、契约与源码。
2. 完成最小可独立验收的实现；新增功能优先从可观察失败反馈开始。
3. 更新对应功能文档、使用命令、契约与本页状态；新事实归项目文档，通用 Skill 不存项目流水账。
4. 执行并核对与修改相匹配的测试/构建；记录跳过、失败和未覆盖的真实环境边界。
5. 检查差异与秘密/产物边界，逐仓提交并正常推送，确认远程含有提交后继续下一项；不强推、不覆盖他人修改。
6. 推送触发的 CI 需另行核对；GitHub 已有 workflow 不等于已执行。无服务器/凭据/设备时先推进独立软件工作，不伪造验收记录。

## 本轮交付记录

本轮先将 DEL-01 的实现、预检和完成状态一并提交；先前验证使用真实 Java 21/PostgreSQL/Mosquitto/Caddy 隔离进程，外部供应商及硬件仍未接入。服务端包 SHA256 和各项边界在 [部署记录](../deployment/README.md)，Linux 证据在忽略目录 `build/server-release-20261113/linux-smoke.json`；ZIP 不包含凭据，不上传 Git。

CORE 文档提交 `ac9a29a` 与源码基线已推送至私有 [nexus-core/codex/core-baseline](https://github.com/liuyiwuqing/nexus-core/tree/codex/core-baseline)。04 的远程为私有 [ai-desktop-pet/main](https://github.com/liuyiwuqing/ai-desktop-pet/tree/main)，本项提交同时交付之前未推送的 `959a4a4`；后续每项文档随对应代码提交，提交号查 Git 历史，推送另以远程分支核对。

DEL-01 文档与工具已随 `7943cc6` 推送，两仓私有远程已确认。首次远程 [CI 43361889779](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43361889779) 的交付工具和桥接准备成功，四固件任务在 `git ls-files` 退出 128。`GIT_TEST_ASSUME_DIFFERENT_OWNER=1` 加空 safe.directory 在真实临时仓库复现相同调用失败；限定当前 ROOT 的单命令信任通过。修复不写全局 Git 设置，也不使用 `safe.directory=*`。本地回归 3 项通过后提交 CI-01；新的远程执行结果后续更新，不能将本地修复计作远程构建已通过。

CI-01 第二次远程执行 `43363151930` 已越过 Git 清单并完成 full 目标编译，主机入口在消息存储 `strnlen` 声明处失败。macOS 用 `_POSIX_C_SOURCE` 限制声明复现相同错误；改为标准 C 有界扫描后，实际消息存储编码/解码/历史夹具检查通过，二进制格式未改。本项单独提交，不包含在途 OTA 实现；下一次远程执行仍须核对。

CI-01 第三次远程执行 `43364202401` 四种固件均已完成编译；full 主机检查越过存储编解码后，在消息服务的 BSD `strlcpy` 声明处失败。以同样受限声明环境复现三处调用；改用标准 C `snprintf`，设备 ID 仍先校验、消息/回执 ID 的既有容量和截断行为保持。现有消息服务公开检查通过，并将受限 POSIX 声明参数纳入这两个主机用例。后续远端入口仍需核对。

SW-04：CORE `2808ed7` 已正常推送至 `codex/core-baseline` 并核对远程分支。04 配套代码、Schema、ACL 说明与 [19](19-remote-ota.md) 同项提交。本地四预设 default/full/off/tones 均退出 0，源码清单与产物哈希重新核对一致；完整主机 ASan/UBSan 及原 Java→C→Java 消息契约再次通过。CORE 完整构建 50 条中 46 通过、4 条条件跳过，新增默认关闭 HTTP 用例另行通过并重新打包；OTA 实际 C peer 单独通过，浏览器 OTA 22 项及既有管理 38 项通过。真实部署、生产签名、Flash/NVS 掉电及启动健康回滚未执行。

SW-04/CI-01：04 `0910e5f` 对应远程 [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 完成，四固件预设及交付/桥接六个 job 全部 success；full 的主机/协议/DSP/LVGL 步骤成功。前一 `43365426328` 因新提交被并发策略取消，未计为成功。

SW-05：CORE 标准完整构建 59 条中 55 通过、4 条条件跳过；7 项 HTTP/真实数据库/文件/CLI 流程通过，含实际第二服务实例保留工具归属、撤销不取消在途请求。CLI 4、stdio 7、桥接启动 6、实际 TLS 桥接 6、交付 10 项通过；已交付基线 ZIP 重新 verify 且 SHA256 未变。仅隔离临时凭据，未修改真实配置或执行生产轮换。

DEL-02：上述两仓干净快照生成 format=2 新服务端包，18 个文件，包 SHA256 `7c7670704dac7c1066275c449e95d4194b1db1599943f7748c2f3062a55f03b5`，详见 [部署记录](../deployment/README.md)。同包实际 Linux JAR 首次启动/重启、V1–V11、依赖与管理台资源检查通过。迁移列表硬编码 V1–V10 的测试缺陷已复现并改为读取 JAR 资源清单后重跑通过，不改变业务迁移。凭据文件在 Linux 下的动态轮换尚未执行，不能以环境模式启动检查替代。

SW-05/DEL-02：04 `3dec18a` 对应 [43368044891](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43368044891) 六个 job 全部 success。`ccd3afa` 的先前运行被并发策略取消，不计作成功；上述 Linux 动态轮换边界保持。

SW-06-A：CORE `3e38ee5` 已推送并核对远程；V12、只读镜像/管理台/ACL 同项交付。完整构建 64 条中 59 通过、5 条跳过；实际 C producer 单独回环、主机 ASan/UBSan、原消息协议及浏览器 18 项通过。首次矩阵在核对期间更新了 `config/README.md`，输入快照保护按预期拒绝混合来源证据，代码编译未因此报错；冻结输入后重新构建和核对清单。只读镜像不作为奖励流水或恢复包，二期奖励下发保留原范围。

SW-06-A 四种预设随后全部退出 0，363 份源码/每预设 6 个产物哈希核对通过；证据保留在忽略目录 `firmware/build-growth-sync-matrix/`，没有烧录或签发生产包。04 同项提交并推送，下一项补齐 Linux 凭据文件热轮换验证。

DEL-03：Linux `--auth-rotation` 的公开 CLI 先复现缺少该检查入口（参数退出 2），实现后使用相同 `9559429` 不可变包退出 0。真实非 root JAR/私有文件/包内 CLI 完成 admin 原子重载、过渡、到期、撤销、作用域隔离、损坏/权限错误及修复，轮换期间容器未重启，之后主动重启仍通过。V1–V11、依赖与管理台资源一并核对；临时容器/卷/网络清理成功。没有 Linux Memory MCP 热轮换或生产凭据验收结论。

DEL-04：两仓干净快照生成 `nexus-server-0.1.0-SNAPSHOT-3e38ee5.zip`，format=2、18 文件，绑定 CORE `3e38ee5` / 04 `69b6308`；SHA256 `8f888362855fcedbad186e4e9efef76a61859b22d7703599dae15a3a1cc17237`。同包 `--auth-rotation` Linux 检查退出 0，实际 JAR 的 V1–V12、首次启动/主动重启、MQTT/TLS 依赖、鉴权及两份管理台资源一致性均通过；admin 热轮换期间进程未重启。包内容与当前工具/JAR、证据哈希一致，`913b3af` / `9559429` 旧 ZIP 未改。命令与证据路径见 [21](21-growth-sync.md)，不构成目标机或生产验收。

SW-06-A/DEL-03：04 `69b6308` 对应远程 [43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 已完成，四固件预设与交付/桥接共六个 job 全部 success；full 的主机/协议/DSP/LVGL 步骤成功。此前 `08a8ad3` 对应运行被新提交取消，不计作成功。本次最终提交仅补交付记录与索引，未改变已通过检查的实现、配置和工具。

DEL-05：新增公开 `--memory-rotation` 入口，先复现缺少参数（退出 2），再以同一不可变包退出 0。初次 search 断言失败后补齐夹具的有界就绪探测；受控延迟 2 秒复现未就绪连接拒绝，完整重跑通过。证据 `build/server-release-20261113/linux-memory-rotation-ready-3e38ee5.json`，原 admin 兼容重跑和交付 CLI 10 项通过；V1–V12、首次启动/主动重启、静态资源一并核对。仅新增检查与文档，CORE/固件及既有包字节未改，推送后的 CI 单独核对。

DEL-05 的 `f99a118` 已正常推送并核对远程；对应 [43384944971](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43384944971) 六个 job 全部 success。随后继续 SW-07-A，不把前一项的远程构建作为新音频改动证据。

SW-07-A：新增显式双向 HAL、仅合成音的有限诊断及默认关闭的 duplex 维护预设，保持普通 AI 半双工与原单方向互斥。两种诊断均校验配置/持久静音/DND；初始化期间撤销先复现短暂启动采集，补充初始化后复核后，12 个启动场景及完整主机 ASan/UBSan 通过。SR/DSP、协议、WSS 与实际 LVGL 10 项通过；五预设退出 0，369 份源码、每预设 6 个产物及配置哈希核对一致。产物字节、命令与无烧录/无生产签发边界见 [22](22-audio-duplex-diagnostic.md)。本项推送后独立核对远端 CI。

CONSOLE-01：CORE `07e3c53` 已正常推送并核对远端，04 配套提交回环 HTTP 故障注入和实际 Vue 的 33 项专项。旧页面稳定复现两个请求产生两消息/不同 ID；保留逻辑提交后，保存后断线与保存前断线均重试原 ID/正文且仅保存一次。明确 4xx 拒绝、5xx、无效确认、导航、退出与 401、390px 均通过；既有管理 38/OTA 22/成长 18 项通过。CORE 完整 build 64 条中 59 通过/5 条可选环境跳过；JAR 内三份静态资源与本轮前端产物哈希一致，无旧 JS。实际生产/设备与原 DEL-04 不可变包不在本次验证范围。

CONSOLE-01 的 04 配套 `82edd36` 已推送，CORE `07e3c53` 与远端一致。CONSOLE-02 仅更新完成度文档：02 保留完整规划并明确其 Webhook 目标与当前管理员接口的差别；23 列出现有能力、专项未覆盖的页面状态和独立后续项。不把“已有核心流程”改写成“全部管理功能完成”。

SW-07-A/CONSOLE-01：`82edd36` 对应 [43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 已完成，default/full/off/tones/duplex 五固件预设与交付/桥接共七个 job 全部 success。`9376eda` 的先前运行因新提交取消，未计作成功；该结果覆盖已合入的音频诊断，不能替代真实音频验收。

CONSOLE-03：先以真实 HTTP/独立数据库复现筛选行/总数未过滤及非法值被接受，加入可选参数与固定/绑定谓词后 3 项通过。真实 Vue 先失败于缺少筛选入口，加入页面后专项 63 项通过；窗口尺寸切换的脚本点选问题修正后完整重跑。完整 CORE build 退出 0，67 条中 62 通过/5 条可选环境跳过、0 失败/错误；JAR 内三份静态资源与本轮前端逐字节哈希一致。没有真实库迁移、真实消息或设备状态流转验收。

CONSOLE-03 的既有浏览器回归再次退出 0：测试发送 33、管理/记忆 38、OTA 22、成长 18 项，共 111 项；连同新增筛选 63 项为 174 项。各任务空间均在成功后关闭，回环 HTTP 夹具退出，无临时探针或日志入库。

CONSOLE-03 的 CORE `b85f704` 已正常推送至 `codex/core-baseline` 并核对远程；04 配套夹具、公开浏览器入口和文档同项提交。推送触发的七个 CI job 使用本次运行单独核对，不用前一 `82edd36` 的成功代替本次结果。

CONSOLE-03 的 04 `ff3ce39` 已推送并核对远程。补查时发现漏同步 `DeliveryService` 源码指纹，守卫先失败；核对旧基线 SHA 与完整差异仅为列表查询后，更新这一项、其余五项保持。首次命令沿用本机 JDK 17，编译拒绝 `--release 21`；按既有文档显式选择 JDK 21 后，原 Java→固件 C 持久化/回执→Java 源码契约及 Schema 全部通过，无 SQL/MQTT/实机结论。契约补充 `f4c4b7e` 已推送并核对远端，只有 `integration/**` 与 `docs/**`，未触发新的固件运行。

CONSOLE-03 的本次远端 [43391298661](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43391298661) 已完成，绑定 `ff3ce39`：default/full/off/tones/duplex 五个固件预设及交付/桥接共七个 job 全部 success，full 的主机/协议/DSP/LVGL 步骤 success。该 workflow 不运行需要同级 CORE 和本地浏览器的 174 项专项，也不替代 CORE 的本地 Maven 验证；两类证据分别记录。最终只更新文档记录，不覆盖历史包或修改已验证实现。
