# 18 · 当前需求、任务台账与交付约定

更新：2026-11-16。本页是后续工作的状态入口；各模块设计仍在 01–06，产品验收标准仍在 [07](07-roadmap-acceptance.md)。历史交付记录 09–17 保留当时证据，不覆盖本页的当前判断。

## 需求与范围

目标是在 ESP32-S3-DevKitC-1 N16R8 上集成语音、信息接收、成长和可选扩展。正常发行硬件只要求开发板、240×320 ST7789、INMP441、MAX98357A 与喇叭；默认 BOOT 按键，温湿度、二维摇杆、光敏和舵机按需启用。设备插电联网后独立运行，Mac 仅用于开发，Mac 控制为可选功能。CORE 部署到常在线 Linux；基础语音仍由设备直接连接小智，CORE 不代理音频。

| 需求 | 当前软件交付 | 尚缺的产品验收 |
| --- | --- | --- |
| R1 语音与表现 | 唤醒/VAD、半双工、WSS/Opus、缓冲、上下文与 MCP、文字/表情、隐私门控；见 [01](01-ai-agent.md)、[12](12-cloud-interop.md) | 真实供应商协议/工具采用、真实音频、声学打断、延迟/连续对话 |
| R2 信息接收 | CORE 消息/管理台、MQTT/TLS、持久队列/去重、回执、DND、远程配置/回读/审计；见 [02](02-message-hub.md)、[11](11-product-software-closure.md) | 真实 Broker ACL/设备显示/断网掉电、生产迁移和消息时间线 |
| R3 可选温湿度扩展 | DHT11 接口、采集/异常恢复、60 分钟图表、告警与成长联动；见 [03](03-climate-sensor.md) | 真实传感器接线、误差、刷新/故障恢复与长期采集 |
| R4 成长 | 本地互动/每日任务/成就/动画、对话与专注奖励、双槽存档/受控恢复；CORE 专注与设备只读镜像；见 [04](04-growth-game.md)、[21](21-growth-sync.md) | 实机掉电/7 天；两端独立结算，服务端奖励下发为二期 |
| R5 扩展控制 | 固定能力、确认/审计、Mac Bridge 与舵机软件/限位/急停；见 [05](05-extension-device-control.md)、[06](06-extension-servo.md) | 实际白名单控制、舵机型号/供电/机械校准；其他平台为后续范围 |
| R6 维护与交付 | 物理配置/诊断/备份、签名及远程 OTA/回滚逻辑、CORE/记忆工具双令牌、恢复 UI、隔离构建/CI、服务端发布包/预检、管理台 Docker Compose | 生产钥/HTTPS、真实服务凭据维护、目标机与实机恢复 |

首版不做本地大模型、任意 shell、自动旧 NVS 迁移、多设备集群或舵机闭环反馈。资源管线、多语言和更多控制平台保留为后续工作，不能将目录预留当作实现。

2026-11-15 用户确认保持现有分期：本次范围为管理台与交付，按 CONSOLE-01–13 的本轮软件切片、回归、镜像及离线包交付收口。SW-06-B 跨端奖励下发与 SW-08 资源／多语言／更多平台在台账中单列二期或后续范围，后续启动另行安排；ACC-01 生产部署和 ACC-02 实机／稳定性仍分别待验收。

## 当前任务台账

“软件已验证”只代表所列本地边界，不代表真实部署或整机验收；提交/推送以对应仓库 Git 历史与远程分支为准。

