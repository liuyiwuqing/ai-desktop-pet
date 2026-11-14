# 18 · 当前需求、任务台账与交付约定

更新：2026-11-14。本页是后续工作的状态入口；各模块设计仍在 01–06，产品验收标准仍在 [07](07-roadmap-acceptance.md)。历史交付记录 09–17 保留当时证据，不覆盖本页的当前判断。

## 需求与范围

目标是在 ESP32-S3 N16R8 上集成语音、信息接收、温湿度、成长和扩展控制。设备插电联网后独立运行，Mac 仅用于开发，Mac 控制为可选功能。CORE 部署到常在线 Linux；基础语音仍由设备直接连接小智，CORE 不代理音频。

| 需求 | 当前软件交付 | 尚缺的产品验收 |
| --- | --- | --- |
| R1 语音与表现 | 唤醒/VAD、半双工、WSS/Opus、缓冲、上下文与 MCP、文字/表情、隐私门控；见 [01](01-ai-agent.md)、[12](12-cloud-interop.md) | 真实供应商协议/工具采用、真实音频、声学打断、延迟/连续对话 |
| R2 信息接收 | CORE 消息/管理台、MQTT/TLS、持久队列/去重、回执、DND、远程配置/回读/审计；见 [02](02-message-hub.md)、[11](11-product-software-closure.md) | 真实 Broker ACL/设备显示/断网掉电、生产迁移和消息时间线 |
| R3 温湿度 | DHT11 接口、采集/异常恢复、60 分钟图表、告警与成长联动；见 [03](03-climate-sensor.md) | 真实传感器接线、误差、刷新/故障恢复与长期采集 |
| R4 成长 | 本地互动/每日任务/成就/动画、对话与专注奖励、双槽存档/受控恢复；CORE 专注与设备只读镜像；见 [04](04-growth-game.md)、[21](21-growth-sync.md) | 实机掉电/7 天；两端独立结算，服务端奖励下发为二期 |
| R5 扩展控制 | 固定能力、确认/审计、Mac Bridge 与舵机软件/限位/急停；见 [05](05-extension-device-control.md)、[06](06-extension-servo.md) | 实际白名单控制、舵机型号/供电/机械校准；其他平台为后续范围 |
| R6 维护与交付 | 物理配置/诊断/备份、签名及远程 OTA/回滚逻辑、CORE/记忆工具双令牌、恢复 UI、隔离构建/CI、服务端发布包/预检、管理台 Docker Compose | 生产钥/HTTPS、真实服务凭据维护、目标机与实机恢复 |

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
| DEL-06 | 管理台 Docker 线上部署方案 | CORE `9026e41` / 04 `5f9c15c` 已推送并核对远端；源码镜像/Compose/私有初始化/健康检查与 [部署说明](../deployment/README.md#管理台-docker-部署del-06)；本地 CLI 11 项、完整构建 62 通过/5 跳过、实际 Linux Compose 六组检查通过 | [Docker CI 43398869944](https://github.com/liuyiwuqing/nexus-core/actions/runs/43398869944) 全步骤及 [04 CI 43398982965](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43398982965) 七个 job success；尚无目标服务器，公网证书/真实升级恢复/生产资源与设备仍待验收 |
| CI-01 | 首次远程 CI 与容器 Git 所有权 | 首次运行的交付/桥接检查通过；四预设在 Git 源码清单处退出 128，尚未编译。真实 Git 所有权模拟复现并建立回归 | `5183050` 修复 Git 所有权，`43169dd` / `1ad19cf` 修复 Linux C17 的 `strnlen` / `strlcpy` 依赖；本地主机检查通过；远程 OTA `0910e5f` 的 [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 六个 job 全绿 |
| SW-04 | 远程 OTA 管理 | 软件已验证；CORE 配套 `2808ed7` 已推送，协议、设备事务、管理台/审计/ACL 见 [19](19-remote-ota.md)；四固件预设、主机/实际 Java–MQTT–C、浏览器通过 | 04 `0910e5f` 已推送、远程 CI 六个 job 全绿；生产钥/HTTPS 与实机 OTA 仍待验收 |
| SW-05 | CORE/记忆工具平滑凭据轮换 | 软件已验证；双令牌/到期/撤销、稳定任务归属、stdio 文件重载、CLI 与发布预检见 [20](20-credential-rotation.md) | CORE `9559429` / 04 `ccd3afa` 已推送；设备及上游服务凭据仍走各自维护流程，不经普通配置下发秘密 |
| SW-06 | 成长跨端同步 | SW-06-A 只读镜像软件已验证，CORE `3e38ee5` / 04 `08a8ad3` 已推送；四预设、实际 C–MQTT–CORE、主机及浏览器见 [21](21-growth-sync.md) | 后续 `69b6308` 的 [43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 六个 job 全绿；奖励下发仍为二期，先定义身份/来源/去重/迁移，不直接相加覆盖旧档 |
| SW-07 | 全双工/播放中声学打断 | SW-07-A 并行 HAL/有限诊断软件已验证，`9376eda` 已推送；主机 ASan/UBSan、隐私门控、五预设及源码/产物核对通过；见 [22](22-audio-duplex-diagnostic.md) | 后续 `82edd36` 的 [43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 七个 job 全绿；普通 AI 仍半双工，真实并行 I/O、参考对齐/漂移/声学打断仍待验收 |
| SW-08 | 资源/多语言/更多平台 | 后续范围；中文 UI 和 Mac 扩展已实现 | 当前优先运维闭环，不擅自引入新基础设施 |
| CONSOLE-01 | 测试消息未知结果重试 | CORE `07e3c53` 已推送；固定逻辑提交 ID/正文、导航恢复、确认后释放、拒绝/退出清理。新增浏览器 33 项与既有 78 项通过，CORE 59 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md) | 页面刷新/关闭后不恢复正文，应先核对列表；不是跨浏览器或设备展示保证。04 配套检查/文档同项提交 |
| CONSOLE-02 | 管理台完成度与验收清单 | `65eb2ef` 已推送；已按八导航、原完整规划及真实控制器核对；当前功能/验证/缺项见 [23](23-console-completion.md#console-02--当前实现与完整规划的差距) | 完整规划尚未全部实现；文档核对不算缺项完成。状态筛选已单独交付 CONSOLE-03 |
| CONSOLE-03 | 消息/投递状态筛选 | CORE `b85f704` / 04 `ff3ce39`、契约补充 `f4c4b7e` 均已推送；HTTP/隔离库 3 项、浏览器新增 63/既有 111 项通过；完整构建 62 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md#console-03--消息与投递状态筛选) | [43391298661](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43391298661) 七个 job 全绿；更多筛选/元数据导出见 CONSOLE-13，取消/时间线见 CONSOLE-04 |
| CONSOLE-04 | 投递状态时间线与受限取消 | CORE `109b83f` / 04 `0dec4b6` 已推送并核对远程；HTTP/worker/隔离 SQL 6 项，实际 Vue 新增 62/既有 191 项，Java→固件 C→Java 契约通过；完整构建 70 通过/5 跳过、JAR 一致；见 [23](23-console-completion.md#console-04--事务状态时间线与受限取消) | CORE [43410896935](https://github.com/liuyiwuqing/nexus-core/actions/runs/43410896935) Docker job、04 [43410974174](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43410974174) 七个 job 全绿；V13 只在隔离库验证，旧行历史未知，无设备撤回或生产迁移 |
| CONSOLE-05 | 只读运行概览 | CORE `af916aa` / 04 `24df6d9` 已推送并核对远程；HTTP/隔离 PostgreSQL 2 项、实际 Vue 17 项、完整构建 64 通过/5 跳过及 JAR 一致；见 [23](23-console-completion.md#console-05--只读运行概览) | CORE [43408287857](https://github.com/liuyiwuqing/nexus-core/actions/runs/43408287857) Docker job、04 [43408341106](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43408341106) 七个 job 全绿；生产/设备另行验收 |
| CONSOLE-12-A | 工具/规则创建与页面故障处理 | CORE `28793aa` / 04 `052851f` 已推送并核对远端；HTTP/隔离 PostgreSQL 4 项、实际 Vue 新增 96/既有 253 项；完整构建 74 通过/5 跳过、JAR 三资源/V14 一致；见 [23](23-console-completion.md#console-12-a--工具规则创建与页面故障处理) | CORE [43414082430 第二次运行](https://github.com/liuyiwuqing/nexus-core/actions/runs/43414082430/attempts/2) Docker 全步骤 success、04 [43414283771](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43414283771) 七个 job success；真实工具/提醒、供应商与设备仍待验收 |
| CONSOLE-13 | 更多筛选与元数据 CSV | CORE `ea9cb77` / 04 `841f13d` 已推送并核对远端；本地 HTTP/隔离 PostgreSQL 新增 6 项、实际 Vue 新增 44/既有 349 项通过；完整构建 80 通过/5 跳过、JAR 三资源/V14 一致、Java→固件 C→Java 源码契约通过；见 [23](23-console-completion.md#console-13--更多筛选与元数据-csv) | CORE [43553123583](https://github.com/liuyiwuqing/nexus-core/actions/runs/43553123583) Docker 全步骤、04 [43553167085](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43553167085) 七个 job success；正文导出、生产与实机仍待验收 |
| DOC-01 | 栖团 Qitomo 命名、Logo 与项目介绍 | 本地文档与资产验证通过；README、[项目介绍](24-project-introduction.md)、[品牌说明](25-brand-identity.md)、[本地上手](26-getting-started.md)，8 份 SVG / 4 份 PNG；成长/语音演练、LVGL 10 项、契约/依赖/板型检查通过 | 本项只含 README 与 docs；固件、协议与其他仓库未更名，不触发当前 push 路径的固件 CI；提交/推送以本项 Git 历史和远端核对为准 |
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

DEL-06：按本次线上 Docker 要求，在 CORE 交付独立的源码多阶段镜像和 `deploy/docker/compose.yml`，Vue 与 API 随同一 JAR 提供，配套 PostgreSQL/MQTT/Caddy。新私有目录初始化沿用已有 ACL/证书生成器，增加容器 DNS SAN，不覆盖现有目录；受鉴权健康检查验证数据库及 Broker，同时保留环境令牌/凭据文件模式。CORE 非 root/只读、无公开 8088，数据库无宿主端口；TLS、持久卷、有限日志和 30 秒停止均有配置，原开发 Compose/systemd 模板与旧离线包保留。

DEL-06 本地 CLI 11 项、TypeScript/Vue 构建与完整 Maven verify 退出 0（67 条中 62 通过/5 条可选环境跳过，0 失败/错误）；实际源码 `linux/arm64` 镜像 `sha256:e6635840f0852cf16f6bc7f31233dbfd857c47f03458d7790ac9efdbf4ac8fd9`，包内 JAR SHA256 `13b4f72a55b447316c40c161983d316e6cb1847b159fc6db65b434c4fb856e57`。随机隔离 Compose 的 HTTPS/管理员鉴权/JAR 资源一致、严格 MQTT TLS、私有端口/非 root/只读、含 `$` 的原样 env、完整重建保留消息/HTTPS 证书、Broker 中断使健康失败及同进程恢复六组检查通过。初次严格 TLS 暴露新 CA 缺少 keyUsage，补齐约束后重跑通过；另修正脚本使用既有 `messageId` 字段，没有改业务 API。临时项目/卷/网络及失败私有目录均已清理，报告留 CORE 忽略目录 `target/docker-smoke.json`。没有注册设备、发送真实消息、迁移真实库、启用清理、申请公网证书或上线服务器。

DEL-06 的 CORE `9026e41` / 04 `5f9c15c` 已正常推送并核对远程；新增 Ubuntu Docker workflow 只构建和运行临时 CLI/Compose，不发布镜像或部署。对应远程 [43398869944](https://github.com/liuyiwuqing/nexus-core/actions/runs/43398869944) 已完成：`docker-compose` job 与全部步骤 success，CLI 11 项、源码镜像及六组实际 Compose 检查通过；不运行宿主完整 Maven 集成套件，62 通过/5 跳过仍是本地证据。04 本项仅更新台账与部署索引，但 `deployment/**` 位于既有 workflow 触发范围；对应 `5f9c15c` 的 [43398982965](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43398982965) 已完成，五个固件预设与交付/桥接共七个 job success，full 的主机/协议/DSP/LVGL 步骤 success，和 Docker 专项分别记录。CORE CI 文档 `eeddbb0` 与 04 记录 `b025bd2` 也已推送并核对远端；最终仅在本页补验收记录，不改变已验证实现、配置或旧包，也不触发新的构建。

DOC-01（2026-11-13）：产品介绍采用“栖团 Qitomo”，Logo 从现有猫形角色提炼猫耳与对话气泡，沿用 Mint / Ink 配色。新增三份介绍/品牌/上手文档，根 README 改为产品入口，文档索引保留所有技术与历史交付入口。品牌目录含 8 份独立 SVG、三尺寸 RGBA 图标及一份展示 PNG，字形来自项目已使用且 SHA-256 一致的 Noto Sans SC，正式资产均已转路径。图标与展示为品牌素材，不作为设备截图或固件资源。

本项实际执行：`bash tools/simulate_growth.sh --state-dir "$PWD/build/brand-check/growth-state"` 按新文档完成照料、任务领取、成就、睡眠和唤醒；`bash tools/simulate_voice.sh` 以合成 PCM / 注入事件走通 listening → thinking → speaking → idle 及静音/恢复；两者退出 0。`bash tools/ci/test_ui_host.sh` 10/10 通过，`python3 tools/export_pet_preview.py` 成功输出真实 LVGL 帧的本地预览。`check_contracts.py` 验证 14 schema / 1 profile / 12 example / 2 extension，`check_deps.py` 验证 19 组件无环，`board_codegen.py --check` 通过。新文档及根 README 的本地链接、SVG XML/路径与外部引用、PNG 尺寸/透明角及 `git diff --check` 均已核对；已查看实际 PNG 的浅深色、单色与小尺寸效果。

品牌 PNG 通过 `node docs/assets/brand/render.mjs <已有 sharp 模块绝对路径>` 从入库 SVG 导出，使用宿主已有 sharp 0.35.5，无新增运行依赖。修改仅在根 README 与 docs，当前 workflow 的 push 路径不匹配，不将先前 CI 结果计作本项结果。未改固件逻辑、构建身份、唤醒词、协议、存档或其他仓库；没有部署、真实消息、烧录或擦除设备。提交推送另以本项 Git/远端核对。

CONSOLE-05：按当前缺项推进只读运行概览，明确 UTC 收件窗口、90 秒在线规则、当前失败比例和全部保留投递积压；单条数据库查询保持计数同一快照。先复现公开接口 404 与页面缺少导航，再完成接口 2 项和页面 17 项验证；完整 CORE build 退出 0（69 条中 64 通过/5 条可选环境跳过，0 失败/错误），JAR 内三份静态资源与当前前端哈希一致。管理脚本的末位导航假设改为按语义定位，既有测试发送 33/管理记忆 38/OTA 22/成长 18/筛选 63 共 174 项再次通过，连同本项共 191 项页面检查；各任务空间均成功关闭。没有真实部署、消息发送、数据库迁移或设备操作。

CONSOLE-05 的 CORE `af916aa` 已正常推送至 `codex/core-baseline` 并核对远端；04 配套检查、按语义定位的既有管理回归与本页记录同项提交。新增 17 项与既有 174 项浏览器检查、2 项 HTTP/真实数据库、64 通过/5 跳过的 Maven 结果均为本地证据；本项推送触发的 CI 另行核对。

CONSOLE-05 的 CORE `af916aa` / 04 `24df6d9` 已正常推送并核对远端；对应 [CORE CI 43408287857](https://github.com/liuyiwuqing/nexus-core/actions/runs/43408287857) 的 Docker job 与全部步骤 success，[04 CI 43408341106](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43408341106) 七个 job success。Maven 和浏览器专项仍为本地证据；不将概览的成功代替之后的取消/历史实现验收。

CONSOLE-04：先复现历史接口 404 和页面缺少时间线动作，再新增 V13 同事务历史、诚实旧行基线、永久取消 guard 及幂等取消。6 项真实 HTTP/worker/PostgreSQL 专项（含 8 次并发）与实际 Vue 62 项通过；既有 191 项再次通过，共 253 项页面检查。完整 Java→C→Java 源码契约在指纹守卫先拒绝后，核对完整差异并更新这一项哈希，补取消后 ACK 不恢复的断言并通过；原消息 Schema、主题/ACK 和其他五项源码哈希不变。只记录 CORE 提交状态，不冒充网络抓包/身份审计；无真实库迁移、清理、真实消息、部署、烧录或设备撤回。

CONSOLE-04 最终 `python3 tools/run_local.py build` 退出 0：类型/前端及 Maven verify/JAR 通过，75 条中 70 通过、5 条可选环境跳过，0 失败/错误。JAR 内三份静态资源与本轮前端逐字节哈希一致，无旧资源。CORE `109b83f` 已正常推送至 `codex/core-baseline`，`ls-remote` 核对一致；04 专项入口、源码契约与文档同项提交，本次推送的远程 CI 另行核对。

CONSOLE-04 的 CORE `109b83f` / 04 `0dec4b6` 已推送并核对远端；对应 [CORE CI 43410896935](https://github.com/liuyiwuqing/nexus-core/actions/runs/43410896935) 的 Docker job 与全部步骤 success，[04 CI 43410974174](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43410974174) 七个 job success，含 full 主机/协议/DSP/LVGL。源码构建/隔离 Compose、固件 CI 与本地 HTTP/Maven/浏览器分别记录。本次文档回填同步修正 02 的概览和时间线当前状态，不改变原 Webhook/路由/角色规划，也不覆盖旧包。

CONSOLE-12-A：旧工具页保存后丢响应稳定产生第二条请求；固定逻辑提交后回归通过。规则公开接口先拒绝尚未支持的 `requestKey`（400），V14 和可选键实现后 4 项真实 HTTP/PostgreSQL 通过，含八个同键并发、启用后重放不重置版本、旧调用和 V13→V14 旧行兼容。另以实际 Vue 复现操作拒绝提示在 5.3 秒轮询后消失；修复后只读后台刷新不会清掉操作错误。

本项 `bash tools/ci/test_operations_ui.sh` 96 项、既有页面 253 项均退出 0，合计 349 项，所有任务空间成功关闭；新夹具只用回环 RAM，不执行真实 Bridge/提醒。完整 `python3 tools/run_local.py build` 退出 0：79 条中 74 通过/5 条可选环境跳过，0 失败/错误；JAR 三份前端资源及 V14 与本轮来源逐字节一致，无旧资源。Schema 14/1 profile/12 examples/2 extensions 检查通过，原消息契约六份源码指纹未变。文档链接、Python/Bash 语法与差异检查通过；不迁移真实库、不执行清理/真实消息/工具、不部署或烧录。逐仓提交推送、本项 CI 另行核对。

CONSOLE-12-A 的 CORE `28793aa` 已正常推送至 `codex/core-baseline`，`ls-remote` 核对一致；04 专项、HTTP 边界与本页配套记录同项提交。原 Docker/固件 CI 只覆盖此前切片，本次推送使用新运行单独核对；规范留在项目契约，不写通用 Skill 或替换旧交付包。

CONSOLE-12-A 的 CORE `28793aa` / 04 `052851f` 均已推送并核对远端。[CORE CI 43414082430 第二次运行](https://github.com/liuyiwuqing/nexus-core/actions/runs/43414082430/attempts/2) 全步骤 success，含 CLI 11 项、源码镜像和真实隔离 Compose。第一次在 Maven 解析 Spring Boot `4.1.2` 父 POM 时报告不存在；随后中央仓库直连返回 200，文件 SHA-256 与本地缓存一致，且同 POM 的前项远程构建已成功。未改依赖/源码，重跑同一提交后通过；临时仓库/网络故障是合理推断，具体原因未确认。04 [43414283771](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43414283771) 七个 job success，含五预设与 full 主机/协议/DSP/LVGL。本地 Maven/浏览器证据不据此改写成远程测试；本次仅回填 CI 文档，不替换旧交付包。

CONSOLE-13：旧接口先复现来源/时间/设备筛选无效及导出 404，新增 6 项 HTTP/独立 PostgreSQL 与既有列表 3 项通过。公开参数绑定同一筛选条件，收件窗口包含开始、不含截止；管理员元数据 CSV 明确列白名单和公式转义，5001 行拒绝、恰好 5000 行成功，不新增迁移或正文导出。

实际 Vue 新增 44 项及既有发送 33/管理记忆 38/OTA 22/成长 18/筛选 63/概览 17/时间线 62/工具规则 96 项全部退出 0，合计 393 项，任务空间均成功关闭。真实浏览器下载并读取消息/投递 CSV，核对跨页完整筛选结果；重复同名下载覆盖、带空格选择器和复用旧页轮询污染初始请求记录的脚本问题已修正并完整重跑。下载权限提示经用户处理并明确回复继续后接回验证；已查看 390px 截图。夹具只用回环 HTTP/RAM，不冒充真实 CORE/数据库/Broker/设备。

最终完整 `python3 tools/run_local.py build` 退出 0：85 条中 80 通过、5 条可选环境跳过，0 失败/错误；JAR 三份静态资源与当前前端、V14 与源码逐字节一致。`JAVA_HOME` 显式选择 JDK 21 的 Java→固件 C 持久化/回执→Java 契约退出 0；原六份源码仅 `DeliveryService` 的列表/导出差异更新指纹，两份新增读取辅助类纳入编译与指纹，其他五项和消息 Schema/ACK 未改。Schema 14/1 profile/12 examples/2 extensions、Python/Bash 语法和差异检查通过。没有真实库迁移、清理、真实消息、部署或设备操作；旧离线包保持原快照。本项逐仓提交推送与远程 CI 单独记录。

CONSOLE-13 的 CORE `ea9cb77` 已正常推送至 `codex/core-baseline`，`ls-remote` 核对一致；04 配套检查、源码指纹与文档同项提交。推送后的 Docker/固件 CI 使用本项新运行分别核对，不沿用前一项结果；本次事实沉淀在项目契约，无需更新通用 Skill。

CONSOLE-13 的 CORE `ea9cb77` / 04 `841f13d` 均已正常推送并核对远端。[CORE Docker CI 43553123583](https://github.com/liuyiwuqing/nexus-core/actions/runs/43553123583) 全步骤 success，含初始化 CLI 11 项、源码镜像和真实隔离 Compose；[04 CI 43553167085](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43553167085) 七个 job success，含五固件预设和 full 主机/协议/DSP/LVGL。393 项浏览器、80 通过/5 跳过的完整 Maven 与源码契约仍为本地证据，两个 workflow 不运行这些专项。CORE CI 记录 `dcb88e3` 也已推送并核对远端；本次只回填文档，不改变已验证实现或原离线包。
