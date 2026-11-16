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
| HW-10 | 基础硬件“小爱同学”语音唤醒 | 已实现并完成初步实机验收；固定双模型、缓存/PSRAM 预算、延迟重建，PTT 与隐私/恢复保护保留 | 自动连续 3 轮通过；用户确认人声 2 轮+BOOT 1 轮均正常，无闪屏/卡住；本地主机/ASan/UBSan、10 项 LVGL、voice/default 构建通过。同一会话约五分钟无重启/显示失败，已推送 `3eacd00`；远程 CI 运行中，见末尾 |
| SW-01 | 设备首版与远程配置 | 软件已验证；设备提交 `959a4a4`，14 项设备切片及管理闭环见 [09](09-software-delivery.md)/[11](11-product-software-closure.md) | 保留硬件验收，不重新列为待编码 |
| SW-02 | 运行维护、恢复、更名、保留策略 | 软件已验证；见 [13](13-operational-software.md)/[14](14-recovery-and-device-names.md) | 真实库未迁移/清理，崩溃采集与恢复待实机 |
| SW-03 | 公开记忆及语音桥接 | 本地契约与 TLS 全链已验证；见 [15](15-memory-lifecycle.md)–[17](17-voice-bridge-regression.md) | 真实 Halo/OpenViking/供应商采用待验收 |
| DEL-01 | CORE 版本保护与离线交付 | CORE 基线 `913b3af`；CLI 7 项、CORE 41 通过/3 跳过、包内实际 JAR Linux 启动/重启通过；见 [交付记录](../deployment/README.md) | 两仓已推送；04 文档/工具提交 `7943cc6`。保留该基线 ZIP，后续包另记 DEL-02/DEL-04 |
| DEL-03 | Linux 凭据文件轮换验证 | 04 `69b6308` 已推送；实际包内 JAR/CLI、Linux 私有卷/UID 65534 的 admin 过渡/到期/撤销/异常拒绝/修复与重启通过；见 [20](20-credential-rotation.md) | Memory MCP/stdio 后续验证见 DEL-05；目标 systemd/真实用户目录仍待验收 |
| DEL-04 | 成长镜像配套离线包 | CORE `3e38ee5` / 04 `69b6308` 干净快照，18 文件；包内实际 JAR 的 Linux 启动/重启、V1–V12、admin 文件轮换通过；见 [21](21-growth-sync.md) | 包仅保留本地忽略目录；目标机、生产 TLS、真实供应商及实机仍待验收 |
| DEL-05 | Linux Memory MCP/stdio 热轮换 | 04 `f99a118` 已推送；同一 `3e38ee5` 包实际 JAR/CLI/stdio 通过过渡、到期、撤销、范围隔离、异常拒绝/恢复；同一 stdio 进程不重初始化、不转发/重放失败请求；见 [20](20-credential-rotation.md#linux-memory-mcp-与-stdio-验证) | 原 admin 模式/交付 CLI 10 项通过；[43384944971](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43384944971) 六个 job 全绿；真实 provider、生产 TLS/systemd/不同用户目录仍待验收 |
| DEL-06 | 管理台 Docker 线上部署方案 | CORE `9026e41` / 04 `5f9c15c` 已推送并核对远端；源码镜像/Compose/私有初始化/健康检查与 [部署说明](../deployment/README.md#管理台-docker-部署del-06)；本地 CLI 11 项、完整构建 62 通过/5 跳过、实际 Linux Compose 六组检查通过 | [Docker CI 43398869944](https://github.com/liuyiwuqing/nexus-core/actions/runs/43398869944) 全步骤及 [04 CI 43398982965](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43398982965) 七个 job success；尚无目标服务器，公网证书/真实升级恢复/生产资源与设备仍待验收 |
| DEL-07 | 1Panel 拉取镜像与无源码初始化 | CORE `043f134` 已推送并核对远端；私有 GHCR 发布/双架构/回拉验证工作流、独立初始化镜像、普通 env 的完整三服务编排及 [部署说明](../deployment/README.md#1panel-镜像部署del-07) 已补齐；本地 CLI 11 项、原部署六组、新镜像部署七组通过，actionlint 通过；发布运行 [43584412344](https://github.com/liuyiwuqing/nexus-core/actions/runs/43584412344) 成功，两个 digest 回拉后 7 项验证通过 | 已有用户提供的 Debian/1Panel 信息，目标机、HTTPS/网络与设备仍待验收 |
| DEL-08 | 1Panel 本地镜像编排 | 本地 7 组检查通过；[部署文件](../deployment/1panel/docker-compose.yml)与[使用说明](../deployment/1panel/README.md)使用 CORE 完整提交标签/amd64/禁止自动拉取，基础服务缺失时拉取，保留原私有目录/项目名/卷/安全配置；04 `1436113` 已推送并核对 origin/main | 实际已发布 amd64 CORE 在本机模拟运行，PG/MQTT/初始化为本地 arm64；解析、缺镜像拒绝、启动/鉴权/TLS、重建保留通过；[CI 43729640797](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43729640797) 运行中，目标服务器/HTTPS/设备未验收 |
| CI-01 | 首次远程 CI 与容器 Git 所有权 | 首次运行的交付/桥接检查通过；四预设在 Git 源码清单处退出 128，尚未编译。真实 Git 所有权模拟复现并建立回归 | `5183050` 修复 Git 所有权，`43169dd` / `1ad19cf` 修复 Linux C17 的 `strnlen` / `strlcpy` 依赖；本地主机检查通过；远程 OTA `0910e5f` 的 [43366086540](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43366086540) 六个 job 全绿 |
| SW-04 | 远程 OTA 管理 | 软件已验证；CORE 配套 `2808ed7` 已推送，协议、设备事务、管理台/审计/ACL 见 [19](19-remote-ota.md)；四固件预设、主机/实际 Java–MQTT–C、浏览器通过 | 04 `0910e5f` 已推送、远程 CI 六个 job 全绿；生产钥/HTTPS 与实机 OTA 仍待验收 |
| SW-05 | CORE/记忆工具平滑凭据轮换 | 软件已验证；双令牌/到期/撤销、稳定任务归属、stdio 文件重载、CLI 与发布预检见 [20](20-credential-rotation.md) | CORE `9559429` / 04 `ccd3afa` 已推送；设备及上游服务凭据仍走各自维护流程，不经普通配置下发秘密 |
| SW-06 | 成长跨端同步 | SW-06-A 只读镜像软件已验证，CORE `3e38ee5` / 04 `08a8ad3` 已推送；四预设、实际 C–MQTT–CORE、主机及浏览器见 [21](21-growth-sync.md) | 后续 `69b6308` 的 [43370460926](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43370460926) 六个 job 全绿；奖励下发仍为二期，先定义身份/来源/去重/迁移，不直接相加覆盖旧档 |
| SW-07 | 全双工/播放中声学打断 | SW-07-A 并行 HAL/有限诊断软件已验证，`9376eda` 已推送；主机 ASan/UBSan、隐私门控、五预设及源码/产物核对通过；见 [22](22-audio-duplex-diagnostic.md) | 后续 `82edd36` 的 [43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 七个 job 全绿；普通 AI 仍半双工，真实并行 I/O、参考对齐/漂移/声学打断仍待验收 |
| SW-08 | 资源/多语言/更多平台 | 后续范围；中文 UI 和 Mac 扩展已实现 | 当前优先运维闭环，不擅自引入新基础设施 |
| CONSOLE-01 | 测试消息未知结果重试 | CORE `07e3c53` 已推送；固定逻辑提交 ID/正文、导航恢复、确认后释放、拒绝/退出清理。新增浏览器 33 项与既有 78 项通过，CORE 59 通过/5 跳过、JAR 资源一致；见 [23](23-console-completion.md) | 页面刷新/关闭后不恢复正文，应先核对列表；不是跨浏览器或设备展示保证。04 配套检查/文档同项提交 |
| CONSOLE-14 | Naive UI 与管理台界面统一 | 软件本地已验证：三组导航/手机抽屉、消息筛选与导出、设备卡片、统一按钮与样式 nonce；见 [23](23-console-completion.md#console-14--naive-ui-与界面统一) | 完整 CORE 113 通过/5 条件跳过，19 组原页面 750 次与新增主题 21 次检查通过；CORE `9dc7ad9` 已推送并核对远端；JAR 三资源/20 迁移一致，04 配套与 CI 见末尾，部署/设备验收独立 |
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

实现提交 `3eacd00c07db527e2aa663cdd6897f11aa7ff5ce` 已正常推送，并以 `git ls-remote origin refs/heads/main` 核对远程一致。对应 [43744450764](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43744450764) 已启动：两项辅助检查成功，default/voice/full/off/tones/duplex 六个固件 job 仍在构建；不宣称本项远程 CI 已全部通过。


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