| 编号 | 工作项 | 状态与证据 | 后续动作 |
| --- | --- | --- | --- |
| HW-01 | 发行硬件与扩展基线 | 软件已验证；DHT11 默认关闭、菜单跳过、功放 LRC 改为 GPIO40，保留内部板型身份；见 [27](27-hardware-and-input.md) | 主机、10 项 LVGL、隔离 default 构建通过；`e034310` 已推送并核对 origin/main，实物待验收 |
| HW-02 | 可选摇杆菜单输入 | 软件已验证；BOOT 保留、显式启用、方向与确认/返回、回中/消抖/连发及采样故障保护 | 主机、10 项 LVGL、ASan/UBSan 与隔离 full 构建通过；`7d14833` 已推送并核对 origin/main，ADC 与实物交互待验收 |
| HW-03 | 可选光敏与硬件使用说明 | 软件已验证；光敏 ADC1、平滑调光/手动上限/熄屏与异常回退；接线/配置及舵机保护同步 | `acec2a5` 已推送并核对；本地五预设、主机/LVGL/ASan/UBSan、远程 CI 七任务全绿，实物待验收 |
| HW-04 | 首台设备备份、烧录与基础硬件验证 | 16 MB 备份及芯片核验、64 KB NVS 初始化、五份产物独立回读、8 MB PSRAM 自检通过；屏幕直连供电后恢复绘制、BOOT 菜单正常，功放改线后提示音可听见，手动录音回放可辨认本人语音；见 [27](27-hardware-and-input.md#首次实机记录2026-11-15) | 显示传输/菜单/音频基础硬件已实测；用户补充未对时时宠物未显示，单列 HW-05。联网/供应商、UI 同启并行诊断超时、OTA/掉电及长稳仍未验收 |
| HW-05 | 未对时主页宠物与首次配置 | 已解除宠物绘制对时间同步的依赖，保留成长结算、菜单遮挡和存档异常保护；私有模板存于本机忽略目录 | `c6b761d` 已推送，远程 CI 七任务通过；用户确认宠物显示/菜单正常。后续配置改用 HW-06 热点 |
| HW-06 | 手机热点配网与小智官方连接配置 | 已实现独立 SoftAP/HTTP/LCD 设置、Wi-Fi-only/分组 CAS、菜单重开与离线退出、官方绑定/WSS 保存；见 [28](28-device-web-setup.md) | `317ad8f` 已推送，远程 CI 七任务通过；主机/ASan/UBSan、10 项 LVGL、手机浏览器、隔离构建通过；用户手机配网保存成功，设备取得官方 WSS 配置并正常重启联网/对时。真实语音暴露 HW-07 |
| HW-07 | 首次真实语音连接与音频验证 | 已修正 WSS 栈/TLS 内存与公网发送/首包时序；用户确认真实回答正常且结束无错误，设备对应 32 包上行/73 包下行、105,120 个真实播放采样、IDLE/DRAINED | `5aecf0b` 已推送，远程 CI 七任务通过；WSS ASan/UBSan、隔离 default 与维护构建、烧录回读通过；剩余栈最小 16,404 B，音频/NVS 错误 0；连续失败继续见 HW-08，长稳/声学仍待验收 |
| HW-08 | 连续语音、显示与输入实时预算 | 已实现并通过本地/实机：4 KiB 片内优先阈值、`-O2`、语音页按变化刷新及 DMA 余量诊断；隐私/超时保护保留 | 用户确认最终固件 5 轮全正常；USB 记录 7 轮 IDLE/DRAINED、显示失败 0；主机、LVGL/ASan/UBSan、隔离 default/维护构建与烧录回读通过。长回答欠载 24 次单列，长稳/OTA 待验收；提交与 CI 见末尾 |
| HW-09 | 待机崩溃进入恢复模式 | 已修复并通过本地/实机：事件任务改为通知唤醒、持续优先扫描，避免过期 Queue Set 通知导致任务返回；恢复与存档保护保留 | 用户确认主页/3 轮完整回答；同一会话连续观察约 6 分钟、8 轮 DRAINED，0 崩溃/重启/恢复锁定；主机/ASan/UBSan、default/维护构建和烧录回读通过。播放欠载单列，长期稳定性待验收；推送/CI 见末尾 |
| HW-10 | 基础硬件“小爱同学”语音唤醒 | 已实现并完成初步实机验收；固定双模型、缓存/PSRAM 预算、延迟重建，PTT 与隐私/恢复保护保留 | 自动连续 3 轮通过；用户确认人声 2 轮+BOOT 1 轮均正常，无闪屏/卡住；本地主机/ASan/UBSan、10 项 LVGL、voice/default 构建通过。同一会话约五分钟无重启/显示失败；`3eacd00` 已推送并核对 origin/main，远程 CI [43744450764](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43744450764) 八个 job 全部成功。长稳、远距离识别率、OTA/掉电仍待验收 |
| SW-01 | 设备首版与远程配置 | 软件已验证；设备提交 `959a4a4`，14 项设备切片及管理闭环见 [09](09-software-delivery.md)/[11](11-product-software-closure.md) | 保留硬件验收，不重新列为待编码 |
| SW-02 | 运行维护、恢复、更名、保留策略 | 软件已验证；见 [13](13-operational-software.md)/[14](14-recovery-and-device-names.md) | 真实库未迁移/清理，崩溃采集与恢复待实机 |
| SW-03 | 公开记忆及语音桥接 | 本地契约与 TLS 全链已验证；见 [15](15-memory-lifecycle.md)–[17](17-voice-bridge-regression.md) | 真实 Halo/OpenViking/供应商采用待验收 |
| DEL-01 | CORE 版本保护与离线交付 | CORE 基线 `913b3af`；CLI 7 项、CORE 41 通过/3 跳过、包内实际 JAR Linux 启动/重启通过；见 [交付记录](../deployment/README.md) | 两仓已推送；04 文档/工具提交 `7943cc6`。保留该基线 ZIP，后续包另记 DEL-02/DEL-04 |
| DEL-03 | Linux 凭据文件轮换验证 | 04 `69b6308` 已推送；实际包内 JAR/CLI、Linux 私有卷/UID 65534 的 admin 过渡/到期/撤销/异常拒绝/修复与重启通过；见 [20](20-credential-rotation.md) | Memory MCP/stdio 后续验证见 DEL-05；目标 systemd/真实用户目录仍待验收 |
| DEL-04 | 成长镜像配套离线包 | CORE `3e38ee5` / 04 `69b6308` 干净快照，18 文件；包内实际 JAR 的 Linux 启动/重启、V1–V12、admin 文件轮换通过；见 [21](21-growth-sync.md) | 包仅保留本地忽略目录；目标机、生产 TLS、真实供应商及实机仍待验收 |
| DEL-05 | Linux Memory MCP/stdio 热轮换 | 04 `f99a118` 已推送；同一 `3e38ee5` 包实际 JAR/CLI/stdio 通过过渡、到期、撤销、范围隔离、异常拒绝/恢复；同一 stdio 进程不重初始化、不转发/重放失败请求；见 [20](20-credential-rotation.md#linux-memory-mcp-与-stdio-验证) | 原 admin 模式/交付 CLI 10 项通过；[43384944971](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43384944971) 六个 job 全绿；真实 provider、生产 TLS/systemd/不同用户目录仍待验收 |
| DEL-06 | 管理台 Docker 线上部署方案 | CORE `9026e41` / 04 `5f9c15c` 已推送并核对远端；源码镜像/Compose/私有初始化/健康检查与 [部署说明](../deployment/README.md#管理台-docker-部署del-06)；本地 CLI 11 项、完整构建 62 通过/5 跳过、实际 Linux Compose 六组检查通过 | [Docker CI 43398869944](https://github.com/liuyiwuqing/nexus-core/actions/runs/43398869944) 全步骤及 [04 CI 43398982965](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43398982965) 七个 job success；尚无目标服务器，公网证书/真实升级恢复/生产资源与设备仍待验收 |
| DEL-07 | 1Panel 拉取镜像与无源码初始化 | CORE `043f134` 已推送并核对远端；GHCR 发布/双架构/回拉验证工作流、独立初始化镜像、普通 env 的完整三服务编排及 [部署说明](../deployment/README.md#1panel-镜像部署del-07) 已补齐；本地 CLI 11 项、原部署六组、新镜像部署七组通过，actionlint 通过；发布运行 [43584412344](https://github.com/liuyiwuqing/nexus-core/actions/runs/43584412344) 成功，两个 digest 回拉后 7 项验证通过；2026-11-17 对 `e76732b` 重发布 [44094002890](https://github.com/liuyiwuqing/nexus-core/actions/runs/44094002890) 成功，两个包改为 Public 后匿名回拉 200，见末尾 | 已有用户提供的 Debian/1Panel 信息，目标机、HTTPS/网络与设备仍待验收 |
| DEL-08 | 1Panel 本地镜像编排 | 本地 7 组检查通过；[部署文件](../deployment/1panel/docker-compose.yml)与[使用说明](../deployment/1panel/README.md)使用 CORE 完整提交标签/amd64/禁止自动拉取，基础服务缺失时拉取，保留原私有目录/项目名/卷/安全配置；04 `1436113` 已推送并核对 origin/main；2026-11-17 本地编排升级至 `e76732b` digest 并补 `console-sessions` 会话卷，隔离启动 7 项通过，见末尾 | 实际已发布 amd64 CORE 在本机模拟运行，PG/MQTT/初始化按本机架构；解析、缺镜像拒绝、启动/鉴权/TLS、重建保留通过；[CI 43729640797](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43729640797) 七个 job success；目标服务器/HTTPS/设备未验收 |
| CI-01 | 首次远程 CI 与容器 Git 所有权 | 首次运行的交付/桥接检查通过；四预设在 Git 源码清单处退出 128，尚未编译。真实 Git 所有权模拟复现并建立回归 | `5183050` 修复 Git 所有权，`43169dd` / `1ad19cf` 修复 Linux C17 的 `strnlen` / `strlcpy` 依赖；本地主机检查通过；远程 OTA `0910e5f` 的 [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 六个 job 全绿 |
| SW-04 | 远程 OTA 管理 | 软件已验证；CORE 配套 `2808ed7` 已推送，协议、设备事务、管理台/审计/ACL 见 [19](19-remote-ota.md)；四固件预设、主机/实际 Java–MQTT–C、浏览器通过 | 04 `0910e5f` 已推送、远程 CI 六个 job 全绿；生产钥/HTTPS 与实机 OTA 仍待验收 |
| SW-05 | CORE/记忆工具平滑凭据轮换 | 软件已验证；双令牌/到期/撤销、稳定任务归属、stdio 文件重载、CLI 与发布预检见 [20](20-credential-rotation.md) | CORE `9559429` / 04 `ccd3afa` 已推送；设备及上游服务凭据仍走各自维护流程，不经普通配置下发秘密 |
| SW-06 | 成长跨端同步 | SW-06-A 只读镜像软件已验证，CORE `3e38ee5` / 04 `08a8ad3` 已推送；四预设、实际 C–MQTT–CORE、主机及浏览器见 [21](21-growth-sync.md) | 后续 `69b6308` 的 [43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 六个 job 全绿；奖励下发仍为二期，先定义身份/来源/去重/迁移，不直接相加覆盖旧档 |
| SW-07 | 全双工/播放中声学打断 | SW-07-A 并行 HAL/有限诊断软件已验证，`9376eda` 已推送；主机 ASan/UBSan、隐私门控、五预设及源码/产物核对通过；见 [22](22-audio-duplex-diagnostic.md) | 后续 `82edd36` 的 [43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 七个 job 全绿；普通 AI 仍半双工，真实并行 I/O、参考对齐/漂移/声学打断仍待验收 |
| SW-08 | 资源/多语言/更多平台 | 后续范围；中文 UI 和 Mac 扩展已实现 | 当前优先运维闭环，不擅自引入新基础设施 |
| CONSOLE-01 | 测试消息未知结果重试 | CORE `07e3c53` 已推送；固定逻辑提交 ID/正文、导航恢复、确认后释放、拒绝/退出清理。新增浏览器 33 项与既有 78 项通过，CORE 59 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md) | 页面刷新/关闭后不恢复正文，应先核对列表；不是跨浏览器或设备展示保证。04 配套检查/文档同项提交 |
| CONSOLE-14 | Naive UI 与管理台界面统一 | 软件本地已验证：三组导航/手机抽屉、消息筛选与导出、设备卡片、统一按钮与样式 nonce；见 [23](23-console-completion.md#console-14--naive-ui-与界面统一) | 完整 CORE 113 通过/5 条件跳过，19 组原页面 750 次与新增主题 21 次检查通过；CORE `9dc7ad9` 已推送并核对远端；JAR 三资源/20 迁移一致，04 配套与 CI 见末尾，部署/设备验收独立 |
| CONSOLE-21 | 紧凑布局与 Naive UI 表单 | 已实施：缩小侧栏/页头/卡片间距、设备设置满宽、原生表单与下拉统一组件；见 [23](23-console-completion.md#console-21--紧凑布局与-naive-ui-表单) | 本轮检查、构建和配套提交见末尾；不改变 API、权限与未知结果重试契约 |
| CONSOLE-22 | 常用筛选折叠与全站按钮统一 | 软件本地已验证：消息状态/来源常驻，时间条件折叠并显示生效数量；导航/列表/图表/导出入口使用 Naive 按钮，见 [23](23-console-completion.md#console-22--常用筛选与全站按钮) | 8 组浏览器 328 次检查通过；完整构建、跨仓提交与 CI 见末尾；5173 开发预览持续运行 |
| CONSOLE-23 | 全站紧凑布局与一致按钮 | 软件本地已验证：十二页操作按钮统一描边/尺寸，侧栏账户重排；低频条件与记录折叠，校验与异常自动展开，见 [23](23-console-completion.md#console-23--紧凑页面与一致按钮) | 21 组浏览器 900 次检查通过；完整构建、配套提交与 CI 见末尾；5173 预览持续运行 |
| CONSOLE-24 | 紧凑表格、卡片与浮层 | 软件本地已验证：共享表面/标题/间距规范，确认区与浮层同步，见 [23](23-console-completion.md#console-24--紧凑表格卡片与浮层) | 21 组浏览器 913 次检查通过；完整构建及跨仓交付见末尾 |
| CONSOLE-25 | 统一加载、空状态与错误反馈 | 软件本地已验证：统一展示组件、读取状态去重与紧凑提示，保留校验/核对/重试 | 22 组浏览器 1038 次检查、完整构建与最终包通过；跨仓交付见末尾 |
| CONSOLE-26 | 刷新恢复登录与令牌维护 | 软件本地已验证：8h 会话、退出/失效/跨页保护；管理员文件令牌修改，角色编辑/重新生成 | 23 组浏览器 1098 条、完整 128 项校验（5 条件跳过）、真实 5173/8088 会话与 SSE；交付见末尾 |
| CONSOLE-27 | 登录会话跨 CORE 重启持久化 | 软件本地已验证：私有摘要会话、重启恢复、退出持久删除、逐请求核验父凭据 | 真实独立 JVM、HTTPS 容器重建、本地 5173/8088 重启与退出复核；交付见末尾 |
| CONSOLE-02 | 管理台完成度与验收清单 | `65eb2ef` 已推送；已按八导航、原完整规划及真实控制器核对；当前功能/验证/缺项见 [23](23-console-completion.md#console-02--当前实现与完整规划的差距) | 完整规划尚未全部实现；文档核对不算缺项完成。状态筛选已单独交付 CONSOLE-03 |
| CONSOLE-03 | 消息/投递状态筛选 | CORE `b85f704` / 04 `ff3ce39`、契约补充 `f4c4b7e` 均已推送；HTTP/隔离库 3 项、浏览器新增 63/既有 111 项通过；完整构建 62 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md#console-03--消息与投递状态筛选) | [43391298661](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43391298661) 七个 job 全绿；更多筛选/元数据导出见 CONSOLE-13，取消/时间线见 CONSOLE-04 |
| CONSOLE-04 | 投递状态时间线与受限取消 | CORE `109b83f` / 04 `0dec4b6` 已推送并核对远程；HTTP/worker/隔离 SQL 6 项，实际 Vue 新增 62/既有 191 项，Java→固件 C→Java 契约通过；完整构建 70 通过/5 跳过、JAR 一致；见 [23](23-console-completion.md#console-04--事务状态时间线与受限取消) | CORE [43410896935](https://github.com/liuyiwuqing/nexus-core/actions/runs/43410896935) Docker job、04 [43410974174](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43410974174) 七个 job 全绿；V13 只在隔离库验证，旧行历史未知，无设备撤回或生产迁移 |
| CONSOLE-05 | 只读运行概览 | CORE `af916aa` / 04 `24df6d9` 已推送并核对远程；HTTP/隔离 PostgreSQL 2 项、实际 Vue 17 项、完整构建 64 通过/5 跳过及 JAR 一致；见 [23](23-console-completion.md#console-05--只读运行概览) | CORE [43408287857](https://github.com/liuyiwuqing/nexus-core/actions/runs/43408287857) Docker job、04 [43408341106](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43408341106) 七个 job 全绿；生产/设备另行验收 |
| CONSOLE-09-A | 停用后规则编辑 | CORE `cb18ba2`、测试补充 `2ba3a84` / 04 `e728aeb` 均已推送并核对远端；本地 HTTP/隔离 PostgreSQL 新增 7 项与既有创建 4 项通过；页面新增 99/既有 393 项、完整 CORE 构建 87 通过/5 条件跳过、最终 JAR 三资源/V1–V14 一致；见 [23](23-console-completion.md#console-09-a--停用后规则编辑) | CORE [43576489895](https://github.com/liuyiwuqing/nexus-core/actions/runs/43576489895) Docker 全步骤、04 [43576106575](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43576106575) 七个 job success；删除/测试运行及真实环境仍待验收 |
| CONSOLE-06-A | 独立来源与签名 Webhook | CORE `3d8e630` / 04 `2f29065` 均已推送并核对远端；V17 来源独立凭据、设备范围与路由交集、HMAC/5 分钟窗口、防重放/冲突、60 每分钟/突发 20、来源只读回执、版本维护/轮换和测试统计隔离；真实 HTTP 3 项与原概览 2 项通过，实际 Vue 34/管理页 38 次检查通过；完整构建 96 通过/5 跳过、最终 Webhook/凭据 10 项 verify 通过；见 [CORE 契约](../../nexus-core/docs/webhook-sources.md) | RSS/天气已由 CONSOLE-06-B 配套；生产来源、凭据、迁移和真实消息未操作 |
| CONSOLE-06-B | RSS / Atom 与室外天气拉取 | CORE `230c809` / 04 `e15692a` 均已推送并核对远端；V20、服务端固定来源/范围、默认停用/首次基线/去重、限时限量/禁止重定向/XXE、防并发及在途停用复核、接入状态/只读测试/消息筛选；HTTP 新增 3 项、完整 CORE 111 通过/5 跳过、最终专项 verify 3 项、CLI 3 项与 JAR 当前资源通过；实际 Vue 新增 32/管理 38/角色 27 次检查通过，见 [CORE 契约](../../nexus-core/docs/pull-connectors.md) | 逐仓交付；地址/坐标只走服务端私有配置，真实 RSS/天气和生产迁移未执行 |
| CONSOLE-07 | 路由与定向测试 | CORE `e859218` / 04 `9e75f9f` 均已推送并核对远端；V16 默认广播/显式规则模式，来源/类型/关键词/受限正则/跨午夜时间窗、设备去重与优先级快照；固定版本 CRUD、无写入预演及固定身份定向测试；HTTP/隔离 PostgreSQL 4 项、实际 Vue 34 次检查、原消息源码契约通过；完整 CORE 94 通过/5 条件跳过；见 [CORE 路由契约](../../nexus-core/docs/console-routing.md) | 配套模板已由 CONSOLE-08 提供；生产路由/迁移、设备 DND/离线与目标机另行验收 |
| CONSOLE-09-B | 规则删除与测试预演 | CORE `5086c91` / 04 `3d3c165` 均已推送并核对远端；V15 保留删除规则及执行历史、固定版本/停用/在途保护、原创建键不复活；只读预演无入队动作；新增 HTTP 3 项与既有编辑/创建共 14 项通过，完整 CORE 90 通过/5 条件跳过；实际 Vue 新增 52/编辑回归 99/工具规则回归 96 项通过，见 [CORE 契约](../../nexus-core/docs/console-automation-lifecycle.md) | 源码/文档配套逐仓提交；生产迁移、实际规则/设备及目标机未执行；条件与限定动作见 CONSOLE-09-C |
| CONSOLE-08 | 模板管理与路由挂接 | CORE `4915dc0` / 04 `f6aede6` 均已推送并核对远端；V19 版本化 CRUD、变量预览、当前设备无声音/8 秒/UTF-8 字节能力约束、路由与定向测试模板选择；投递快照不重写，源正文清理同事务清除快照；HTTP/路由/保留 12 项、实际 Vue 27 次检查、消息源码契约通过，完整 CORE 102 通过/5 条件跳过；见 [模板契约](../../nexus-core/docs/message-templates.md) | 逐仓提交；未变消息 Schema，生产模板/数据库/真实设备未操作 |
| CONSOLE-10 | 角色令牌与统一审计 | CORE `745043f` / 04 `2171363` 均已推送并核对远端；按用户确认的独立角色令牌方案：保留管理员，operator 消息/规则、viewer 只读；V18 服务端权限、过期/撤销、一次性签发、请求审计与显式 90 天/200 条保留接口；角色 HTTP 3 项及原凭据/Webhook 兼容 10 项通过，实际 Vue 27/原管理页 38 检查通过，完整 CORE 99 通过/5 条件跳过；见 [授权契约](../../nexus-core/docs/roles-and-audit.md) | 角色/模板/引导均已配套；未签发/撤销真实令牌或清理生产记录 |
| CONSOLE-11 | 实时更新与首次接入引导 | CORE `21ecb79` / 04 `1019f77` 均已推送并核对远端；认证请求头 SSE、版本化变化提示/撤销检查/有界连接/轮询回退；顶部设备视图与只读接入引导，登记/凭据/连接/接收/展示分开；HTTP/隔离 PostgreSQL 2 项、实际 Vue+SSE 20 次检查通过，完整 CORE 104 通过/5 跳过、最终 JAR 三资源逐字节一致；见 [CORE 契约](../../nexus-core/docs/console-events-and-setup.md) | 逐仓提交；生产 1Panel 代理、真实连接/凭据和设备验收未执行 |
| CONSOLE-12-A | 工具/规则创建与页面故障处理 | CORE `28793aa` / 04 `052851f` 已推送并核对远端；HTTP/隔离 PostgreSQL 4 项、实际 Vue 新增 96/既有 253 项；完整构建 74 通过/5 跳过、JAR 三资源/V14 一致；见 [23](23-console-completion.md#console-12-a--工具规则创建与页面故障处理) | CORE [43414082430 第二次运行](https://github.com/liuyiwuqing/nexus-core/actions/runs/43414082430/attempts/2) Docker 全步骤 success、04 [43414283771](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43414283771) 七个 job success；真实工具/提醒、供应商与设备仍待验收 |
| CONSOLE-12-B | 管理台最终软件回归 | CORE `230c809` 完整构建 116 条中 111 通过/5 条条件跳过；实际 Vue 19 组共 750 次检查全部通过，CORE CLI 11/交付 CLI 10、Schema 14 份/12 示例及 Java→固件 C→Java 消息契约通过；见 [23](23-console-completion.md#console-12-b--最终软件回归) | 新版镜像/离线包及三个 C peer/语音桥成功补跑见 CONSOLE-12-C；真实供应商与实机未验收 |
| CONSOLE-12-C | 新镜像与干净离线包 | CORE `230c809` 的 [发布 43620052738](https://github.com/liuyiwuqing/nexus-core/actions/runs/43620052738) 四个 job success；amd64/arm64 manifest、GHCR digest 回拉及 7 组真实隔离 1Panel 编排通过；18 文件 ZIP 绑定两仓干净提交，实际 Linux JAR 的 V1–V20、启动/重启、admin/Memory/同一 stdio 进程热轮换通过；[复制部署命令](27-1panel-quickstart.md) | 已发布镜像不等于已部署用户服务器；三个 C peer/语音桥补跑通过；04 [43620611463](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43620611463) 七个 job success |
| CONSOLE-13 | 更多筛选与元数据 CSV | CORE `ea9cb77` / 04 `841f13d` 已推送并核对远端；本地 HTTP/隔离 PostgreSQL 新增 6 项、实际 Vue 新增 44/既有 349 项通过；完整构建 80 通过/5 跳过、JAR 三资源/V14 一致、Java→固件 C→Java 源码契约通过；见 [23](23-console-completion.md#console-13--更多筛选与元数据-csv) | CORE [43553123583](https://github.com/liuyiwuqing/nexus-core/actions/runs/43553123583) Docker 全步骤、04 [43553167085](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43553167085) 七个 job success；正文导出已由 CONSOLE-13-B 提供；生产与实机仍待验收 |
| CONSOLE-13-B | 管理员正文导出 | CORE `d07bce8` / 04 `73d9f1e` 均已推送并核对远端；管理员显式确认、已应用条件与最近七天未清正文交集、1000 行上限和 CSV 公式/引号保护；真实 HTTP 新增 1/元数据 6 项、实际 Vue 正文 15/原元数据 44 检查通过，完整 CORE 105 通过/5 跳过、路径变体/1000 条最终 verify 通过；见 [正文导出契约](../../nexus-core/docs/content-export.md) | 逐仓提交；只导出隔离测试数据，没有读取生产消息 |
| CONSOLE-09-C | 消息条件与限定动作组合 | CORE `f39ecec` / 04 `343e23b` 均已推送并核对远端；来源/类型/关键词/优先级 AND、新消息固定提醒、公开分析原授权保护、自动提醒回环排除；HTTP/隔离库新增 3 项与原编辑/创建共 17 项 verify 通过，实际 Vue 新增 17/原编辑 99/删除 52 检查通过；见 [CORE 契约](../../nexus-core/docs/automation-conditions.md) | 同步契约/页面/专项；不开放任意动作，真实规则/消息与生产库未操作 |
| DOC-01 | 栖团 Qitomo 命名、Logo 与项目介绍 | 本地文档与资产验证通过；README、[项目介绍](24-project-introduction.md)、[品牌说明](25-brand-identity.md)、[本地上手](26-getting-started.md)，8 份 SVG / 4 份 PNG；成长/语音演练、LVGL 10 项、契约/依赖/板型检查通过 | 本项只含 README 与 docs；固件、协议与其他仓库未更名，不触发当前 push 路径的固件 CI；提交/推送以本项 Git 历史和远端核对为准 |
| ACC-01 | 真实云端与常在线部署 | 用户已提供 Debian/1Panel 部署信息与域名；新版镜像交付见 CONSOLE-12-C，尚未连接目标机或配置真实供应商 | [1Panel 部署](27-1panel-quickstart.md)、[部署预检](../deployment/server-release.md)、[云端步骤](12-cloud-interop.md) |
| ACC-02 | 实机及稳定性 | HW-04 完成基础烧录与显示/BOOT 菜单/提示音/语音录入回放；尚无联网语音与长期稳定性结论 | P1–P7 完整场景、NVS 掉电、真实 OTA/回滚、24h/7d 仍待验收，见 [07](07-roadmap-acceptance.md) |

04 当前版本 `0.4.0-dev`、CORE `0.1.0-SNAPSHOT`，均为开发版本；当前已有可拉取的私有部署镜像发布，尚无目标机生产部署或整机验收结论。误回退的 22 文件已恢复；覆盖来源未查明，见 [17](17-voice-bridge-regression.md)，不把恢复成功写成原因调查完成。

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

CONSOLE-09-A：旧编辑接口先返回 404，新增 7 项 HTTP/真实隔离 PostgreSQL/调度与既有创建 4 项通过。停用且无 executing 任务方可编辑，事务锁定规则后校验原版本；保存保持停用、相同确认不重复增版、旧创建键和历史保留，页面启停附带观察版本，旧无版本客户端仍兼容。非法时区先复现 500 后改为 400；高频调度下的测试 TRUNCATE 锁竞争改用每例独立规则 ID 消除，没有修改生产调度器。

`bash tools/ci/test_automation_edit_ui.sh` 新增 99 项，既有发送 33/管理记忆 38/OTA 22/成长 18/筛选 63/概览 17/时间线 62/工具规则 96/元数据导出 44 共 393 项全部退出 0，合计 492 项，任务空间均成功关闭。新增及既有入口均禁用页面缓存并核对实际加载的当前编译 JS；新专项改为单标签复用并修正异步等待。一次输入出现未由夹具写入的额外字符，失焦后完整复跑通过；没有据此修改产品逻辑。已查看 390px 表单和按钮区截图，并改进底部布局。夹具仅为回环 HTTP/RAM，不操作真实规则、消息、Bridge、供应商或设备。

完整 `python3 tools/run_local.py build` 退出 0：92 条中 87 通过、5 条可选环境跳过、0 失败/错误。随后仅调整窄屏底部布局和取消原因文案，重新执行所有页面入口的 TypeScript/Vue 检查与构建，清理生成静态目录后 `JAVA_HOME` 选择 JDK 21 执行 `mvn -s .mvn/settings.xml -B -ntp -DskipTests package` 退出 0；后端测试证据来自前述完整 verify。最终 JAR 恰含当前 index/JS/CSS 三资源，V1–V14 逐字节匹配，无新增迁移；原消息契约八份源码指纹均未变，Schema 14/1 profile/12 examples/2 extensions、Python/Bash 语法和差异检查通过。没有真实库迁移、清理、真实消息、部署或烧录，旧离线包保留原快照。逐仓提交推送与远程 CI 单独记录；本项语义留项目契约，无需更新通用 Skill。

CONSOLE-09-A 的 CORE `cb18ba2` 已正常推送至 `codex/core-baseline`，`ls-remote` 核对一致。04 配套脚本、当前资源守卫与文档同项提交；本项 Docker/固件 CI 使用新运行核对，不沿用 CONSOLE-13 结果。

CONSOLE-09-A 的 CORE `cb18ba2` / 04 `e728aeb` 已正常推送并核对远端。[CORE CI 43576059029](https://github.com/liuyiwuqing/nexus-core/actions/runs/43576059029) 全步骤 success，含初始化 CLI 11 项、源码镜像及真实隔离 Compose。后续仅测试提交 `2ba3a84` 将实际调度断言限定到本规则的运行/结果消息，避免其他规则到点影响计数，原 11 项专项再次退出 0；实现与最终 JAR 来源未变。该提交也已推送并核对远端，对应 [CORE CI 43576489895](https://github.com/liuyiwuqing/nexus-core/actions/runs/43576489895) 全步骤 success。

[04 CI 43576106575](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43576106575) 绑定 `e728aeb`，七个 job success，含五固件预设和 full 主机/协议/DSP/LVGL。492 项浏览器和 87 通过/5 跳过的完整 Maven 仍为本地证据；两个 workflow 不执行这些专项，不替代真实部署/供应商/设备。CORE CI 文档记录 `9061aaf`、`e0e81d4` 均已推送并核对远端；本次仅回填文档，不改变已验证实现、配置或旧离线包。


2026-11-15 本轮收口：CORE 最新实现 `230c809`，04 软件回归 `1fe497c`、镜像/包交付文档 `a9e7604` 均已正常推送并核对远端；每个独立切片均有单独实现与配套提交。CORE [镜像发布 43620052738](https://github.com/liuyiwuqing/nexus-core/actions/runs/43620052738) 四个 job success，双架构 manifest 与 digest 回拉后的 7 项编排通过；04 [43620611463](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43620611463) 绑定 `1fe497c`，七个 job 全部 success，含 default/full/off/tones/duplex 及 full 主机/协议/DSP/LVGL。后续只改 docs，受 CI 路径保护的代码/配置/工具与已验证提交一致；此前 `e15692a` 对应运行被新 push 取消，不计作成功。

当前可独立验收的本轮管理台软件切片、最终回归、镜像发布和干净离线包已交付；目标服务器生产部署/迁移/HTTPS、真实供应商、实机和长期稳定性仍未验收。设备 IP/RSSI/凭据维护 UI 等产品扩展、SW-06 奖励二期、SW-07 真实声学打断及 SW-08 后续范围继续保留，不把软件与隔离验证等同完整产品验收。用户已确认本次完成管理台与交付、二期单列；当前按该分期收口。


HW-01（2026-11-15）：按用户新要求调整基础发行硬件与可选扩展。先以公开菜单接口复现基础版仍可选中未安装的温湿度，新增断言失败后增加功能门控；已启用温湿度、语音按住/释放/取消、启动按住与消息抢占回归均通过。功放 LRC 从 GPIO38 改为 GPIO40，并由生成器拒绝 N16R8 存储、USB、UART、绑带及两版 RGB 保留引脚。JSON profile 与默认预设均关闭温湿度，full 显式保留其覆盖。

本项 `bash tools/ci/test_host.sh`、`bash tools/ci/test_ui_host.sh`（10/10）、`python3 tools/ci/test_build_matrix.py`（4/4）、`check_contracts.py`、`check_deps.py`、`board_codegen.py --check` 与差异检查均退出 0。显式设置 `IDF_PYTHON_ENV_PATH` 后，以 ESP-IDF v6.1 执行 `python tools/build_matrix.py --profile default --build-root firmware/build-matrix-hw01 --execute --jobs 4` 退出 0，实际 sdkconfig 中 UI/音频开启、温湿度/舵机关闭，产物/来源证据保存在忽略目录。本机 export.sh 最初错误查找未安装的 py3.14 环境，改用仓库已说明的现有 v6.1 venv 后成功，无安装或依赖变更。未烧录、未擦除、未接入真实模块；背光驱动与物理接线仍须验收。


HW-02（2026-11-15）：二维摇杆采用 ADC1 GPIO4/5 与 SW GPIO6，显式开关默认关闭。先从缺失公开导航接口的编译失败建立反馈，再实现回中门控、消抖、上下限速连发、左右单次、正反向菜单及按住说话；生成器拒绝 ADC2 与跨模块冲突，启用 DHT11 的自定义 GPIO 与摇杆冲突时编译拒绝。BOOT 始终可用，消息已读、外部确认和恢复仍只接受 BOOT；初始化失败回退 BOOT，采样错误取消旧摇杆手势。板型 profile 新增可选 `joystick`（缺省 false），同步生成 TypeScript 契约。

本项 `bash tools/ci/test_host.sh`、`bash tools/ci/test_ui_host.sh`（10/10）、`check_contracts.py`、`check_deps.py`、`board_codegen.py --check`、`test_board_codegen.py`、`test_build_matrix.py`（4/4）、Bash 语法与差异检查均退出 0。新增公开输入滤波/菜单及真实 BSP GPIO/ADC 边界故障测试通过，导航/菜单 ASan+UBSan 通过。已查看真实 LVGL 的摇杆成长菜单和语音菜单 PNG，布局断言在 240×320 内。ESP-IDF v6.1 的 `python tools/build_matrix.py --profile full --build-root firmware/build-matrix-hw02 --ota-public-key firmware/build-hardware-check/ota-test-public.der --execute --jobs 4` 退出 0，实际启用摇杆/DHT11，舵机 GPIO=-1，产物/源码证据在忽略目录。没有跑完整 UI FreeRTOS 任务端到端或真实 ADC/按键/显示；未烧录或擦除，未执行真实语音、舵机、外部消息。


HW-03（2026-11-15）：新增默认关闭的模拟光敏扩展（GPIO7，ADC1），与摇杆共享同一 ADC1 实例；显式启用后按相对亮度平滑调光，手动亮度保留为上限、0 保持熄屏，采样错误恢复手动值。恢复界面从不读取可选光敏；舵机继续默认关闭、GPIO=-1、显式 arm/限位/急停。补齐 [27](27-hardware-and-input.md) 的全套接线、配置、独立构建与物理边界。新 profile 可选 `lightSensor` 缺省 false，同步生成 TypeScript。

先验证缺失光敏接口的失败反馈，再实现公开调光策略；暗端收敛测试发现 1% 死区会停在 16%，随后修复边界收敛，固定暗端/手动上限/熄屏/异常/时钟回绕检查通过。`bash tools/ci/test_host.sh`、`bash tools/ci/test_ui_host.sh`（10/10）及两入口 `PET_HOST_SANITIZE=1` 均退出 0，新增 BSP 五种组合覆盖摇杆独立、光敏独立、同时开启、均关闭及反相。原 servo/外部确认/消息/语音检查保持通过。契约 14 schema/1 profile/12 examples/2 extensions、19 组件无环、生成物、build CLI 4 项、Bash/差异与本地文档链接均通过。

ESP-IDF v6.1 的 `python tools/build_matrix.py --all --build-root firmware/build-matrix-hw03 --ota-public-key firmware/build-hardware-check/ota-test-public.der --execute --jobs 4` 已完成 default/full/off/tones/duplex 五预设，全部退出 0；本次来源和产物在忽略目录。default 应用 2,859,824 B，实际 nm 未见温湿度 init/start、光敏采样/ADC 或舵机启动符号，UI/菜单存在。全部构建为未签名测试产物，未烧录、未擦除、未采集真实音频或 ADC、未移动舵机；不能代替整机、声学、电源、背光或长期验收。本项远程 CI 随提交另行核对。


HW-01–03 最终交付：实现分别为 `e034310`、`7d14833`、`acec2a5`，均已正常推送并逐次核对 origin/main。最终本地五预设均退出 0，应用大小 default/full/off/tones/duplex 分别为 2,859,824 / 3,632,096 / 225,760 / 2,902,752 / 2,886,544 B；每预设 6 个实际产物的大小/SHA256、源码清单与当前提交一致。default/off 不含温湿度/光敏/ADC 采样符号，full 含对应传感器符号；full 的 servo GPIO=-1 使启动调用及函数仍被裁剪，属于预期的非激活检查。一次将 full 预期误设为应有 servo 启动符号的验证断言失败，核对实际 GPIO=-1 与组合根后修正验证预期，未改动保护逻辑。

[远程 CI 43717383447](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43717383447) 绑定 `acec2a5`，7 个 job 全部 success；五个固件预设构建与产物归档通过，full 的 ASan/UBSan 主机、协议、WSS、SR/DSP 与真实 LVGL 步骤通过。此前 `e034310` 与 `7d14833` 的运行被后续 push 取消，不计为成功；最终成功覆盖其累计源码。后续仅回填 docs，不改变已验证的源码、构建开关、工具或产物。14 份本轮改动文档的本地链接均可解析，契约/依赖/生成物/差异检查再核对通过。

本轮硬件基线与可选输入的软件工作已完成；真实模块安装、背光驱动电路、麦克风/扬声器声学、ADC 方向与噪声、电源、舵机机械校准及实机长稳未验收。没有烧录、擦除、真实采集/消息/舵机动作或真实部署；ACC-01/ACC-02、二期与后续范围不因此自动完成。

HW-04（2026-11-15）：用户随后接入基础硬件并授权实机烧录，另行确认初始化新项目 NVS。先以 ESP-IDF v6.1 构建隔离的基础维护版本，再通过 esptool 5.6.0 读取完整 16 MB Flash，独立 `verify-flash` 核验备份摘要匹配；仅擦除 `0x9000` 起的 64 KB NVS 后，写入并逐份验证 bootloader、分区表、OTA 初值、应用及禁用模型的占位产物。没有全片擦除或修改 eFuse。备份、原分区记录、照片、串口日志与诊断产物仅存于本机忽略目录 `build/hardware-20261115/`，不提交。

基础版本关闭全部可选扩展和开机语音/录音诊断，通过原生 USB 主控制台查询维护状态；没有配置真实 Wi-Fi/Broker/供应商凭据。PSRAM 自检通过；同一串口会话运行 34 秒时显示提交 734 帧、失败 0、最大耗时 94 ms，NVS 失败 0，无 panic。用户实物仍只有背光，不能将这些软件计数当作显示通过。照片丝印为 240×320、2.4 寸 ST7789 SPI，供电脚为 3V3；用户确认经面包板电源轨供电，模块端电压尚未测量。10 MHz、无 LVGL 的黑白红绿蓝探针反复提交成功，用户观察仍完全无变化。

实机音频回环首测传输完成但 RMS/peak 为 0；启用 UI 的并行诊断出现 `ESP_ERR_TIMEOUT`，最大采集间隔约 2.28 秒，与 UI 启动时间重合。关闭 UI、使用 USB 主控制台的隔离构建随后完成 1 秒收发：16,000 个录入采样、24,000 个播放采样，drain/cleanup 成功，最大采集间隔 20,026 µs，peak 21,909；用户确认没有听到声音。尚不能把非零数据等同真实拾音，或将超时归因于硬件/调度。临时诊断只替换应用分区、不再次初始化 NVS；测试后恢复基础固件并独立回读校验。后续先断电绕过面包板验证屏幕供电，再逐项核实信号、功放供电/使能及真实音频；整机、联网、云端与长期稳定性均未通过验收。

屏幕排查结果：用户断电后仅将屏幕 3V3/GND 绕过面包板直接接开发板，六根信号线保持原接法；重新上电后确认正常显示角色页面。这将故障范围缩小到原面包板供电路径/连接，尚未测量确认具体断轨或触点；没有修改显示驱动、SPI 频率或控制器型号。基础固件保持关闭临时诊断。继续 BOOT 菜单与音频模块的物理验证，不据此宣布全部硬件通过。

HW-04 基础硬件收口：用户确认 BOOT 菜单可正常打开并切换；功放存在接线不一致，断电调整后已听到 1 秒合成提示音，具体原错接引脚未提供。普通开机回环缺少可听见的开始信号，用户只听到滋滋声；随后在忽略目录创建独立的 BOOT 手动诊断，直接编译当前 BSP 显示/音频及 `audio_loopback.c`/PCM/缓冲源码，不改正式实现或诊断开关。蓝色等待、短按释放后合成短音、红色录音 1 秒、绿色回放 1 秒；只有按键才采集，音频仅在 RAM 中并在退出时清除，不初始化 NVS、不启动网络。

手动诊断两次均 `ESP_OK`，每次录入/提交各 16,000 个采样，drain/cleanup 成功；第二次 RMS 1,529、peak 21,698，用户明确确认能听清自己的话。测试期间关闭 UI 的旧隔离版本曾显示白屏，恢复基础版本后用户再次确认角色页面正常，不将此阶段行为记为屏幕硬件回退。最后恢复初始维护基础应用并回读核验、检查启动；BOOT/显示、播放与录入回放分别有用户实物反馈，至此仅完成基础硬件验证。UI 同启的并行诊断超时原因仍未独立证实；真实联网语音、AEC/声学打断、掉电/OTA、扩展及长稳均未因此验收。提交仅同步这两份文档，不将本机探针、日志、照片或固件加入 Git，不追加无关软件测试或新 CI 结论。

HW-05（2026-11-15）：用户随后补充最终主页一直“等待时间同步”、宠物未显示，语音页提示未配置。前项关于“角色页面正常”的反馈只能证明已有界面和菜单，不能作为宠物实际可见的最终验收。当前源码以 `time_ready` 同时门控宠物动画/可见性；新增真实 LVGL 断言先退出 8，修正为只保留主页/存档可用条件后通过。等待对时文案仍显示，成长服务继续拒绝未对时的结算；菜单遮挡、返回和存档异常隐藏均保留。修改前实际 USB 查询凭据 revision 0，日志为 `network not provisioned`，当前基础固件 `PET_AI_XIAOZHI_ON_BOOT=n`，不能将音频回放等同联网语音。

本项 `ctest --test-dir firmware/build-ui-preview -R '^message_view$' --output-on-failure` 在修正后退出 0；`bash tools/ci/test_ui_host.sh` 10/10、`bash tools/ci/test_host.sh` 均退出 0，已查看 `growth-waiting-time` 实际 LVGL 预览。用户要求创建本机配置模板，空凭据 JSON 权限 600、目录 700，同步字段说明；补齐 `/data/` 忽略并核对模板不进入 Git。模板仍待用户填写，不写入设备、不伪造联网/时间或小智可用。

ESP-IDF v6.1 使用原基础维护开关在独立 `firmware/build-hardware-offline-20261115/` 完整构建，应用 2,860,624 B，SHA256 `92b772da50cb45eb1fdbc81bc3f2a6a9907d01e0d2269cdf07d97e99fc8b50c0`；bootloader/分区表/OTA 初值/模型与上一版逐字节一致。只在 `0x20000` 写应用并独立 `verify-flash` 摘要匹配，不再次初始化 NVS；启动观察 12 秒无 panic，UI 已初始化，诊断与开机语音仍关闭。构建/串口/原备份保留本机忽略目录。用户随后明确确认等待时间同步时宠物已显示、菜单正常；本项远程 CI 另记，不沿用 HW-04 的模糊画面结论。


HW-05 远程验证：`c6b761d` 的 [43726970383](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43726970383) 已完成，default/full/off/tones/duplex、voice-bridge-preparation、server-delivery-tools 七任务均成功。

HW-06（2026-11-15）：用户要求通过设备热点/手机浏览器设置，明确供应商为小智官方平台。新增独立 `svc_setup` 启动模式，密码热点 15 分钟限时、固定 AP 接口 HTTP、Origin/nonce/CAS、防重复提交、Wi-Fi 成功取得 IP 后才保存；长按 BOOT 退出，菜单可重新进入，明确离线选择不反复弹出。分组更新保留其余服务/设备身份，兼容完整旧 namespace，并保持 USB 全量 provision 契约。正常模式允许仅 Wi-Fi，MQTT 可选，SNTP 与官方 WSS 不依赖 NEXUS。官方 TLS 配置仅保存绑定/WSS 信息，忽略供应商固件字段，不替换本项目固件。

`bash tools/ci/test_host.sh`、`PET_HOST_SANITIZE=1 bash tools/ci/test_host.sh`、`PET_HOST_SANITIZE=1 bash tools/ci/test_config_host.sh` 退出 0；真实 LVGL 的 `bash tools/ci/test_ui_host.sh` 10/10；契约/板型生成、四项 build-matrix 工具测试、20 组件依赖无环及 JavaScript 语法检查通过。新增 boot 公开接口覆盖无配置/编译期 Wi-Fi/显式离线/损坏记录/异步请求失败重试；策略覆盖 Host/Origin/session 与 Wi-Fi 边界，实际响应解析覆盖绑定码、WSS 与供应商固件忽略。macOS ASan 编译首次将第三方 cJSON 的弃用 `sprintf` 警告提升为错误，改为只对该第三方对象使用既有屏蔽方式后重跑通过；没有修改第三方源码或整体关闭警告。新增模块首次 SDK 构建的 const 参数和 FreeRTOS 头文件顺序问题已修正，实际 ESP-IDF v6.1 构建退出 0。

浏览器使用实际 HTML 与本机模拟 API，验证 390px 手机布局、密码失败重试、特殊 SSID 无注入、绑定码/处理中/退出，以及服务端已保存但 HTTP 确认丢失时由新 revision 清除旧错误。成功后按钮改为“语音配置已保存”，避免反复请求绑定。临时浏览器任务空间与本机服务已关闭；模拟检查不替代设备或官方平台。

首次应用 3,187,104 B、SHA256 `444e3e26784d45455d1cf6a84911a6e7f9fbaff3e495eb7e8c86fa9e707be7c2` 在 `0x20000` 写入并独立回读匹配；配套四份产物与已烧版本逐字节相同，不再次初始化 NVS。设备真实 AP/HTTP 启动后，用户用手机打开页面并确认家庭 Wi-Fi 已连接保存；USB 对应 revision 1、NVS 失败 0。官方请求随后直接取得合法 WSS 信息并保存，revision 4、`voiceSaved=true`、错误 0；没有显示新绑定码，不伪称用户重新完成了平台添加。退出后设备从保存凭据连接家庭 Wi-Fi，41 秒时 SNTP 成功，AI/WSS ready 且 networkReady=true，无音频故障。随后用户首次发起真实对话，设备明确报告 `pet_xz_wss` 栈溢出并重启，用户界面提示暂时无法对话；单列 HW-07 修复，不把配置保存和网络就绪当作真实语音通过。原密码、WSS token、备份、串口日志与固件仅留忽略目录。

HW-06 推送与远程验证：`317ad8f902c70412020b88d09391e9b2e79ffeb5` 已推送并核对 origin/main；[43730473137](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43730473137) 七任务均成功（default/full/off/tones/duplex、voice-bridge-preparation、server-delivery-tools）。这份 CI 对应热点工作项，后续 HW-07 内存及语音修复另行核验。


HW-07（2026-11-15）：首次真实语音已经超出主机替身边界，先后确认三处资源/时序问题。原 WSS 拥有者栈 8,192 B，在证书验证后报告 `A stack overflow in task pet_xz_wss` 并重启；锁定音频 SDK 的文档预算不含栈，按其建议为同步编解码拥有者预留 40 KiB，并增加 `ownerStackFree` 实际水位。扩大栈后不再溢出，但 `mbedtls_ssl_setup` 返回 `-0x008D`，当前 SDK 定义为内存不足；当时片内可用约 27 KiB、最大连续块约 18 KiB，TLS 默认只从片内分配。N16R8 默认改为 `MBEDTLS_EXTERNAL_MEM_ALLOC=y`，TLS 动态缓冲使用 PSRAM，I²S/任务栈继续保留片内内存，证书/主机名验证不放宽。只修改隔离硬件 SDKCONFIG，不覆盖仓库现有用户配置。

取得真实识别文字后仍失败，不能按界面残留处理。临时只记录状态/枚举/耗时/包长的 `[DEBUG-hw07]` 探针确认另一轮在发送阶段出现 `transport_poll_write(0)`，SDK 返回短写并主动关闭连接；原 200 ms 公网写窗口过短。先增加 300 ms 可写背压的公开 WSS 回归，原实现退出 134，再将单次发送预算改为 1 秒；失败仍关闭整轮，绝不重放已消费 PCM。独立最小回归还复现 `tts.start` 后首音延迟 1.2 秒时，旧实现过早启动 A4 的 500 ms 播放停滞判定。现在先解码首包 PCM，再投递 `RESPONSE_BEGIN`；此前保持 THINKING，沿用从输入结束起算的 15 秒回复期限。新增完全无首包时仍按原期限超时/关闭的检查，不延长实际 DMA 停滞、队列背压或隐私取消预算。主机第二条复现也先退出 134，再经修正通过；实机没有单独证明所有历史失败都由首包延迟造成。

清除全部临时源码探针后，`IDF_PATH=/Users/lywq/esp/v6.1/esp-idf PET_HOST_SANITIZE=1 bash tools/ci/test_ai_wss_host.sh` 退出 0，包含新增发送背压、首音延迟与无首音超时，以及原故障/断网/隐私等场景。`python3 tools/build_matrix.py --profile default --build-root firmware/build-matrix-hw07 --jobs 4 --execute` 退出 0，实际默认应用 2,940,000 B、SHA256 `4fbe9b8cb9daee5521c9798dde03973a933b21c2944d4893d5f814d6f55338b3`；来源、配置和产物证据保存在该忽略目录。维护配置的 ESP-IDF v6.1 完整重编译及最终增量构建均退出 0；与当前方案无关的宿主/外围实现未改动。

最终维护应用 3,187,632 B、SHA256 `2d5dd4a3af043126ed63786300c9a1a326f81d1f23d524ba58f856913c196eed`，只在 `0x20000` 写入并独立 `verify-flash` 摘要匹配；配套 bootloader/分区表/OTA 初值/模型逐字节相同，未再次初始化 NVS。Wi-Fi、小智 WSS、设备身份及成长存档保留。用户最终明确确认“回答正常，结束后没有错误”；对应同一 USB 会话：上行 32 包、下行 73 包、真实提交 105,120 个 24 kHz 采样（约 4.38 秒），A4 回到 IDLE、reason=DRAINED；WSS error/cleanupError、音频 error/cleanupError/underruns、MCP failures 与 NVS failures 均为 0。任务栈最小剩余 16,404 B。启动阶段显示帧预算超限计数为 1，后续未增加，没有把它写成全系统 0 错误。

这只证明所记录的一轮真实云端识别/回答/播放结束与用户听感，不证明连续多轮、24h/7d、AEC/声学打断、公开记忆采用、掉电或 OTA 已通过。私有日志/备份/配置/固件均留本机忽略目录，诊断输出只增加状态/原因/轮次、播放采样数与栈余量，不输出语音文本或凭据。

HW-07 推送：`5aecf0b3d3e3f2574088fa98f073e1e5c17d4131` 已正常推送并核对 origin/main。对应 [43732294876](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43732294876) 七任务均已成功；不沿用 HW-06 的 CI 成功结论。后续设备经正常软件重启进入网络设置，原始 USB 响应仍为 revision 4、Wi-Fi/voice 保存标记均为 true，无 panic；这不追加为一轮对话或长稳验收。


HW-07 补查：[43732294876](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43732294876) 已完成，default/full/off/tones/duplex、voice-bridge-preparation、server-delivery-tools 七任务全部成功。该结论只对应 `5aecf0b`，不覆盖后续 HW-08 的默认分配策略修改。

HW-08（2026-11-16）：连续语音存在两类失败。修改前已捕捉 `esp-aes: Failed to allocate memory`→TLS `-0x0084`→发送 -132，连接开始时片内可用 12,523 B、最大块 7,680 B，无栈溢出/HAL 错误。用户描述为回答中途失败，但捕捉到的这一轮尚无新下行包，二者具体时刻不能直接等同。当前 SDK 的 S3 硬件 AES 对非对齐 PSRAM 缓冲仍需片内 DMA 临时内存；仅让 TLS 使用 PSRAM 不足以保证连续连接。

最终默认采用 `SPIRAM_MALLOC_ALWAYSINTERNAL=4096` 与 `COMPILER_OPTIMIZATION_PERF=y`（`-O2`），为显式片内/DMA 分配留余量并满足录音/显示实时预算；增加纯数值诊断 `dmaFree/dmaLargest`。语音页先确定最终文案，只有文字或颜色变化时才提交 LVGL，避免每次轮询重绘和菜单标题二次覆盖。40 KiB WSS 栈、证书/主机名验证、断言、100 ms 输入/显示预算及播放/隐私保护保持原值，没有通过吞错或延长时限凑通过。`-O2` 暴露旧温湿度文本槽的 `-Werror=format-truncation`，配套扩大温度/湿度/趋势的内部字符缓冲；没有屏蔽警告或改变传感器含义。

中间方案未通过：1 KiB 阈值虽增加待机余量，但显示帧耗时 103–113 ms，触发“显示不健康→隐私中断→取消录音”，用户表现为按住说话返回主页，USB 未重启。4 KiB 与缺省 `-Og` 仍出现最大 119 ms 的帧和第二轮起 INPUT_GAP。`-O2` 对照版本用户确认三轮完整回答；设备对应三轮 IDLE/DRAINED、384,480 个 24 kHz 播放采样、显示失败 0、最大 96 ms；采样所见片内最少 34,695 B、连续 DMA 最少 24,576 B，WSS 栈最少剩余 16,316 B。慢启动/输入间隔探针未触发，支持实时预算问题，但未单独测得旧版本的具体抢占者。全部 `[DEBUG-hw08]` 已清理，`ai_agent.c` 无最终改动；最终版本连续回归另记。

新增真实 LVGL 回归验证相同模型/时间/菜单再次刷新不发生屏幕传输；原实现断言失败，修复后普通与 ASan/UBSan 的 `bash tools/ci/test_ui_host.sh` 均为 10/10。`bash tools/ci/test_host.sh`、`python3 tools/ci/test_build_matrix.py`（4 项）、`python3 tools/ci/check_contracts.py` 与 `python3 tools/ci/check_deps.py` 均退出 0，20 组件无环。另一种“播放间隔”临时用例只复现旧 500 ms 行为，不能解释 AES 日志，已撤销，不据此放宽播放时限。

最终维护构建退出 0，应用 3,203,920 B、SHA256 `c33439498cf1850c194632c341ea27a6a5e1fd1b5a0c1a63d6ecfe9ad1a136fd`；只写 `0x20000`，独立 `verify-flash` 匹配，bootloader/分区表/OTA 初值/模型逐字节未变，未写 NVS。实际 SDKCONFIG 核对 4 KiB、`-O2`、断言与 TLS 外部内存。隔离 default 命令 `python3 tools/build_matrix.py --profile default --build-root firmware/build-matrix-hw08c --jobs 2 --execute` 退出 0，应用 2,955,440 B、SHA256 `1da24c550101fdc70f1b045bde23e155633887b00b21ae3ff4cc5e32dde2d135`；来源与产物清单逐项匹配当前文件。编译期间仍有改动的首次来源校验正确拒绝，固定后重跑通过，不采用被拒绝的证据。私有原始日志/备份/配置/固件均留忽略目录，不进入仓库。

最终去探针版本实机：用户明确确认“5 轮全部正常，无跳主页或错误”；同一连续 USB 会话实际记录 7 轮 IDLE/DRAINED，258 包上行/1,384 包下行，共提交 1,992,960 个 24 kHz 采样（约 83.04 秒）。显示失败 0、最大帧 100 ms；采样所见片内最少 33,519 B、连续 DMA 最少 21,504 B，WSS 栈最少剩余 16,316 B。WSS error/cleanupError、音频 error、MCP/NVS failures 均为 0；较长回答期间播放缓冲欠载计数 24，零填充后仍正常完成，不写成音频零欠载。端到端识别/回答/播放结束与用户听感已覆盖；不据此宣称 24h/7d、所有公网抖动、AEC/声学打断、掉电或 OTA 通过。

HW-08 推送：`9bb1b37a01f7286ef43dfaa1ed0714ecedfba5f0` 已正常推送，并通过 `git ls-remote origin refs/heads/main` 核对远程包含该提交。对应 [43735670225](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43735670225) 当前运行中，不将既往 CI 成功沿用到本次 `-O2`/分配/绘制变更。USB 连续采样已关闭、端口释放，最后为 IDLE/DRAINED，设备继续独立运行。

HW-08 CI 补查：[43735670225](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43735670225) 七任务均已成功，仍不替代待机长稳；用户随后反馈待机进入恢复，单列 HW-09，不将短时间连续语音通过扩张为整机稳定。

HW-09（2026-11-16）：用户确认无人操作待机自动进入恢复模式。初次 USB 只读诊断确认 `active=true/failures=3`，运行 ELF SHA256 `94f55d9e0e889d2688b5913f412b573b65b236831f4fee6c15f68287b776121d` 与已烧维护 ELF 完全匹配；该维护配置关闭持久 coredump，没有事前转储。保存原 ELF/日志后，通过原 `PET_RECOVERY_CLEAR` 仅清 RTC 恢复标记并受控重启，不清空 NVS。持续 USB 监测复现三次 `IllegalInstruction`，PC `0x4204f88d`、回溯 `0x4204f88a`；使用匹配 ELF 的 addr2line/objdump 定位为 `event_bus_task` 函数尾部返回，随后再次锁入恢复。

最小主机复现使用真实 `event_bus.c` 与 SDK 边界替身：发布多个不同优先级事件，处理完后应继续阻塞等待，旧实现在残留 Queue Set 成员上读到空队列并返回，断言退出 134。当前 SDK 文档明确要求先 `select` 返回某成员再读取该成员，不能直接遍历读成员后保留其通知。改为成功入队后唤醒专属任务，数据仅取自原四条队列，空/合并通知重新扫描，不让分发任务返回。回归覆盖 500 轮空闲再唤醒、空扫描与等待之间发布、优先级/FIFO、32 条容量与各优先级满队列计数；普通全量主机检查已退出 0。

`PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_host.sh` 全量检查退出 0，包含新增真实分发器回归；契约与 20 组件依赖无环检查通过。维护 SDK 构建退出 0，应用 3,203,680 B、SHA256 `64a9514932d0ac3308a209b01eb1eb4100af38cf19ee96d3dbf2040654a9a52d`，ELF SHA256 `59761e347361387df2886b294b1c3b11cc36c15beeff37cea3d4516e49c46451`。原恢复标记通过现有维护命令解除后，仅写 `0x20000`，独立回读摘要匹配；bootloader/分区表/OTA 初值/模型仍与已烧基线逐字节一致，没有擦除或写入 NVS 配置。

隔离 default 构建 `python3 tools/build_matrix.py --profile default --build-root firmware/build-matrix-hw09 --jobs 3 --execute` 退出 0，应用 2,955,200 B、SHA256 `47454e4a449c4d89c573a30fd6c16731a408e725f4c3861005c1c942e7e0a335`；来源与实际产物逐项复核匹配。修正版首次启动通过 30 秒连续健康窗口，恢复 `active=false/healthy=true/failures=0`；同一串口会话继续观察至少 5 分钟，不把短启动成功写成长期稳定。

实机验收：用户明确确认“主页正常，3 轮完整回答”；同一 USB 会话自动检查至 uptime 300,178 ms（481 次诊断采样），期间 0 panic、0 非预期重启，恢复持续 `active=false/healthy=true/failures=0`，已记录 5 轮 IDLE/DRAINED；显示失败 0、最大帧 94 ms，WSS/音频 error 为 0。较长回答期间播放缓冲欠载计数为 2，未把它写成全音频零欠载。只证明本次 5 分钟运行、待机和已记录轮次；不替代 24h/7d、掉电、实际 BOOT 双长按恢复或 OTA 回滚。原恢复流程未更改，使用 USB 清 RTC 标记后启动修正版，没有用关闭恢复保护绕过问题。

HW-09 推送：`6f2cb31a509ab3437baafd50e695eaa10b1d8044` 已正常推送，并通过 `git ls-remote origin refs/heads/main` 核对远程分支包含该提交。对应 [43739157271](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43739157271) 当前运行中；不把 HW-08 已成功的 CI 沿用到本项事件总线修改。

最终采样在 uptime 372,237 ms、IDLE/DRAINED 后关闭并释放串口；同一会话共记录 8 轮完整结束，628 包上行/1,080 包下行、1,555,200 个 24 kHz 播放采样（64.8 秒）。0 panic/非预期重启/显示失败，恢复锁与失败计数均为 0；显示最大帧 94 ms，WSS error/cleanupError、音频 error、MCP/NVS failures 均为 0。播放缓冲欠载累计 20，较长回答仍完整结束；采样所见片内最少 22,371 B、连续 DMA 最少 12,800 B，WSS 栈最少剩余 16,296 B。设备继续独立运行，原始日志/ELF/固件仅留忽略目录。远程 CI 七项中两项已通过，五个固件预设仍在构建，本次交付不宣称远程 CI 已完成。

HW-10（2026-11-16）：基础硬件“小爱同学”语音唤醒已实现并完成初步实机验收，后续待机/远程结果继续记录。用户确认最新版本两轮人声唤醒与一轮 BOOT 按住说话全部完整回答，无闪屏/卡住；独立的电脑播词连续三轮也全部通过，三个轮次记录 `WAKE→SILENCE→RESPONSE→DRAINED`，无需按键。以下事实分别标明自动记录与人工确认，不把短测写作 24h/7d 稳定性。

实现与根因：

- 新增 `voice` 预设，只启用基础硬件、小智开机服务、固定 `wn9_xiaoaitongxue` 与 `vadnet1_medium`、维护/恢复；扩展、DSP/AEC、OTA 关闭。PTT、静音/DND、播放停采集与原 100 ms 显示/断流保护保留；加入独立构建和 CI。
- 首次实机连续三次 `LoadProhibited`，匹配 ELF 定位 `dl_convq_queue_bzero→WakeNet model_clean→reset_frontend`，EXCVADDR `0x10`；首次 reset 及已推理后的 reset 均能触发。固定 SDK 的 clean 不再调用，reset 清残帧/VAD 标签并使旧历史失效，下次空闲检测前用公开 destroy/create 重建；重建失败则停止检测并释放。同步重建会拖慢唤醒后的录音窗口，已移至空闲准备阶段。
- 加模型后内部堆约 17 KB、DMA 最大块不足 2 KB；`voice` 使用 512 B 内部优先阈值，A4 PCM/文本与 WSS 报文暂存移到 PSRAM，任务栈/DMA 保持内部内存。显示/模型共享缓存导致预算争用，`voice` 使用 32 KiB 指令缓存、64 B 数据缓存行，数据缓存保持 32 KiB；依据[官方性能指南](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html)，仍以实测为验收。
- 分核与内部 9,600 B 绘图缓冲实验均未通过，分别见过 437 ms 和 113 ms，已全部撤回；没有通过提高 100 ms 阈值或关闭保护掩盖问题。临时耗时探针已移除。

软件验证：

- `python3 tools/ci/test_build_matrix.py`：新 voice CLI 先因不支持预设失败，修改后 5 项通过；默认计划不写文件、不烧录、不需要 OTA 钥。
- `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_sr_frontend_host.sh`：旧 clean 边界回归先退出 134，修正后通过；覆盖新建后 reset、模式/残帧、恢复空闲重建、重建失败后封闭失败与资源回收。真实 SDK 头文件，推理边界替身，不能替代上板。
- `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_host.sh`、`IDF_PATH=/Users/lywq/esp/v6.1/esp-idf PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_ai_wss_host.sh`、`bash tools/ci/test_ui_host.sh` 均退出 0，LVGL 10 项通过；契约 14 schemas/1 profile/12 examples/2 extensions 与依赖 20 components 检查通过。
- ESP-IDF v6.1 `python3 tools/build_matrix.py --profile voice --build-root firmware/build-matrix-hw10d --execute --jobs 2` 退出 0，源码清单/产物摘要已独立重算匹配。default 回归也退出 0；本轮没有在本机重复所有六预设，远程 CI 单独记录。构建为未签名测试产物。

实机交付：

- 已保留原 PTT 应用/ELF/sdkconfig，实际只读备份整个 1 MiB 模型分区，SHA256 `60d8edcc64359ba1347e9419ba515357f78e707098646ffe3086cba0a1190a9e`；不读取/导出凭据。生成包、日志和备份仅在忽略目录。
- 最终应用 3,548,528 B、SHA256 `a5a943a98f5776a1d8a84f22b83e3b311187dda61cc071c1ec486e252bc4822c`，匹配 ELF SHA256 `71c649b3566e6a3335ce6c5ce70f67a778f454c4e2d631d9bf1c571150473421`；双模型 579,215 B、SHA256 `8f859601e0873438fa57553e648f25db5d64ce3ec2ba966a707e7d27ef8cd5cb`，应用 `0x20000` 与模型 `0xe20000` 独立回读匹配，烧录未覆盖 NVS，原 Wi-Fi/小智/成长保留。
- 同一 USB 会话电脑播词 3 轮（turn 2/3/4）均通过，显示失败/播放欠载 0，最大显示 76 ms、前端回调 44 ms；没有用录音文件或模拟成功响应替代真实设备收发。最后一轮结束时 227 包上行/768 包下行，用户另确认两轮人声唤醒+一轮 BOOT 均正常；USB 实际观测轮次与用户确认分别记录。
- 启动未完成联网对时时曾触发一次失败，因此音频最近错误保留 `-1`，不声称所有启动阶段 error 为 0；完成对时后自动三轮 WSS error/cleanupError/MCP failures 为 0。使用说明明确等首页时间正常后再对话。当前半双工，无播放中声学打断；长距离识别率、物理静音延迟、OTA/掉电与 24h/7d 未验收。

前置 HW-09 的 [43739157271](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43739157271) 已核对七个 job 全部成功；不沿用为 HW-10 证据。HW-10 的最终待机、推送与远程 CI 结果见下文。

HW-10 最终记录：同一 USB 会话观察至 uptime 317,023 ms（491 次诊断采样），0 panic/非预期重启、0 显示失败、0 播放欠载；恢复 `active=false/healthy=true/failures=0`，前端 13,383 次处理、最大 44 ms，显示最大 76 ms。采样所见内部空闲最少 12,383 B、连续 DMA 最大块最少 3,072 B；A4 栈最少剩余 4,412 B、WSS 栈 18,788 B。自动记录 3 个 WAKE/DRAINED 轮次和 1,105,920 个真实播放采样，用户另外确认人声与 PTT 三轮正常。仅证明本次约五分钟及已测试轮次；不能替代长稳或识别率验收。IDLE/DRAINED 后关闭监视器，`lsof /dev/cu.usbmodem1201` 无占用，设备继续独立运行。

实现提交 `3eacd00c07db527e2aa663cdd6897f11aa7ff5ce` 已正常推送，并以 `git ls-remote origin refs/heads/main` 核对远程包含该提交。对应 [43744450764](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43744450764) 已核对完成，conclusion 为 success：`server-delivery-tools`、`voice-bridge-preparation` 与 default/voice/full/off/tones/duplex 六个固件 job 共 8 项全部成功；除 `full` 外五个固件 job 的 “Host, protocol, DSP and LVGL checks” 步骤按工作流 `if: matrix.profile == 'full'` 跳过，非失败。该提交无后续重跑。远程 CI 只证明该提交的构建与主机检查，不替代长稳、识别率、OTA/掉电与实机验收。


## CONSOLE-14 · 管理台界面统一（2026-11-16）

按用户确认的 Naive UI 方案落地：CORE 固定 Naive UI 2.45.3 / `@lucide/vue` 1.73.0，三组导航与手机抽屉、统一按钮/主题、登录与消息输入、设备状态与接收开关；消息筛选/导出分层，保留原字段和结果未知时的原身份重试。`ConsoleShell`/`ConsoleNavigation` 与导航定义独立，API/待确认提交仍由 App 管理。静态 HTML 每次生成独立 CSP 样式 nonce，无 unsafe-inline 或外部脚本放行。

`python3 tools/run_local.py build` 退出 0：118 条中 113 通过、5 条可选环境跳过，新增静态页 nonce/缓存及原资源策略 2 项通过；CLI 的 `python3 -m unittest discover -s tools -p 'test_*.py'` 11 项通过。真实 Vue/回环 RAM HTTP 的原 19 组页面共 750 次检查通过；新增 `bash tools/ci/test_console_theme_ui.sh` 21 次检查通过，含实际 CSP、独立 nonce、十二导航、390px 抽屉与导出范围、Escape/外部点击、重复开关与焦点返回。已查看消息、设备、概览和手机导航的实际截图；样例数据不代表真实设备状态。

迁移后部分脚本曾命中 Naive 按钮内部 span，而非分页计数；改为直接分页文字/稳定导航身份并完整重跑失败专项，保留全部原业务断言。手机导航新增检查发现抽屉自身焦点恢复覆盖目标标题，改为等待组件卸载后的 nextTick，再转移焦点；该检查已通过。补充实际 CSP 到既有公共浏览器夹具，不以无 CSP 页面代替正式样式验收。

最终 `npm run check` / `npm run build` 通过；清理生成的旧静态资源后重新执行 Maven package，最终 JAR 静态资源及迁移与源码逐字节核对，结果与提交记录见下。没有生产迁移、消息、设备操作或目标机部署，旧镜像和离线包保持原快照；剩余用户视觉验收及真实环境验收不计作完成。


CONSOLE-14 交付核对：CORE `9dc7ad9717dbf9c44a1bef7d3ad952c339aeefe0` 已正常推送并确认包含于 `origin/codex/core-baseline`；04 配套为本项文档/公共浏览器夹具/主题专项及既有脚本的语义定位调整。最终 JAR SHA-256 `a7f1e04d74322f3634c37d0bb8aa5766586bebd62a6088eff2229d8ce8b10d07`，三份 Vue 资源与 20 份迁移逐字节一致；补充独立静态页策略 2 项再次通过。首次远程 [CORE Docker CI 43799072343](https://github.com/liuyiwuqing/nexus-core/actions/runs/43799072343) 正在运行，其结果独立于上述本地验证；04 CI 随配套提交触发。未发布新镜像、未更新固定的 1Panel 镜像标签或部署服务器。

## CONSOLE-15 · 管理台 UI 细节优化（2026-11-16）

按用户要求先梳理问题再定优化项，保持现有功能与整体设计方向。诊断基于真实构建产物 + 环回 RAM 夹具（不接数据库/Broker/设备），取 1440×900、1024 与 390×844 三档真视口并采网络条目。主要问题：消息双栏高度随列表涨到 3482px、分页在 y≈3900、空详情提示垂直居中到 y≈2000；移动端投递表 540px min-width 把操作列推出视口；未知错误直接显示 `请求失败（HTTP 404）`；分隔线 5 套色、控件描边 2 套、字号 15 种、圆角 11 种；`dl` 两端对齐在 800px 宽下产出 400~850px 空隙；Naive 默认焦点环仅 1.24:1；危险操作与普通提交同色；单 chunk 470.20 kB，停在概览页 16 秒发出 37 次 API 请求。

首批只做「视觉基线 + 布局硬伤」，不动请求时序与模板契约：`style.css` 建立 token 层并由 `theme.ts` 逐值镜像（颜色/圆角/9 档字阶/描边/不透明焦点环/层级），消息双栏定高并各自滚动、分页吸底，≤760 以 `data-label` + `:has()` 为开关把表格转卡片行（未标注表格回退横向滚动），`dl` 改两列网格、指标数字收敛，5 处删除/撤销/轮换确认改 `type="error"`，`Select`/`DatePicker` 走 peers 而非空操作的顶层键。

实测对照（1440×900，33 条消息）：页面 3956→928px，分页 y≈3900→844（首屏），空详情提示回到视口内；分隔线 5→1，硬编码 hex（除装饰）40+→0，圆角 11→5，字号 15→9（+2 专职）；390 投递表 540→356px 且操作列可见；CSS 21.66→26.60 kB、JS 470.20→471.07 kB。`npm run check` / `npm run build` 退出 0；浏览器复跑十二导航/三分组/图标/导出折叠与 Escape 归焦/设备卡片标签/`data-metric`/390 无横向溢出均通过。

未做并记入待办：错误文案映射与反馈吸顶、`StatusNote` 统一、按页请求与 `defineAsyncComponent` 分割、设备设置面板内联、OTA 安装门槛、token 守卫脚本、空挂类清理。理由分别是：涉及 `mutate()` 未确认分支（被 750 次跨仓断言覆盖）、属模板契约变更、或会给 build 增加失败面，需单独立项回归。未执行 `run_local.py build`（需独立 PostgreSQL/MQTT 与 Maven）、远程 CI、部署与实机视觉终验。

CORE 提交 `b3150bc` 已推送，`origin/codex/core-baseline` 已确认包含；诊断与规范见 CORE `docs/console-ui-refinement.md`、`docs/console-ui.md`。

CONSOLE-15 追加（CORE `004c2db`，已推送）：三张表格容器补 `tabindex="0" role="region"` 与作用域 `aria-label`，键盘可进入横向滚动区；投递与操作审计两处实测生效（`aria-label` 分别为"投递记录/操作审计…"，焦点落在 `.table-shell`），"最近执行"表因夹具无执行记录未渲染，属性随源码与 `vue-tsc`/构建通过。文档补两条约定：抽屉关闭后 `focusContent()` 只认 `.content h1`，不得放宽到 `h2`；`.section-heading` 不叠加 `:focus-visible` 轮廓。下一轮建议加"12 页 `h1` 文本指纹"断言作为兜底。OTA 安装按钮显性化目标与"完成并结算"二次确认、空挂类清理分别留在 P1/P2，按用户的"不为一次性改动增加机制"原则不并入本批。

## CONSOLE-16 · 按页请求与代码分割（2026-11-16）

推进 CONSOLE-15 未落地的 O12/O13。刷新改为三档：轮询/可见/切页/操作回读走当前标签页的 `tabResources`；SSE `invalidate` 在其上补 `devices`（顶栏"设备视图"是外壳级依赖，事件不说明变了什么）；顶栏刷新、错误重试、登录走全量。`loading` 只在实际取数据时置位，纯 `status` 探测不再让顶栏每 5 秒闪"刷新中"；筛选/搜索守卫从 `loading || busy || exporting` 收窄为 `busy || exporting`，竞态统一交给 `readGeneration`。

面板改为按需加载：`src/panels.ts` 集中 14 个加载器，`asyncPanel()` 统一产出 `defineAsyncComponent`（`delay:200` 内不闪骨架、`timeout:20000` 给失败态），`App.vue` 只同步保留外壳与默认页；导航项 `@pointerenter`/`@focus` 做意图预取，`watch(tab)` 兜底；新增 `PanelSkeleton` 骨架；`focusContent()` 改为有限次轮询等 `.content h1`（异步挂载晚于一次 `nextTick`，否则"焦点进入 h1"会静默失效）。

实测（概览夹具，单钩子统计 `fetch`，16 秒窗口）：概览页 refresh 请求 15→3（每 tick 5→1，仅 status）；消息页 20→8（5→2）；投递 15→6；设备 20→8。构建产物 CSS 26.70 kB、首屏 JS 181.62 kB + 共享 Button 142.26 kB（471.07→323.88，−31%），其余 14 个按页 chunk 2.8–49.3 kB。

面板异步化后"点导航→下一句找按钮"不再成立，改了三处等待方式、断言内容不变：20 个 UI 脚本的 `expect()` 改为 `js(expr).catch(()=>false)`（元素未渲染时 `.find(...).disabled` 会抛 `TypeError` 直接中止脚本）；3 个脚本的单发 `tap()` 改有限次重试；`test_roles_ui.sh` 的 `nav()` 加"页面已挂载"门（否则"只读角色看不到某按钮"的否定断言会假通过）。回归 20 个控制台脚本 / 779 条断言全部通过。

取舍：非设备类页的设备下拉依赖 SSE 失效或进入设备页才更新（下拉只显示设备名，不含在线态）；不采用"登录后预取全部 chunk"，以免抵消分割收益。

## CONSOLE-17 · P1 收尾：错误文案、反馈吸顶、提示统一、面板内联、OTA 目标、token 守卫（2026-11-16）

按用户"每完成一项即提交、自动推进下一步"的要求，逐项落地并各自提交：

**O9 失败文案（CORE `cb21afa`）**。新增 `src/failure.ts`：`failureText()` 对未登记错误码给出可照着做的中文，并把 `HTTP 状态 · 后端码` 留在括号里便于报障；`readFailure()` 把 fetch 的 `TypeError`（原本直接把 `Failed to fetch` 甩给用户）换成"无法连接 CORE"。11 处面板 catch 改用它。已登记错误码文案保持原样，角色/筛选/导出提示不受影响。

**O10 反馈吸顶（CORE `a998c38`）**。`.feedback` 改为 sticky 到工具栏底缘（新增 `--toolbar-h:62px`，桌面与 390 实测均为 62），静止位置本就在吸顶位之下，所以不改变现有布局；实测两种视口下与工具栏底缘贴合、无重叠、无横向溢出。

**O11 提示统一（CORE `f8193a3`）**。新增 `StatusNote.vue`（`info`/`warn`/`danger` + `#actions` 插槽，根元素保留 `.scope-note` 钩子），改造 9 个面板 11 处：消息未确认此前用红色 `.inline-error`（它是警告不是错误）、权限/路由/模板是裸 `<p>` 无任何样式、其余 6 处是 12px 灰字脚注带重试按钮。现统一为带底色与圆点的状态带。原计划的五态收敛为三态：busy/action 没有独立视觉职责。

**O14 面板内联（CORE `96bb5a0`）**。设置与 OTA 面板原本挂在整张网格之后，第 1 张卡片点开要在几屏外找结果；现渲染进所属 `<article>`，卡片撑满整行（`grid-column:1/-1`），嵌套面板去外框改分隔线衔接，关闭时把焦点还给触发按钮。附带修掉 `.management-panel` 焦点环用 UA 默认色（桌面蓝、移动琥珀）改为 `var(--focus)`，以及 ≤560px 标题被压成断字的问题。

**O15 OTA 目标（CORE `4d84ec7`）**。安装按钮从"安装版本"改为"安装 <版本>"，页脚写明"将安装到 <设备>"。设备名不进按钮：面板标题已写明，长名字会撑破不换行的按钮。

**O16 token 守卫（CORE `3148ede`，04 `89327ed` 同批）**。新增 `scripts/check-tokens.mjs` 并接入 `npm run check`：hex 只许出现在 `:root`、组件 `<style>` 不许有 hex、`var(--x)` 必须已定义、定义过的 token 必须有人用。最后一条当场找出 5 个死 token 并删除（现 53 个）。用注入的 `#dce5e0` 与 `var(--text-xl)` 做过负例验证。**O17 结论为不改**：`.primary/.secondary/.text-button/.compact` 保持零字节，视觉由 Naive 决定、跨仓断言以它们为选择器。

回归：20 个控制台脚本 / 780 条断言全部通过（`test_remote_ota_ui.sh` 由 22 增至 23，新增"按钮自报版本、页脚自报设备"一条）。未执行 `run_local.py build`、远程 CI、部署与实机验收。

## CONSOLE-18 · 只读评审驱动的可达性与视觉收口（2026-11-16）

两轮只读评审（交互侧 + 视觉侧，1440×900 / 390×844 实测，另补 559/560/600/700/899/900/1000/1100 边界）指出 5 条必须修、7 条建议修。按"每完成一项即提交"逐条落地。

**可达性（CORE `47ef9ef`）**。三条同源：面板由脚本拉开，但焦点与播报没跟着走。①`StatusNote` 暴露 `focus()`，失败分支 `await nextTick(); note.value?.focus()` —— 提交期间按钮 `disabled` 会把焦点丢到 `body`，键盘用户得从头 Tab 才知道发生了什么；②`warn`/`danger` 改 `role="alert"` + `aria-atomic`，这档提示只在失败那刻插入，`polite` 会排在"正在加载"之后被吞；③内联面板 `@keydown.esc` 关闭，设置与升级互斥、再点收起（同开时 Tab 从升级按钮串进设置面板，卡内出现两组同名按钮）。另修：`.management-panel` 焦点环从常驻改 `:focus`（常驻会把内联面板框成"第二个盒子"）、`html { scroll-padding-top }`、`staleDraft` 从成功绿的 `.feedback` 改 `tone="warn"`。04 侧同步 `test_operations_ui.sh` / `test_automation_edit_ui.sh` / `test_message_compose_ui.sh` 的 role 断言（`563d6c3`），其中 390px 那条原来只查 `!!querySelector('[role=status]')`（有播报就算过），改成要求选中提示且文案正确。

**视觉（CORE `8de4e70`）**。①展开的设备卡撑满整行后宽 1148px，卡内三处 `space-between` 把「标签↔控件」拆到 1012px/993px，中间全是空白；`≥900px` 改两栏（左 `minmax(260px,34%)`、右 `minmax(0,1fr)`、`column-gap:32px`），实测回到 287px/268px，左/间距/右 = 373/32/693 精确闭合。②嵌套面板 h2 与卡片标题同为 16px，层级只靠 1px 分隔线扛不住；降 14px 并去掉重复设备名。③≤560 的换行断点实测漏掉 561–760（这段 h2 仍被压成两行），改无断点 `min-width:18ch`，559 与 560 两档现在一致。④删 `.notice*` 五条死规则（与 `.status-note--warn` 逐值重复，0 个调用点）。⑤`--topbar-h` 窄屏 60→63（实测顶栏 63px，写 60 会压掉吸顶工具栏 3px 底边框）。⑥OTA 提交未知时红字与黄块说同一件事，改 `v-if="error && !pending"` 并把事实并进提示正文。

**两栏带出的两条新回归，都是评审在 900–1100 抓到、当批修掉（CORE `8514b1f`、`43f09b0`）**：①`.management-metrics` 写死 4 列，面板被压到 302–492px 后每列只剩 62px、固件版本断 3 行 → `repeat(auto-fit,minmax(140px,1fr))`；②单列表单最初写在 `min-width:900px` 里，对 1440 也生效（音量滑杆被拉成 693px、卡高 +143px）→ 收窄到 `900–1150`。

**守卫缺口**。改②时多写一个 `}`，`vue-tsc` 与 token 四条规则全过，只有 lightningcss 在 build 阶段报 `Invalid empty selector`。`check-tokens.mjs` 因此加规则 0（花括号必须成对），负例验证过会红。

回归：20 个脚本 / 780 条断言全绿，受本批直接影响并单独重跑的是 message_compose(33)、management(38)、remote_ota(23)、operations(96)、automation_edit(99)、growth_sync(18)。未执行 `run_local.py build`、远程 CI、部署与实机验收。

## CONSOLE-19 · 十二页 h1 文本指纹断言（2026-11-16，04 `517ec18`）

`App.vue` 的 `focusContent()` 按 `.content h1` 找落点：导航标题一旦回退成 `h2` 或文案漂移，焦点会静默落回 `body`，而 20 个脚本里没有一条会红。这是 `console-ui-refinement.md` 登记的「下一步值得做」第一条。

做在 `test_console_theme_ui.sh` 里（本仓没有脚本总登记处，新建脚本不会被任何入口跑到），插在「三组十二项」「SVG 图标族」「nonce 授权」之后、导出步骤之前：顺序点 12 个导航项，逐页断言 `.content h1` 文案等于期望值，且 `document.activeElement` 就是它。check 由 21 增至 33。

三个实现细节值得留档：

1. **点击活动节点而不是坐标**：导航按钮会随角色数据重渲染，坐标点击偶尔落到已脱离文档的旧节点（实测撞到一次，`h1` 停在「消息」8 秒没切过去）。
2. **循环后回 inbox 而不是 overview**：紧接着的 `.export-actions` 断言依赖消息页的 `ListSearchForm`（`export-actions` 只在这个组件里），而登录后的默认 tab 本来就是 `inbox` —— 回 overview 会改变后续步骤的起点。
3. **把焦点交还 `body`**：循环结束时焦点按设计停在 `h1` 上，而原脚本跑到导出步骤时是 `body`。不还原的话 `Escape` 落在 `h1` 而不是 `<details>` 里的 summary，`Escape closes exports and returns focus` 会红 —— 不是断言过期，是插入改变了前置焦点状态。

负例验证：把「运行概览」临时改成「概览」，报 `page overview heading/focus: h1="运行概览" active=H1` 并退出 1；回填后连跑 4 次、每次 33 checks 全绿。原有 21 条一字未改。

累计：20 个脚本 / 792 条断言。

## CONSOLE-20 · 点掉"未确认"提示后的焦点去向（2026-11-16，CORE `437c558`、04 `7750f04`）

上一轮只登记没做的 P2-7。三处提示上的动作按钮会把提示连同自己一起卸载，焦点落到 `body`；这比消息页那处更糟 —— 那里表单还在，这里连上下文都没了。落点按动作语义选，不搞统一套路：权限页「已核对，开始新的签发」→ 名称输入框、来源页「放弃重试并只读核对」→ 本区标题、模板页「关闭模板草稿」→ 重开入口「创建模板」。只在用户主动点掉时转移，轮询让提示自然消失时不抢焦点。

`NButton` 不暴露 `focus()`（`@vue-ignore` 的类型里只有 `focusable`），改成给按钮一个 id、`nextTick` 后按 id 取 DOM —— 与设备页 `device-manage-<id>` 同一做法。

同时给 `roles_ui_fixture` 补了写失败通道（`next_write=drop_after` 写后断连、`error` 走 503）：这个夹具此前没有任何签发失败注入点，`pendingIssue` 那条路径在跨仓断言里是空白 —— 也就是说"签发结果未确认"这个状态从来没被自动验证过。现在 roles 35→39、sources 34→38、templates 27→32，20 个脚本累计 805 条断言全绿。

## TELEMETRY-01 · 遥测历史 API 与采样口径（2026-11-16，CORE `377813e`）

设备管理此前只有 `management` 的实时快照，无法回答「过去几小时堆内存/音量/未读怎么变」。本轮只补 CORE 侧历史与查询，不改设备主题、遥测 Schema、ACL 或管理台：CORE 从既有 retained `telemetry` 追加样本，快照与历史分开写入。

- **采样**：`observedAt` 必须晚于该设备最新历史点；`firmwareVersion/configRevision/volume/brightness/muted/dnd/unread/audioFaulted` 变化即记（距上一点至少 5 秒），稳定值 30 秒最多一条。堆内存波动只随心跳进入历史。retained 重放、乱序和重复时间戳不写。
- **保留**：默认 30 天，每天 04:20 UTC 分批删除，只动样本表；启动校验 5–3600 秒 / 1–采样间隔 / 1–365 天。
- **接口**：`GET /api/v1/devices/{id}/telemetry?from&to&bucket`，`bucket` 为 `raw|1m|5m|15m|1h|1d`，默认最近 6 小时、5 分钟桶；窗口最大 30 天，`raw` 最大 12 小时且最多 2000 点（`truncated`），聚合桶最多约 2000 个。空桶不补零；最后一点不代表在线。字段聚合语义（last/avg/min/max/bool_or）与 400/404 见 CORE 文档。
- **验证**：`DeviceManagementFlowTest` 新增 2 项真实 HTTP/PostgreSQL/MQTT 用例（变化下限、心跳、原始/聚合、空窗口、401/404、四类 400；40 天样本清理）。完整构建 120 条中 115 通过、5 条条件跳过，V21 在隔离库应用成功。
- **未做**：图表/仪表盘页面、前端「实时 vs 快照」标识、真实库迁移、目标机部署、ESP32 长期采样/时钟回退/30 天规模与掉电验收。

远程 CI：CORE `377813e`/`4944710` 对应的 Docker deployment 未启动 job，GitHub 返回计费/额度限制（43874044017）；上一提交 `253e467` 同样如此。非代码失败，待额度恢复后补跑；本地完整构建与隔离 PostgreSQL/MQTT 结果不受影响。

## TELEMETRY-02 · 管理台状态历史与实时/历史标识（2026-11-16，CORE `83e025b`）

在 TELEMETRY-01 的只读历史接口上补管理台入口：设备「设置与状态」新增「状态历史」，1 小时/6 小时/24 小时/7 天分别查询 `1m/5m/15m/1h` 桶，用无依赖内联 SVG 画可用内存（实线 `--accent`）与启动以来最小内存（虚线 `--info`）。相邻点跨度超过 1.8 个桶就断线，缺失区间不补零、不用直线替设备补数据；悬停与左右方向键显示采样时间与可用/最小 KB，`role="img"` + `aria-label` 提供读屏摘要。历史每 60 秒刷新，切范围立即重查并清空旧点。

- **实时/历史区分**：快照徽标只读 `management` 的 `fresh`，文案改为「实时快照 / 快照已过期」并带 90 秒口径的原生 `title`；历史区块只写「来自设备已上报的遥测」，不出现「实时」，脚注说明最后一条不代表在线。概览页脚同步改为「数据库快照 … 页面每 5 秒重新读取」，避免把轮询说成设备刷新。
- **窄屏**：图表外层 `.chart-scroll` 横向滚动、SVG `min-width:560px`；390px 下文档无横向溢出，图内滚动承载可读字号。
- **验证**：`npm run check`（78 token，无散落 hex）与 `npm run build` 通过。04 夹具新增 `/telemetry` 端点（按 `bucket` 生成点、记录请求、支持空态），`test_management_ui.sh` 新增空态、图例/双序列、默认 `5m` 请求、切 24 小时请求 `15m`、悬停取值、历史不含「实时」及 390px 无溢出断言；完整 46 项浏览器断言通过。夹具时间解析同时修正为接受 `toISOString()` 的小数秒，避免历史接口整条失败。
- **未做**：图表仅展示可用内存与启动以来最小，未画音量/未读/音频指标；没有缩放、游标联动或导出；未迁移真实库、部署与实机长期采样/视觉终验。收尾补跑 `python3 tools/run_local.py build`：120 条中 115 通过、5 条条件跳过，V21 在隔离 PostgreSQL 17 应用成功并产出 jar，前端构建包含在产物中。

远程 CI：本项推送后的 [CORE Docker CI 43877392638](https://github.com/liuyiwuqing/nexus-core/actions/runs/43877392638) 与 [04 CI 43877407916](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43877407916) 均在约 2 秒内失败：job 已创建但 `steps` 为空、无日志可读，与 TELEMETRY-01 的计费/额度阻断一致。非代码失败，不把本地 46 项写作远程通过，待额度恢复后补跑。

## TELEMETRY-03 · 状态历史指标切换与音频故障标记（2026-11-16，CORE `ef98bc8`、04 `1dcd617`）

TELEMETRY-02 的图只画内存。本轮在「状态历史」时间范围下加指标组（内存默认/音量/未读/音频），复用同一条 `GET .../telemetry` 响应，切换只换本地序列、不发请求；内存保留双序列（`freeHeap` 实线 `--accent`、`minFreeHeap` 虚线 `--info`），音量 `%`、未读 `条`、音频欠载 `次` 为单序列。`audioFaulted=true` 的桶在音频视图叠加 `--danger` 圆点，图例出现「音频异常」，悬停该点写明；`audioUnderruns` 为 `null` 的桶不画，1.8 倍桶宽断线规则不变。计数/百分比下界钳 0，全 0 时刻度不出现负值。

- **验证**：`npm run check`（78 token）与 `npm run build` 通过；夹具让 volume/audioUnderruns/audioFaulted 随点变化，`test_management_ui.sh` 新增 8 项断言（指标组、音量单序列与 `%`、未读 `条`、音频故障点/图例/提示、切回内存），46→54 项通过，390px 无横向溢出仍覆盖。全量 `python3 tools/run_local.py build`：120 条中 115 通过、5 条条件跳过，V21 在隔离 PostgreSQL 17 应用成功并产出 jar。
- **未做**：缩放、导出、图表游标联动；未迁移真实库、部署、烧录与实机视觉终验。CORE `ef98bc8` 与 04 `1dcd617` 已推送并核对远端；[CORE Docker CI 43893638155](https://github.com/liuyiwuqing/nexus-core/actions/runs/43893638155) 与 [04 Software checks 43893666019](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43893666019) 均在约 2 秒内失败、`steps` 为空且无日志，仍是 GitHub 计费/额度阻断，非代码失败，待恢复后补跑。

## TELEMETRY-04 · 状态历史缩放与 CSV 导出（2026-11-17，CORE `f13c4f5`、04 `a7de938`）

TELEMETRY-03 之后补图表交互。图内拖拽选区放大时间窗口，只放大已取回并聚合的桶、不重新请求历史；纵轴按窗口内采样自适应，`clipPath` 裁掉窗外线段/故障点/游标，跨度小于 3 小时轴标签精确到秒。缩放后出现「重置缩放」，双击图表也可恢复；每分钟刷新时窗口仍在查询范围内则保留。按当前范围导出全部 18 列桶数据（采样/接收时间、样本数、固件与配置版本、音量/亮度/静音/免打扰/未读、内存四值、音频欠载、推理/显示最长耗时、音频异常），UTF-8 BOM + CRLF，文件名 `telemetry-<设备>-<范围>-<UTC>.csv`，无点时按钮禁用。

- **验证**：`npm run check`（78 token）与 `npm run build` 通过；`test_management_ui.sh` 新增 5 项断言（CSV 下载文件名与大小、CSV 头与 70+ 行、拖拽缩放、缩放后悬停、重置），54→59 项通过，390px 无横向溢出仍覆盖。全量 `python3 tools/run_local.py build`：120 条中 115 通过、5 条条件跳过，V21 在隔离 PostgreSQL 17 应用成功并产出 jar。
- **未做**：缩放为已取回桶的放大而非重新采样，未做多序列联动；未迁移真实库、部署、烧录与实机视觉终验。CORE `f13c4f5` 与 04 `a7de938` 已推送并核对远端；[CORE Docker CI 43908329517](https://github.com/liuyiwuqing/nexus-core/actions/runs/43908329517) 与 [04 Software checks 43908339990](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43908339990) 仍约 2–3 秒失败、`steps` 为空且无日志，属 GitHub 计费/额度阻断，非代码失败，待恢复后补跑。

## TELEMETRY-05 · 设备 Wi-Fi 地址与信号（2026-11-17，CORE `8656303`、04 `f8c6610`）

设备管理此前只展示内存等状态，设备表要求的 IP/RSSI 一直缺位。本轮固件 `svc_net` 新增只读 `pet_net_local_ip`/`pet_net_rssi`：从既有关联的 netif 取 IPv4、从 AP 记录取 dBm，不新增连接、不读 SSID/口令；联网时 `app_summary_json` 附带 `wifiIp`/`wifiRssi`，未关联或假传输时整项省略。`telemetry.schema.json` 增加可选字段与 IPv4/`-130..0` 约束，并重新生成 `pet-contracts.ts`。

CORE 侧 V22 为样本表追加 `wifi_ip`/`wifi_rssi`：地址变化立即成条、信号波动随心跳（默认 30 秒），聚合桶取桶内最新非空地址与平均信号，非法 IPv4 或越界 RSSI 使整条遥测被拒；管理台实时快照显示「网络 · 地址 · dBm」，状态历史新增「网络」指标画 RSSI，CSV 扩至 20 列。

- **验证**：`npm run check`（78 token）与 `npm run build` 通过；夹具补充 `wifiIp`/`wifiRssi` 与快照网络字段，`test_management_ui.sh` 由 59 增至 62 项（快照地址/信号、网络指标与 dBm、CSV 网络列），390px 无横向溢出仍覆盖。固件增量 `idf.py build` 通过（`nexus_pet.bin` 0x2d81c0，app 分区余量 29%），`test_host.sh`（`LC_ALL=C`）与 `check_contracts.py` 通过。CORE 全量构建 121 条中 116 通过、5 条条件跳过，V22 在隔离 PostgreSQL 17 应用并产出 jar。
- **未做**：SSID/BSSID、信道、掉线原因与凭据轮换 UI 未纳入；未迁移真实库、部署、烧录，未验证真实 AP 切换、弱信号表现与长期采样。`test_host.sh` 的构建矩阵用例在中文 locale 下按英文 `dubious ownership` 断言失败，`LC_ALL=C` 重跑通过，属既有环境问题，未改断言。CORE `8656303` 与 04 `f8c6610` 已推送并核对远端；[CORE Docker CI 43989886723](https://github.com/liuyiwuqing/nexus-core/actions/runs/43989886723) 与 [04 Software checks 43989890697](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43989890697) 仍在约 2 秒内失败、`steps` 为空且无日志，属 GitHub 计费/额度阻断，非代码失败，待恢复后补跑。

## TELEMETRY-06 · 设备卡片显示最近网络（2026-11-17，CORE `2345764`、04 `862c466`）

设备列表此前只有在线与最近连接，TELEMETRY-05 采集的地址和信号只能进详情面板才看得到。本轮 `GET /devices` 用 `LEFT JOIN LATERAL` 取每台设备最近一条带 `wifi_ip` 的样本，返回 `wifiIp`/`wifiRssi`/`telemetryAt`；卡片在「最近连接」下新增「Wi-Fi 地址 · dBm · 相对时间」，无地址时整行省略。无新表、无迁移，列表仍一次查询；不改变离线判定与登记/改名流程。

- **验证**：`DeviceManagementFlowTest` 在既有网络用例中补列表断言（地址与信号取最近带地址样本）；夹具 `/devices` 补充字段，`test_management_ui.sh` 由 62 增至 63 项并断言卡片显示 `192.168.1.20` 与 `-52 dBm`，390px 仍无横向溢出；CORE 全量构建 121 条中 116 通过、5 条条件跳过，前端检查与构建通过。
- **未做**：未在列表显示固件版本、队列深度或逐台信号趋势；未做按信号/离线排序与筛选；未迁移真实库、部署、烧录或实机验收。CORE `2345764` 与 04 `862c466` 已推送并核对远端；[CORE Docker CI 43992251814](https://github.com/liuyiwuqing/nexus-core/actions/runs/43992251814) 与 [04 Software checks 43992263834](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43992263834) 仍在约 2 秒内失败、`steps` 为空且无日志，属 GitHub 计费/额度阻断，非代码失败，待恢复后补跑。

## TELEMETRY-07 · 设备网络质量（2026-11-17，CORE `05255ed`、04 `a6f2516`）

此前只能看到地址与信号，无法判断设备换到了哪个网络、是否受信道干扰或为什么掉线。固件 `svc_net` 新增只读 `pet_net_ssid`/`pet_net_channel`/`pet_net_last_disconnect`：从当前 AP 记录取 SSID 与主信道，断线原因和时刻在运行期断线事件中记录；SSID 空口字节不是可打印 UTF-8 时整条省略，未对时只报原因不报时刻。`app_summary_json` 按需附带 `wifiSsid`/`wifiChannel`/`wifiDisconnectReason`/`wifiDisconnectedAt`，`telemetry.schema.json` 增加四个可选字段并重新生成 `pet-contracts.ts`。CORE 以 V23 追加 `wifi_ssid`/`wifi_channel`/`wifi_disconnect_reason`/`wifi_disconnected_at`，SSID、信道与断线原因变化立即成条，聚合桶取最新非空网络信息；非法 SSID、越界信道或原因整条拒绝。管理台快照显示网络名/地址/信道/信号与「上次断线」原因名称，设备卡片显示 SSID 与地址，CSV 扩至 24 列。

- **验证**：`DeviceManagementFlowTest` 扩展既有网络用例（原始字段、最新网络聚合、非法地址/信道整条拒绝）。夹具补充 SSID/信道/断线原因，`test_management_ui.sh` 由 63 增至 64 项（卡片网络名、快照信道、断线原因映射、CSV 列），390px 仍无横向溢出。固件增量 `idf.py build` 通过（`nexus_pet.bin` 0x2d84f0，app 分区余量 29%），`test_host.sh`（`LC_ALL=C`）与 `check_contracts.py` 通过；CORE 全量构建 121 条中 116 通过、5 条条件跳过，V23 在隔离 PostgreSQL 17 应用并产出 jar，前端检查与构建通过。
- **未做**：未纳入 BSSID、信道带宽（`second`）与断线历史列表；SSID 非法时只是省略，不转码；当轮未迁移真实库或部署（随后已在本机实际执行，见下一节），未烧录，未验证真实 AP 漫游、信道干扰与断线原因长期分布。CORE `05255ed` 与 04 `a6f2516` 已推送并核对远端；[CORE Docker CI 43995342794](https://github.com/liuyiwuqing/nexus-core/actions/runs/43995342794) 与 [04 Software checks 43995350148](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43995350148) 均在约 4 秒内失败、`steps` 为空且无日志，属 GitHub 计费/额度阻断，非代码失败，待恢复后补跑。

## TELEMETRY-07 部署 · 本机真实库迁移（2026-11-17）

管理台与 CORE 在本机真实环境完成 V23 迁移和进程重启。迁移前确认真实库位于 `nexus-core` Compose 项目（`nexus-core-postgres-1`，127.0.0.1:54329），Flyway 停在 V20；依赖容器已不存在但 `nexus-core_postgres-data` 卷仍在，按原卷重建后通过健康检查。按升级口径备份 `data/backups/nexus-before-v23-20261117-094358.dump`（86 KB、183 个 TOC 条目、600），在临时库 `nexus_restore_check` 完整恢复并核对（最高 V20、37 张表、无样本表）后删除。停止旧 JAR（PID 35397），用新构建的 JAR 启动，Flyway 自动应用 V21–V23，`flyway_schema_history` 三条 success；`/api/v1/status` 的 database/mqtt 均 ready，管理台 200，设备列表出现 `wifiSsid` 键，`GET /devices/dodo-1/telemetry?bucket=1h` 返回空点集。服务由独立会话后台运行（PID 48119，日志 `data/logs/core-20261117-094505.log`）。

- **边界**：这是本机真实库与本地进程部署，不是目标服务器、1Panel 镜像或公网域名验收；设备尚未烧录新固件，网络质量字段仍需实机上报后才能看到真实值。备份和日志位于忽略的 `data/`，未输出任何凭据。

- **管理台验收（ego-browser，2026-11-17）**：管理员令牌登录成功；设备页显示 1 台设备/0 在线/从未连接，无遥测时不渲染 Wi-Fi 行；「设置与状态」显示“尚未收到设备状态”，状态历史空态为“所选范围内没有采样点。”且导出 CSV 禁用，切到网络指标无错误；390px 视口无横向溢出；概览显示 0 在线/1 离线、今日收件 0、失败率 `—`。投递页仅有 1 条 2026-11-10 的 `ack_timeout` 历史失败，与本次迁移无关。未做真机上报、漫游与断线原因分布验收。


## TELEMETRY-08 · 设备断线历史列表（2026-11-17，CORE `6306809`）

TELEMETRY-07 只在快照里显示「上次断线」，无法回看此前掉线。本轮不新增表或迁移，直接用已落库的遥测样本生成只读历史：CORE 新增 `GET /api/v1/devices/{deviceId}/telemetry/disconnects?limit=20`，按 `observed_at` 比较相邻样本，仅当 `(wifi_disconnect_reason, wifi_disconnected_at)` 与上一条不同且原因非空时才记一次事件；同一对值在后续心跳重复上报不重复列出，原因回到空只表示重新上报可用网络，不算事件。设备未对时只报原因时 `disconnectedAt` 为空，仍按上报时刻列出。`limit` 默认 20、范围 1–100（非整数或越界 400），设备不存在 404，未授权 401；接口只读，不写样本、不触发设备动作。

管理台在状态历史下方新增「最近断线」折叠区：断线时刻（未对时退回记录时刻并注明）、原因名称、记录时间；未收录的原因码显示数字。无断线原因且列表为空时整块不渲染；打开面板、点「刷新状态」或设备上报新的断线时刻时重读。CORE `docs/device-telemetry-history.md`、`docs/console-ui.md` 已同步接口口径与界面说明。

- **验证**：`DeviceManagementFlowTest` 新增断线历程序例（401/404/400、原因变化立即成条、时刻变化随心跳成条、同一对值跨心跳去重、无原因样本不计入、`limit` 截断），测试 Broker 增加 `dodo-disconnects` 凭据；CORE 全量构建 136 条中 131 通过、5 条条件跳过、0 失败，前端类型检查、74 token 守卫与生产构建通过。`test_management_ui.sh` 由 64 增至 66 项：实际 Vue + 环回夹具断言三条事件、原因名称（含未知码 777）、未对时说明与只读端点 `limit=20`，390px 仍无横向溢出。
- **未做**：未纳入 BSSID 与信道带宽，未做断线次数统计、筛选、导出或趋势图；不覆盖保留期外或已清理的样本，未对时且原因为空的历史无法补造；未迁移真实库、未部署、未烧录或实机验收。CORE `6306809` 已推送并核对 `origin/codex-core-baseline`；[CORE Docker CI 44102984359](https://github.com/liuyiwuqing/nexus-core/actions/runs/44102984359) 的 docker-compose job 成功。04 `75047d0` 已推送并核对 `origin/main`；[04 Software checks 44103563880](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44103563880) 八个 job 全部成功。

## CONSOLE-21 · 紧凑布局与 Naive UI 表单（2026-11-17，CORE `959e7b7`）

按用户反馈进一步压缩留白：侧栏 208px、页头 56px、桌面页边距 24px、卡片内距 16px、字段间距 12px、标准控件 34px；设备摘要/动作在上方，设置面板满宽；卡内指标 18px，网络等长文本 14px。桌面四条以内消息容器 340px，手机短列表随内容收缩，筛选状态/来源并排、日期各占一行。各页表单、输入、下拉、数字、勾选、滑块、日期/时间、时区补全和 manifest 选择统一使用 Naive UI。

`ConsoleForm`/`ConsoleField`/`formRules` 负责标签、原输入约束、提交前反馈与焦点；权限、版本、API 和原身份重试继续由原业务层处理。编辑中暂时清空时间/显示时长不会被轮询覆盖，提交仍用分钟数/毫秒数，收件窗口仍转 UTC。manifest 不自动上传，异步读取保留最新选择；`vueuc` 浮层/虚拟列表局部样式工厂经过现有 nonce 目标，正式 CSP 不放宽。用 Select 公开实例修正关闭已被筛走的投递时间线后的焦点恢复。见 [23](23-console-completion.md#console-21--紧凑布局与-naive-ui-表单) 与 CORE [界面约定](../../nexus-core/docs/console-ui.md)。

- **验证**：21 组实际 Vue + 环回 RAM HTTP/SSE 浏览器检查共 848 次通过：消息 33、管理 64、筛选 44、元数据导出 44、正文导出 15、角色 39、OTA 23、成长 18、工具/操作 96、规则编辑 99、规则删除/预演 52、路由 34、来源 38、RSS/天气 32、主题/导航 33、实时/引导 21、概览 17、条件规则 17、投递历史 62、模板 32、新增表单/紧凑布局 35。脚本通过 `naive_ui_helpers.js` 操作实际菜单/键盘/焦点，未用组件内部状态或隐藏原生控件。类型/75 token 守卫/生产构建、Shell 与 JS 语法、`git diff --check` 通过；1440×900 与 390×844 明暗截图保留忽略目录 `firmware/build-ui-console-forms/`，已实际查看。
- **完整构建**：`python3 tools/run_local.py build` 退出 0，121 条中 116 通过、5 条条件跳过；最终手机短列表样式经新增专项复核后，`mvn -s .mvn/settings.xml -B -ntp -DskipTests package` 更新静态资源，Java 未改动。JAR 的 23 份前端文件和 V1–V23 迁移均逐字节匹配，无旧哈希资源。首轮实时/模板检查各出现一次登录连接失败，单独重跑通过，初因尚未确认；等待真实菜单可用与抽屉焦点恢复后再操作，日期/手机浮层检查通过。
- **交付边界**：CORE `959e7b77ff0a61e6890e3985b9450c7e03200b06` 已推送并核对远端分支；04 配套随本项提交。本次未改后端/固件，未重启真实 8088 服务、迁移真实库、发送真实消息或验收设备；5173 开发预览继续运行，远程 CI 与本地验证分别记录。


CONSOLE-21 推送核对：CORE `959e7b7` / 04 `20ece8e` 均已推送且远端分支包含对应提交。[CORE Docker CI 44008225925](https://github.com/liuyiwuqing/nexus-core/actions/runs/44008225925) 与 [04 Software checks 44008403882](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44008403882) 均未开始执行步骤；已读取 check-run 注释，GitHub 明确报告账户付款失败或 spending limit 需要提高。远程 CI 未通过，本地检查结果不替代它；本条后续提交仅补记录，不改变已验证代码。


## CONSOLE-22 · 常用筛选与全站按钮（2026-11-17，CORE `11f6007`）

消息页默认只显示消息状态、来源标识，收件时间条件收进“更多筛选”，已应用数量直接显示在按钮上。折叠保留草稿和已应用条件，分页/UTC 查询/导出继续使用同一条件，清空一并重置；倒置时间会展开并报错。投递页条件全部可见。导航、消息列表项、图表“重置缩放”、历史评论导入和“导出”入口统一使用 `NButton`，用 Naive 的主/次/危险层级与选中态，删除旧原生按钮背景/悬停/边框/焦点覆盖；保留正文导出确认、禁用/权限与原身份重试。导出打开时明确焦点归属，Escape 关闭后返回入口。

- **浏览器验证**：8 组实际 Vue + 环回 RAM HTTP/SSE 共 328 次通过：表单 38、主题/12 页按钮 47、元数据 CSV 48、正文 CSV 15、消息 33、管理 64、列表筛选 44、角色 39。新增折叠草稿保留、已应用高级数量和清空、历史评论导入展开/草稿检查；主题专项曾发现鼠标打开导出后焦点留在 body、Escape 未关闭，主动落焦后复跑主题/元数据/正文导出通过。已查看 1440px/390px 明暗截图，CSS、浮层和浏览器 CSP 检查通过。截图在忽略目录 `firmware/build-ui-console-forms/`、`firmware/build-ui-console-theme/`，结果在 `firmware/build-ui-console-22/`。
- **构建验证**：类型/74 token 守卫/生产前端、Shell/Python 语法、`git diff --check` 通过。`python3 tools/run_local.py build` 退出 0，121 条中 116 通过、5 条条件跳过；最终导入/焦点 UI 复测后以 `mvn -s .mvn/settings.xml -B -ntp -DskipTests package` 重新纳入静态资源，Java 未改。JAR 的 23 份前端和 V1–V23 迁移逐字节一致，无旧哈希文件。Vite 入口略超 500 kB 为非阻断告警，本项不扩为拆包重构。
- **交付边界**：CORE `11f6007` 已推送并核对远端分支，04 配套随本项提交。5173 开发预览与当前 Vue 模块返回正常；没有修改后端/固件、重启真实 8088、迁移真实库、发送真实消息或验收设备。远程 CI 与本地结果独立核对。


CONSOLE-22 推送核对：CORE `11f600731a73f19cc7baee5de6c345ce94480a70` / 04 `8705dd6261f017c548357e84420f60ea9f643de7` 均已推送，远端分支包含对应提交。本轮 [CORE Docker CI 44020100421](https://github.com/liuyiwuqing/nexus-core/actions/runs/44020100421) 与 [04 Software checks 44020146376](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44020146376) 已实际启动（与上一轮未启动的计费阻断分别记录）；截至 2026-11-17 12:53，本轮仍运行中，CORE 初始化 CLI 步骤 success、源码构建进行中，尚不记为远程 CI 通过。本条只补充交付状态，不改变已验证实现。


## CONSOLE-23 · 紧凑页面与一致按钮（2026-11-17，CORE `2a8f456`）

按用户跨页批注统一普通操作按钮：十二页、顶栏、筛选/分页和设备内联面板全部使用 34px 高、8px 圆角的 Naive 描边按钮；主提交/选中为深绿，危险为红色，图标按钮为方形。数字输入框内部加减键保留控件尺寸；导航/消息行保留选中语义。侧栏角色与折叠同排，退出登录单独一行，收起后仍有无障碍名称。

投递页收件日期沿用消息页“更多筛选”；路由/自动化可选条件、测试预演、模板预览、令牌签发和审计、已结束专注按需展开。设备先展示快照和常用设置，更多设置、状态历史、配置/升级记录分开呈现；未确认/失败记录自动展开。成长先展示档案与专注，再展示设备成长；工具查询结果直接显示、记忆检索桌面同排。`ConsoleDisclosure` 保留草稿和组件挂载，`ConsoleForm` 逐层展开校验错误并落焦；API、权限、确认、原身份重试和设备配置语义保持。

- **浏览器**：21 组实际 Vue + 环回 RAM HTTP/SSE 共 900 次检查通过：表单 47、主题/十二页按钮 65、管理 64、路由 35、自动化条件 17、规则编辑 99、规则生命周期 52、模板 33、角色 41、工具/操作 96、元数据 CSV 48、投递历史 62、成长 18、拉取 32、来源 38、列表筛选 44、OTA 23、消息 33、正文 CSV 15、概览 17、SSE/引导 21。覆盖按钮高度/圆角/描边、侧栏展开与收起、折叠草稿、隐藏错误自动展开/落焦且不提交，以及原有失败、未知、重复、确认、权限和导出行为。已查看 1440px/1226px/390px 明暗截图，包括用户标注宽度的消息、成长和设备展开布局。初轮校验提醒保留遥测来源，已放入折叠摘要；截图工具曾超时/失败，前置自身 QA 页并使用 reduced motion 后复跑。最终专项日志和逐组计数在忽略目录 `firmware/build-ui-console-23/`，最终截图已复制到 `firmware/build-ui-console-23/screenshots/`。
- **构建**：`npm --prefix frontend run check` 的类型/74 token 守卫、生产前端构建与 `python3 tools/run_local.py build` 均退出 0。完整 CORE 121 项中 116 通过、5 项条件跳过，无失败/错误；最终 24 份前端/23 份迁移与 JAR 逐字节一致，无旧哈希资源。Vite 的入口略超 500 kB 为既有非阻断告警。
- **本地预览**：5173 页面和当前 Vue 模块返回 200。最终只读复核发现原 8088 进程所用 JAR 被后续构建替换，根页面请求超时且日志出现 `NoClassDefFoundError`；保持原进程环境，从忽略目录内的独立 JAR 副本重启开发后端，禁用启动迁移，避免后续构建再次替换运行文件。恢复后直接访问 8088 的页面/状态/消息/设备 GET 均为 200，5173 页面及其消息代理 GET 同为 200；只读记录见 `preview-check.json`。
- **边界**：上述浏览器流程使用隔离 RAM 夹具，完整构建包含独立测试容器；未迁移真实库、发送真实消息、操作真实设备或进行生产部署验收。跨仓提交及远程 CI 与本地结果分别记录。

CORE `2a8f4561c5301b09fa714bfc8737b5d9f6cbacbf` 已正常推送至 `codex/core-baseline` 并核对远端分支；本仓配套测试与台账同项提交。远程 CI 状态继续与本地 900 次浏览器检查、完整 CORE 构建及开发预览恢复分别记录。

本项 [CORE Docker deployment 44023847705](https://github.com/liuyiwuqing/nexus-core/actions/runs/44023847705) 已实际启动，目前 `in_progress`，尚不记为远程 CI 通过。04 配套提交推送后核对其流水线。

CONSOLE-23 推送核对：CORE `2a8f4561c5301b09fa714bfc8737b5d9f6cbacbf` / 04 配套 `8684155e3b18afff8e6d4fcdd0c86ae74a5941e1` 均已正常推送，远端分支包含对应提交。截至 2026-11-17 13:25，[CORE Docker deployment 44023847705](https://github.com/liuyiwuqing/nexus-core/actions/runs/44023847705) 与 [04 Software checks 44023970010](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44023970010) 均实际运行中，尚不记为远程 CI 通过。本条只补充交付状态；本地 900 次浏览器检查、完整 CORE 构建和 5173/8088 只读预览复核已通过。


## CONSOLE-24 · 紧凑表格卡片与浮层（2026-11-17，CORE `450c54f`）

按用户要求统一各页表格、卡片和浮层，并继续收紧整体布局。桌面页边距 24→20px，卡片内距 16→12px，保持 12px 圆角、1px 边框和明暗表面色；普通卡片标题 14px，正文和核心指标保留字号。内部记录改为分隔线，减少重复框；分页、卡片底部和表单动作使用相同间距。投递、自动化执行、操作审计表格单元格纵向 12→8px、横向 16→12px，顶端对齐、长标识换行、数字等宽，表头补 `scope="col"`；手机字段标签、操作和分页不删减。

导出菜单及 Naive 下拉/日期/时间层统一表面、12px 圆角和浮层阴影；导航抽屉统一头尾/内容间距。删除、撤销、来源凭据轮换及重启确认使用统一面板/动作分隔，保留危险/警示语义、名称或勾选校验、原位置和确认流程。没有将内联确认换成其他交互，也没有新增依赖。7 个 Vue 文件的业务脚本与原 HEAD 逐字节一致，仅改模板样式 hook 和表头关联；其余实现集中在 `style.css` 和 `theme.ts`。

既有主题、表单、元数据 CSV 和权限脚本补充密度、列关联、浮层及确认区检查；自动化生命周期和时间线脚本保留功能断言并整理截图/手机场景隔离。

- **浏览器**：21 组实际 Vue + 环回 RAM HTTP/SSE，共 913 次检查通过：主题/十二页 68、表单 48、元数据 CSV 51、管理 64、路由 35、自动化条件 17、编辑 99、生命周期 52、模板 33、角色 43、工具 96、投递历史 66、成长 18、拉取 32、来源 38、筛选 44、OTA 23、消息 33、正文 CSV 15、概览 17、SSE/引导 21。原有空态、加载、失败、权限、禁用、确认、未知结果、原身份重试、分页与导出继续验证。
- **截图与复跑**：已查看 1440px/1226px/390px 的概览、消息、设备、投递明暗、日期浮层、导出菜单和确认区。初轮有截图接口超时；前置自身 QA 页、使用 reduced motion 和显式截图后复跑。时间线脚本中前序流程残留的下拉层曾使 390px 检查失败；从 CORE 改前 HEAD `2a8f456` 隔离构建前端，同一菜单关闭断言仍失败。手机布局改成独立登录场景，保留四条投递、30 条历史及原宽度断言，完整 66 次通过；没有删除浮层、裁掉溢出或修改业务逻辑来通过检查。初轮、对照和最终日志在忽略目录 `firmware/build-ui-console-24/`，最终截图在其 `screenshots/`，逐组次数与退出值见 `verified-results.json`。
- **本地预览**：5173 的页面/CSS/主题模块/消息代理及 8088 状态 GET 五项只读检查均返回 200，记录在 `preview-check.json`。8088 继续使用上一项的稳定 JAR 副本，本轮未重启后端，未迁移真实库、发送真实消息、操作设备或部署目标机。

- **构建与打包**：`npm --prefix frontend run check` 类型/74 token 守卫和生产前端构建通过；`python3 tools/run_local.py build` 退出 0，121 项中 116 通过、5 项条件跳过，无失败/错误。最终 JAR 的 24 份前端及 23 份迁移与当前输出逐字节一致，无旧哈希文件，核对见 `package-check.json`。入口略超 500 kB 是非阻断告警，本项不扩大为拆包改造。浏览器、构建、开发预览与后续 CI/真实验收分别记录。

CORE `450c54f6dffad1fc7b2802738fa350b5d723c85b` 已正常推送至 `codex/core-baseline`，已核对远端分支。04 配套测试与台账同项提交，CI 和本地结果继续分开记录。

- CORE 远程 CI [Docker deployment 44038263653](https://github.com/liuyiwuqing/nexus-core/actions/runs/44038263653) 已启动，本次检查为 `in_progress`，尚未据此声明远程通过；本地结果见上。

配套验证/文档 `a487854fcfd01905d8b4163582e53bd8df5ebbc9` 已推送 `main` 并核对远端；原 `.workbuddy/` 未暂存。2026-11-17 15:24 +08:00：CORE [Docker deployment 44038263653](https://github.com/liuyiwuqing/nexus-core/actions/runs/44038263653)、04 [Software checks 44038366787](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44038366787) 均为 `in_progress`，远程完成结果不由本地通过代替。此后仅追加本条交付记录，不改变已验证实现或脚本。


## CONSOLE-25 · 统一加载、空状态与错误反馈（2026-11-17）

按用户要求继续统一十二页的读取反馈与设备内联面板。新增 `ConsoleState` 仅负责骨架、紧凑加载、空结果与语义错误/警告/成功展示，沿用现有主题 token、图标和 Naive 组件，不引入依赖。异步页面骨架复用；局部读取错误不再单独吸顶，只有外壳反馈保持吸顶。手机长错误和动作可换行，减少动态效果偏好继续生效。

消息/投递去除页头和失败卡片的重复说明，由外壳保留一次错误原因与重试；加载、空结果和失败分支互斥。设备、接入透传已有读取状态，读取失败不显示空清单。新增权限令牌/审计、设备记录等空态，减少“零指标＋尚无记录”重复文案；配置/来源核验、时效/未知历史、操作后果和权限信息保留。AI 分析仅在成功读到配置时才提示未启用。未知提交继续用 `StatusNote` 的 alert、焦点及原身份核对/重试流程，投递取消提示统一到相同语义；没有改业务请求或草稿规则。

验证使用新增十二页状态矩阵 `tools/ci/test_console_states_ui.sh`，配合既有 21 组实际 Vue/环回 RAM HTTP/SSE 脚本、类型/token、生产构建和完整 CORE 构建。截图与运行日志留在忽略目录 `firmware/build-ui-console-25/`，本地预览与远程 CI/真实验收分别记录。下方补充实际退出结果和交付状态。


- **源码核对**：22 个既有业务脚本在忽略展示 import/props 后与 CORE 原 HEAD 完全一致，核对清单 `business-script-check.json`；无后端/API/权限/数据库/固件变更。
- **构建与预览**：类型/74 token、生产前端及完整 `python3 tools/run_local.py build` 退出 0，121 项中 116 通过、5 条件跳过，无失败/错误。`package-check.json` 核对 JAR 的 25 份前端/23 份迁移逐字节一致，无旧资源；`preview-check.json` 中 5173 页面/新组件/CSS/消息代理与 8088 状态 GET 五项都是 200。8088 稳定副本未重启，未迁移真实库、发送真实消息、操作设备或目标机。

- **初轮与修正**：原筛选/元数据、概览、历史空态检查中的旧标题/样式钩子改为唯一错误条、实际零指标和统一状态，但原筛选/无旧行/导出禁用/恢复等行为断言保留。状态矩阵初始化自身明暗偏好，避免沿用上轮深色状态。鼠标追踪观察到高层 `click` 会将 1440px 视口改回原生 1698px，旧坐标落在表单空白；表单/主题脚本改用当前坐标的 CDP 实际鼠标按下/释放，宽度和功能断言不删。另修正页头读取时的高度变化，新增加载中筛选区不位移断言；失败后自动轮询不再与错误条叠加初始加载面板。来源及表单曾停在异步骨架，保留失败日志；来源用新 URL 重载并前置 QA 页后完整 38 次通过，表单前置 QA 页后完整 48 次通过。所有截图和原始/复跑日志保留，不把初轮失败计成通过。

- **最终浏览器**：22 组实际 Vue/环回 RAM HTTP/SSE 共 1038 次检查通过：console_states 125、list_filters 44、operations 96、overview 17、console_theme 68、console_forms 48、metadata_export 51、management 64、routing 35、automation_conditions 17、automation_edit 99、automation_lifecycle 52、templates 33、roles 43、delivery_history 66、growth_sync 18、pull 32、sources 38、remote_ota 23、message_compose 33、content_export 15、events 21。状态矩阵最终 125 次含十二页延迟/503/原入口恢复、空/未启用状态、加载与错误去重、筛选区不位移、明暗/390px、ARIA/reduced-motion/CSP；原 21 组共 913 次业务检查仍通过。`verified-results.json` 只引用最终退出 0 的日志，初轮失败与复跑单独保留。已查看桌面加载/空/错误、手机空/明暗错误、表单/设备截图；原校验、草稿、确认、权限、未知结果身份、分页与导出路径继续验证。
- **最终静态包**：失败轮询展示条件收紧后复跑状态/筛选及表单，类型/74 token 与生产前端通过；Java 未改，`mvn -s .mvn/settings.xml -B -ntp -DskipTests package` 退出 0，最终 25 份前端/23 迁移与当前输出逐字节一致（无旧资源）。该步骤不替代前述完整 121 项构建，原构建日志和最终打包日志分别保留。最后五项预览 GET 再次 200。

CORE `b5e58d87ffaeafafe66714ef7bf5563127cb6259` 已推送至 `codex/core-baseline` 并核对远端分支；04 配套脚本和台账同项交付。

CORE 远程 CI [Docker deployment 44055865803](https://github.com/liuyiwuqing/nexus-core/actions/runs/44055865803) 已触发，本次检查为 `in_progress`；远程结果与本地验证分别记录。

- 最终退出/清理复核：来源的一次复跑在 38 条功能断言后出现 shell 尾段错误，保留该日志且不作为最终通过依据；固定脚本重新完整执行，38 条、退出 0、`completeTaskSpace` 返回 `done: true` 均已确认（`sources-final.log`）。最终汇总改为引用这份日志，22 组总数仍为 1038；最终浏览器构建与 JAR 再次逐字节一致。

04 配套脚本/验证/台账 `cee90c817ba22a1a42185c3a471accd3439e9137` 已推送并核对 `origin/main`，`.workbuddy/` 未暂存。远程状态复核：[Docker deployment 44055865803](https://github.com/liuyiwuqing/nexus-core/actions/runs/44055865803) `in_progress`；[Software checks 44055957685](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44055957685) `in_progress`；本地浏览器/构建与远程 CI 分开记录。后续仅追加交付记录，不改变已验证实现和脚本。


## CONSOLE-26 · 刷新恢复登录与令牌维护（2026-11-17）

按用户本次要求修复刷新后丢失登录，并设计实现管理员及角色令牌维护。原实现只存页面内存，已在实际旧 Vue + 环回 RAM 复现登录成功后刷新退回登录。新增测试先在原实现失败（会话 Cookie 缺失、角色编辑/轮换与管理员修改接口不存在），随后实现并逐项回归；原脚本、失败和最终证据留在忽略目录 `firmware/build-ui-console-26/`。

- 默认保持登录 8 小时，原 Bearer 留在服务端内存；浏览器仅持 HttpOnly/SameSite=Strict 会话 Cookie，外部主机始终 Secure。刷新 GET 会话后恢复身份；每次请求重验父凭据，失效/撤销/角色变化不会沿用旧权限。Cookie 写请求要求独立 CSRF，机器 Bearer、Webhook 与记忆工具边界保留；SSE、导出、退出、异步响应和同源多标签页纳入同一会话规则。CORE 重启后需要重新登录，不引入迁移。
- “权限”页可编辑角色令牌名称、角色、到期；重新生成保持身份及其他元数据，旧值失效，新值仅显示一次；过期/撤销不复活，版本冲突拒绝。名称确认、未确认结果、切页保留和回读入口保留。
- 管理员修改沿用保护文件，校验 current 与 revision；随机生成/重复输入/先保存确认后零过渡更新，不返回新旧秘密。CLI/Java 统一 POSIX record lock、原子替换、保留 0600/owner/group、稳定身份和完整 memory scope。环境变量/只读文件模式如实提示服务端维护；真实秘密不会由本轮测试自动轮换。
- 组件切走后仍由 App 接管成功会话，防止 Cookie 已更新而 CSRF/Bearer 仍用旧值；迟到响应不能恢复已退出身份。浏览器存储仅有无秘密的退出意图，不持久化草稿或幂等身份。

使用/契约同步 CORE `docs/roles-and-audit.md`、`docs/credential-rotation.md`、README 与本项目 20/23 文档；没有修改共享 Skill。实际构建、最终浏览器组数、预览配置、提交/推送和远程 CI 结果在验证完成后追加。

初轮完整 CORE 构建退出 0：127 项中 122 通过、5 项条件跳过。非回环 Secure Cookie 补强后鉴权/轮换/SSE/CSP 专项 21 项重新通过并打包，CLI 4 项通过。新增浏览器 60 条已通过，包含字段组件离开后的会话接管、读取 body 延迟后退出、修改令牌响应在退出后到达时清除其迟到 Cookie；同一会话 CSRF 证明不会误撤销另一个新登录。既有自动化编辑回归曾定位“busy 时忽略退出”，已修复为立即清空页面并独立结束会话，原失败日志保留后重跑。最终整体回归与预览证据继续追加。

本地预览：已将 CORE 8088 正常重启至忽略目录中的稳定 `core-console-26.jar` 副本，保留原进程环境和 `SPRING_FLYWAY_ENABLED=false`；仅为本地预览启用 `data/dev-preview/auth/tokens.json`，初始化时核对原管理员值与稳定身份相同，文件 0600、目录 0700，未轮换秘密；随后仅在 `data/local.env` 追加非秘密的 `NEXUS_AUTH_TOKEN_FILE` 路径并保留 0600，以确保正常重启后继续使用文件中的最新凭据。原先未配置 memory scope，本轮同样不新增。5173 Vite 持续运行；经真实 Vite 代理 POST 登录、无 Bearer GET 恢复、GET 管理员可编辑状态、POST 退出 204、随后 GET 会话 401，页面/组件/消息/状态四项只读 GET 200。该核对没有签发/编辑真实角色令牌、迁移真实库、发送消息或设备操作。

新增会话恢复阶段让登录表单晚于文档加载，既有 22 组浏览器脚本统一等待真实表单可用再输入；逐字核对原脚本，除增加该等待函数和调用外，原断言与动作完全保留。组合 heredoc 的函数作用域错误已在正式复跑前定位修正，原失败日志不计最终结果。

真实 5173 Cookie 会话进一步复核 SSE ready、缺失 CSRF 的 POST 被拒绝 403、正常退出 204。为避免网页修改后重启又退回旧环境凭据，本地启动配置持久化受保护 store 路径；仅追加非秘密路径，管理员初始值与原值相同，后续由用户明确修改。文件模式下应从受保护 store 的 admin.current.token 读取当前值，旧环境值不作后备。

最终完整 `mvn -s .mvn/settings.xml -B -ntp verify` 退出 0：128 项、123 通过、5 条件跳过（`final-verify.log`）。之后仅收紧测试中的 actor 断言，改为真实 GET 会话且要求非空 64 位身份，再单跑管理员修改流程 1 项通过；实现未再修改。最后静态包 28 份前端与 23 迁移逐字节一致，当前预览鉴权类也与最终编译输出一致。角色 UI 一次登录读取出现连接失败，原失败日志保留，脚本未经改动完整重跑 43 条、退出 0、任务空间完成；该失败不计通过。

最终浏览器汇总 `summary.json`：原 22 组 1038 条 + 新增 1 组 60 条 = 23 组 1098 条，最终运行全部退出 0 且任务空间完成。所有既有断言完整保留，首次失败/修复/复跑日志分开保存；构建与浏览器使用实际前端，RAM 夹具不替代真实服务端证据。依赖/协议检查、23 个 shell 语法、Python 编译和两仓 `git diff --check` 通过。规范沉淀为本项项目契约/使用说明/验收脚本，不更新共享 Skill。

真实浏览器最后复核：独立任务空间在 5173 实际登录并刷新恢复，打开“权限 → 修改管理员令牌”确认文件模式可编辑且三个秘密输入均为空；未填写/提交修改。测试会话已退出并核对刷新仍为登录页，任务空间完成。5173/8088 持续运行。

CORE 实现 `b546e1974f190ffaa2a66ebf59535138fb5aec18` 已正常推送至 `origin/codex/core-baseline`，`git ls-remote` 与本地 HEAD 一致。配套 04 的 23 组脚本、使用说明与台账随本项提交；`.workbuddy/` 和所有秘密/生成产物未暂存。

[CORE Docker deployment 44063434295](https://github.com/liuyiwuqing/nexus-core/actions/runs/44063434295) 当前 `in_progress`，结论 `尚未结束`；远程 CI 与本地构建/浏览器验证分别记录。

04 配套实现/检查/说明 `580e2eb4bcdbbe7601a9a4ce580796f558159a55` 已正常推送并核对 `origin/main`；两仓源码工作项已交付，后端和前端预览继续运行。 [04 Software checks 44063551850](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44063551850) 为 `in_progress`，结论 `尚未结束`。


## CONSOLE-27 · 登录会话跨 CORE 重启持久化（2026-11-17）

按用户要求在 CONSOLE-26 上补充服务端持久化，维持 8 小时绝对有效期及退出/令牌过期、撤销和轮换失效。本项替代 CONSOLE-26 的“CORE 重启需重新登录”限制。

采用私有会话目录，不更改数据库 Schema。文件名哈希 Cookie ID，内容仅含父凭据摘要、CSRF、绝对到期与格式版本；原始长期令牌和 Cookie 不写会话文件或浏览器存储。目录 0700/文件 0600，POSIX 锁与同步原子写，退出删除后同步目录。缺失 401，损坏或权限/IO 故障 503；不能靠重启、旧缓存或摘要 HTTP Bearer 绕过授权。原角色权限、CSRF、SSE/导出、同源标签页退出与原身份重试保持。

源码 Docker/1Panel 模板新增私有命名卷，新镜像设置 UID/GID 65534 与 0700，保留只读根和非 root；04 已固定旧镜像的部署编排不擅自替换。首次从内存版升级需要重新登录一次，之后在同一目录/卷、有效期内可恢复。会话文件的有界到期回收不启用业务清理。

验证先补服务实例重建测试并在旧 Map 实现失败，再实现持久化；首轮 IO 边界失败（缺失误报 503）和测试清理问题已定位修正，保留初轮与最终日志，不把失败计通过。新增独立 CORE JVM/真实 HTTP/隔离 PG/MQTT 覆盖重启、退出、权限变更、过期/撤销/轮换和不同角色独立性；私有文件、坏数据/符号链接、容量与绝对期限另有针对性检查。实际命令/结果、预览、提交及 CI 随验证追加。

针对性校验最终退出 0：26 项全部通过，包含真实独立 JVM 三次启动的重启/权限/退出/过期/撤销/轮换流程，以及文件权限/坏数据/符号链接、1024 上限、绝对期限、现有鉴权/凭据轮换/SSE/CSP。旧内存版的新增重启断言先失败，修正后的实例重建和跨 JVM 检查已实际通过。初始化 CLI 3 项与两份 Compose 解析通过；后续构建/浏览器和真实 Linux 容器结果另记。

本地配置仅追加忽略的 `NEXUS_AUTH_SESSION_DIRECTORY` 绝对路径，指向当前 `data/auth/sessions`，保持 env 0600 和目录 0700；避免下次从不同工作目录启动丢失会话，不移动或更换现有管理员凭据。预览先运行新源码镜像的独立 JAR 副本，保留原服务环境和禁用真实迁移；浏览器已新登录，等待后续最终包重启复核。

Linux 真实 HTTPS 夹具首次在 Secure Cookie 标志处失败，诊断确认登录 200、HttpOnly=true、Secure=false；原因是 TLS 在代理终止而后端 Host 为 localhost。修正只允许 HTTPS 转发提示收紧 Secure，外部主机无法用 http 提示降级，不把转发头用于身份。保留该轮失败和私有诊断，不计作通过；最终镜像重建后再跑完整容器检查。

最终 Linux 源码镜像 `nexus-core:console-27-final` 的部署脚本退出 0，8 组检查全部通过且清理自身随机项目与卷：真实 HTTPS Secure/HttpOnly Cookie、CSRF、UID/GID 65534 与目录 0700、非 root/只读根、MQTT 私有 CA、整套容器 down/up 后同一 Cookie/CSRF/期限恢复，退出 204 后重启 CORE，旧 Cookie 重放 401。初轮失败仅是上面已修正的 Secure 边界。实际命令 `docker build -t nexus-core:console-27-final .`、`python3 tools/test_docker_deployment.py --image nexus-core:console-27-final --report …/docker-session-final.json`；没有发布镜像。

本机真实预览用最终源码镜像提取的独立 JAR 更新并正常停止/启动两次 CORE：5173 浏览器在第一次重启后不输入令牌直接恢复 admin，绝对 expiresAt 与重启前逐字相同，Cookie 对 JS 不可见；退出后核对对应私有文件已删除、GET 会话 401，第二次 CORE 重启再刷新仍为登录页、GET 401。首次退出探针先于异步退出完成，等待后核对持久删除，未据此改业务。保留原管理员文件内容与 0600、原环境和禁用迁移；最终 8088 PID 8274、5173 Vite 继续运行。没有修改真实管理员/角色秘密，没有真实库迁移、消息或设备操作。浏览器任务已 finish；其旧封装标签被新版 SDK 识别为 unknown，按保护规则保留并交还用户，不关闭未知来源标签。

`python3 tools/run_local.py build` 退出 0：133 项，0 失败/错误，5 项需可选环境而跳过（在最后 HTTPS/503 边界补充前）；随后最终源码 `BrowserSessionsTest,BrowserSessionStoreTest,RoleAccessFlowTest` 定向 15 项全通过，涵盖 HTTPS 仅收紧与损坏会话 503 后修复恢复同一 Cookie，再 `mvn … -DskipTests package` 退出 0。最终 JAR 的 28 个静态文件与前端输出逐字节一致，当前镜像提取运行包的三项鉴权 class 与最终包相同。浏览器登录 60、角色 43、SSE 21，共 124 项通过；角色首轮网络读取未完成保留日志，未改实现重跑通过。Python 公开部署工具单测 11 项、初始化专项 3 项、两份 Compose 解析、py_compile 与两仓 diff 检查通过。证据均在忽略的 `firmware/build-ui-console-27/`；不把本地检查替代远程 CI、目标机部署或实机验收。

匹配的初始化镜像也从当前源码构建，`python3 tools/test_image_deployment.py --image nexus-core:console-27-final --init-image nexus-core-init:console-27-final --report …/image-session-final.json` 退出 0，7 组检查通过并清理自身隔离资源：无源码/socket 初始化、私有权限/凭据不覆盖、1Panel 三服务解析、非 root/只读启动与 literal env、实际 JAR 资源、MQTT TLS、容器重建后业务记录和原凭据保留。仅验证本地源码镜像，不发布新镜像或替换 04 固定发布版。临时重启脚本和浏览器指南副本已清理。

CORE `e76732b3792313ea5f3744b09970a7972fc89a36` 已正常提交并推送至 `origin/codex/core-baseline`，已用 `git ls-remote` 核对远端分支包含本次提交。04 本项仅提交 18/23 配套文档；现有 `.workbuddy/`、私有 env/会话、生成镜像/JAR 与测试证据均不纳入版本。远程 CI 另查当前 SHA，不以前项成功代替。

04 配套 `7b01867c2fa5d176c8cd3058995e89259669b3df` 已推送并核对 `origin/main` 包含该提交；该提交仅 docs，不匹配 Software checks 的 push paths，按 SHA 查询无新运行。CORE 本项远程 [Docker deployment #44074830487](https://github.com/liuyiwuqing/nexus-core/actions/runs/44074830487) 当前 `in_progress`，尚未获得远程完成结论；不计作 CI 通过。最终 5173 与 8088 HTTP 首页均 200，CORE 工作区干净；04 除原有未跟踪 `.workbuddy/` 外无其他遗留改动。

## DEL-07 续 · CORE 新镜像发布与公开访问（2026-11-17）

按用户要求把 nexus-core 代码推上线、发布新镜像以便更新线上部署。核对后代码不需要再推：本地 HEAD 与 `origin/codex/core-baseline` 同为 `e76732b3792313ea5f3744b09970a7972fc89a36`，`git rev-list --left-right --count` 为 0/0，工作区干净，仓库可见性已是 PUBLIC；因此本项实际交付是镜像发布、包可见性与部署口径。

镜像发布运行 [44094002890](https://github.com/liuyiwuqing/nexus-core/actions/runs/44094002890) 最终 success。首次触发时 `validate` 在 `docker build -t nexus-core:ci .` 的 `mvn -s .mvn/settings.xml -B -ntp -DskipTests package` 阶段失败，原因为 Maven Central 对构建机返回 429（`Non-resolvable parent POM ... spring-boot-starter-parent:pom:4.1.2 ... status code: 429`），当时未产出任何镜像；原样重试失败 job 后 `validate`、`publish`（core 与 init 各双架构）、`verify-published` 全部通过，未修改源码、Dockerfile、镜像源或重试参数，失败与重试分别记录，不把首轮失败计作通过。

发布产物（`verify-published` 的 `published-image-verification` artifact 内 `published-images.env` 原值）：

- CORE `ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba`
- INIT `ghcr.io/liuyiwuqing/nexus-core-init@sha256:9b4d60ccc175fbcd2d349d7117ceeb068d06d26f98f868132e8dc8a2a85faad6`

两者同时带 `sha-e76732b3792313ea5f3744b09970a7972fc89a36` 标签；回拉后的 7 项隔离容器检查通过，含无源码/socket 初始化、重复初始化拒绝、1Panel 三服务编排解析、非 root/只读启动与实际 JAR 资源鉴权、MQTT TLS、重建后数据与凭据保留。

可见性：仓库开源不会自动让 GHCR 包公开。改前用匿名令牌请求 `ghcr.io` 的 `nexus-core`、`nexus-core-init` 均返回 `UNAUTHORIZED`（同环境对照公开包可正常取 token），`gh` 令牌无 `read:packages`/`write:packages`，查询与 PATCH 均不可用，故由用户在 GitHub 包设置把两个包改为 Public。改后匿名 token 获取成功，按 digest 与 `sha-e76732b...` 标签请求 manifest 均为 200，manifest 含 `linux/amd64` 与 `linux/arm64`；服务器与 1Panel 因此无需 `docker login`。2026-11-17 起该结论取代此前“私有包 + PAT 登录”的口径。

CORE 配套 `f75d0f388554e9db3cf4cb17ce58d914647fd60a` 仅改 `docs/1panel-deployment.md`：新增镜像发布记录表、公开包匿名拉取说明与旧编排升级需补的三处会话卷改动；已推送并 `git ls-remote` 核对远端包含该提交。该提交只含 docs，不匹配 Docker deployment 的 push paths，按 SHA 查询无新运行。

部署边界：当前没有目标服务器，未连接、未修改任何线上编排，也未执行 `docker compose up`；线上更新由用户按其实际 1Panel 环境执行，结果以其机器为准。新镜像对应 `e76732b`，其 1Panel 模板相对 `043f134` 仅增加 `NEXUS_AUTH_SESSION_DIRECTORY`、`console-sessions` 挂载与同名顶层卷三处；旧编排不补这三项时容器仍可启动，但重启后登录会话不再保留。04 `deployment/1panel/docker-compose.yml` 的 DEL-08 本地编排仍固定 `230c809`，本项未改，需另行验收。

本项 04 配套提交为 `8b3b7d11479d1c9fb74e764eb1cde9899bd3c874`（`deployment/README.md` 发布口径 + 本文档），已推送并 `git ls-remote` 核对 `origin/main` 包含该提交。远程 [Software checks 44096057248](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44096057248) 六个 firmware job 全部 success；docs 改动未触及 CORE 源码，不产生新的 Docker deployment 运行。

用户提供的线上编排 `nexus-production` 固定 `ghcr.io/liuyiwuqing/nexus-core:sha-230c809670edab3800801d9135fde6465f190623`，且没有会话目录相关内容，判定必须修改四处：`core.image` 换为 `e76732b` 的 digest `sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba`；`core.environment` 增加 `NEXUS_AUTH_SESSION_DIRECTORY: /var/lib/nexus/sessions`；`core.volumes` 增加 `console-sessions:/var/lib/nexus/sessions`；顶层 `volumes` 增加 `console-sessions:`。其余服务、端口插值、只读根、`cap_drop`、健康检查与 `/etc/nexus/docker` 绝对路径不变。

缺失会话卷的后果经源码核对：`BrowserSessionStore.replace` 在 `Files.createFile(.lock)` 处遇只读文件系统抛错，被统一包装为 `Unavailable`，再由 `ApiErrors` 映射为 `503 session_store_unavailable`，即管理台登录直接失败，而非仅丢失重启后的会话；`read_only: true` 下该目录只是镜像内的只读目录，无法替代可写挂载。另外 `e76732b` 相对 `230c809` 新增 Flyway `V21`–`V23`（`CREATE TABLE device_telemetry_samples` 与两组 `ADD COLUMN`，无 drop），升级后把镜像回滚回 `230c809` 会因迁移校验失败拒绝启动，回滚需还原备份或关闭校验。

配套新增 `deployment/1panel/docker-compose.nexus-production.yml`（结构对齐用户线上模板，只含上述四处差异）与 [1Panel 升级说明](../deployment/1panel/README.md#从-230c809-升级到-e76732b)。`docker compose config` 在替换私有路径为临时占位后退出 0，解析出的卷名为 `nexus-production_console-sessions`、`NEXUS_AUTH_SESSION_DIRECTORY` 为容器内绝对路径、`read_only: true` 与 `user: 65534:65534` 保持。DEL-08 的 `docker-compose.yml` 仍固定 `230c809` 与 `pull_policy: never`，未改。目标服务器仍未连接，本轮未执行任何 `docker compose up`，线上结果以用户机器为准。

### 2026-11-17 · DEL-08 本地编排对齐 e76732b

DEL-08 此前把 CORE 固定为 `230c809`，并记录“本地编排仍固定 230c809，需另行验收”。在剩余软件缺口里，设备凭据轮换缺后端、SW-06-B 二期范围过大、SW-07 依赖真实声学环境，均不适合本轮纯软件验证，因此选此项。

- [`deployment/1panel/docker-compose.yml`](../deployment/1panel/docker-compose.yml)：CORE 改为 `ghcr.io/liuyiwuqing/nexus-core@sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba`，保留 `platform: linux/amd64` 与 `pull_policy: never`；增加 `NEXUS_AUTH_SESSION_DIRECTORY=/var/lib/nexus/sessions`、服务级 `console-sessions:/var/lib/nexus/sessions` 与同名顶层卷。其余服务、端口、私有路径、只读根、UID 65534、1 GiB、健康检查和日志限制未改。
- [`deployment/1panel/README.md`](../deployment/1panel/README.md)：固定镜像、校验命令、初始化镜像与 `--image` 参数、升级指引和验证边界同步到 `e76732b`；明确新安装使用本目录文件，已有 `230c809` 安装改用 `docker-compose.nexus-production.yml`。

核对事实（2026-11-17，本机 macOS，Docker Compose v5.5.0）：

- CORE [发布运行 44094002890](https://github.com/liuyiwuqing/nexus-core/actions/runs/44094002890) 为 success；`docker buildx imagetools inspect` 得到索引 digest `sha256:0310e7335b24bae830108f47001b18fdc195a47f8456ad47f977d0b96453ddba`，回拉后的 amd64 镜像 ID 为 `sha256:aa3a2b56291ddf7b86bf38bdc63fc431e2433d5348012bb9c5c5dacec928c238`，匿名拉取成功。
- `docker compose -f deployment/1panel/docker-compose.yml config --no-env-resolution -q` 退出 0。
- 隔离项目（临时私有目录与回环端口，初始化镜像 `sha-e76732b3792313ea5f3744b09970a7972fc89a36`）`up -d --no-build --wait` 后 postgres/mqtt/core 全部健康；未带令牌 `/api/v1/status` 返回 401，带令牌 `database`/`mqtt` 均 `ready`，页面 200。
- 日志确认 Flyway `V21`–`V23` 执行；`POST /api/v1/session {"remember":true}` 返回 `persisted:true`，仅凭 Cookie 可读会话；会话卷目录 `65534:0700`、会话文件 `0600`；重启 CORE 后同一 Cookie 仍返回 admin 且状态 ready。
- 运行时约束复核：`user=65534:65534`、`readonly=true`、`Memory=1073741824`、`cap_drop=[ALL]`、非特权；MQTT TLS 1.3 证书 `CN=mqtt.muyin.site` 经私有 CA 校验通过。
- 收尾执行 `down -v` 并删除临时私有目录；未连接目标服务器，未执行线上部署或真实数据库迁移。

边界：本地隔离运行不是 Debian 目标机部署、远程 CI 或线上升级；HTTPS/网络、真实数据库迁移与设备仍待目标环境验收；升级后会话卷首次为空，已登录用户需要重新登录一次。

本项 04 提交 `7b8e7ef` 已推送并 `git ls-remote` 核对 `origin/main` 为该提交；[Software checks 44098970301](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/44098970301) 八个 job 全部 success。远程 CI 只构建固件预设，不解析也不运行 1Panel 编排，不能替代上面的本地隔离运行或目标机验收。
