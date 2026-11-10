# 08 · 目录结构与生产级架构评审

状态：评审中（2026-11-10）。S1 可构建骨架已完成（`idf.py build` 通过，0 warning）；S2 组件化已完成（`contracts`/`core`/`svc_growth` 组件、模块开关、依赖图检查、主机测试）；S3 契约与配置已完成（5 个 JSON Schema、C/TS codegen、扩展 manifest 与开关、事件契约扩展）；S4 温湿度纵向切片代码已完成（`pet_hal`/`bsp_dodo_s3_lcd`/`storage`/`svc_climate`/`presentation` 与 5 组主机测试，实机验收待硬件）。已确认 Q1（独立 Git 仓库）、Q2（冻结 `02`，`03` 作 donor）、Q3（方案 C：消息中心归 `nexus-core`，AI 网关独立）、Q4（独立 `model` 分区 1 MB）、Q5（`board.json` 生成 C/Kconfig）、Q6（`presentation` 独立）、Q8（管理台复用 `nexus-core`），以及 A/B 组决策。评审对象：工作区根目录、`04-ai-desktop-pet` 骨架，以及被复用的 `02-desktop-pet`、`03-xiaozhi-voice`、`nexus-core`、`mac-bridge`。

结论：分层方向正确。S1 已把“不可构建”变为可构建骨架，S2 已建立由构建系统强制的组件边界，S3 已把配置、消息与扩展契约变成可校验的单一事实源；剩余问题集中在测试门禁、资源管线与发布流程，进入功能开发前仍需补齐 P0-3、P0-4、P0-5 的落地项和高风险项。

## 1. 评审方法与证据

本次只做静态评审，不烧录、不改 02/03、不读取凭据。证据来自：

- 目录清单与文件数量统计。
- `02/03` 的 `main/CMakeLists.txt`、分区表、`.gitignore`。
- `nexus-core` 的 Maven 与前端结构。
- `04` 的 README、`firmware/README.md` 与 `00-architecture.md` 全文。
- 未执行构建与实机验证；涉及容量与时序的判断均标注为“需实测”。

关键事实：

| 对象 | 事实 | 含义 |
| --- | --- | --- |
| 工作区根 | 不是 Git 仓库；`02`、`03` 各自独立仓库 | 跨工程改动无法原子提交、回滚和统一 CI |
| `02-desktop-pet` | `main/` 49 个文件，C/H 约 6.4k 行，单组件平铺 | 业务、驱动、UI、协议混在同一编译单元 |
| `03-xiaozhi-voice` | `main/CMakeLists.txt` 1589 行、145 个二级板级目录、73 个托管组件；`.gitignore` 忽略 `dependencies.lock` | 板型分支与依赖漂移风险高，构建不可复现 |
| `04-ai-desktop-pet` | 37 个目录（含根）、0 个构建文件；`00`–`07` 八篇共 1743 行，本次新增 `08` | 目录是假设，未被编译器验证 |
| `nexus-core` | 单 Maven 模块、11 个领域包；前端 `src/` 平铺 12 个源文件（9 个 Vue 面板） | 当前规模可用，继续增长会出现前端与接口边界问题 |
| `mac-bridge` | 单文件进程 + 测试，标准库实现 | 边界清晰，适合继续作为本机执行适配器 |

## 2. 当前目录结构的优点

1. **部署单元分离清楚**：`firmware`、`server`、`bridge`、`config`、`resources`、`extensions`、`tools`、`tests` 各自有明确职责，没有把所有东西塞进固件目录。
2. **文档按模块拆分**：`00`–`07` 每篇独立，包含选型、步骤、验收，符合“逐模块确认后再开发”的协作方式。
3. **扩展能力独立**：`extensions/device_control`、`extensions/servo` 不在核心业务目录内，方向正确。
4. **复用优先**：明确复用 `nexus-core`、`mac-bridge`、`03` 的语音底座，而不是另起一套同构实现。
5. **分层意图明确**：`app/core/modules/hal/ui/storage` 的划分能表达“业务与硬件分离、跨模块走事件”的目标。
6. **测试分层有意识**：根 `tests/` 与 `firmware/tests/` 分开，说明已经考虑契约测试与固件测试的差异。
7. **命名可读**：目录名直接对应业务能力，新成员和 Agent 都能快速定位。

这些是继续演进的基础。问题不在“分得不够多”，而在“分层没有被构建系统和契约强制”。

## 3. 问题清单

严重度定义：

- **P0 阻断**：不解决就无法可靠构建、发布或继续迭代。
- **P1 高风险**：短期能跑，但会在 1–2 个迭代后造成返工或线上故障。
- **P2 优化**：影响效率、可维护性或一致性，可排期处理。

### P0-1 骨架不可构建（S1 已修复）

**现象（评审时）**：`04` 没有 `firmware/CMakeLists.txt`、`main/CMakeLists.txt`、`sdkconfig.defaults`、`partitions/*.csv`、`idf_component.yml`、`Kconfig.projbuild`、`.gitignore`、CI 配置。`README` 明确写着“构建文件在架构确认后再生成”。

**风险**：目录结构只是假设。模块能否编译、依赖是否成环、分区是否放得下、LVGL/PSRAM 配置是否可用，全都没有证据；后续任何“按目录开发”都可能在第一轮构建时推倒重来。

**建议**：先做“可构建骨架”（walking skeleton）：

- 根 `firmware/CMakeLists.txt` 只做 `project()`、`EXTRA_COMPONENT_DIRS`、分区表选择。
- `main` 只保留 `app_main`，启动事件总线和一个空模块。
- 提交 `sdkconfig.defaults`（16 MB Flash、8 MB PSRAM、自定义分区、LVGL 基础配置）。
- 提交 `partitions/16m.csv` 与容量预算表。
- CI 执行 `idf.py build`，产出 size report。

**已完成（2026-11-10，S1）**：已提交 `firmware/CMakeLists.txt`、`main/CMakeLists.txt`、`sdkconfig.defaults`、`partitions/16m.csv`、最小事件总线与启动诊断；`idf.py build` 通过，0 warning，镜像 181,068 B；`idf.py size` 可产出报告。S1 事件总线为固定 32 B payload、FIFO，队满丢新并计数；优先级队列、correlation id 与 schema version 留到 S3。

**验收**：`idf.py build` 通过；`idf.py size` 输出 app/DRAM/PSRAM 占用；分区表通过 `parttool.py` 校验；不改 `02/03`。

### P0-2 模块边界没有由构建系统强制（S2 已建立机制）

**已完成（2026-11-10，S2）**：`main` 收敛为组合根，只保留 `main.c`、`app/app.c` 与 Kconfig。已建立三个组件：

- `components/contracts`：`pet/event.h` 跨模块契约，纯 C、不依赖 ESP-IDF，可在主机上直接编译测试。
- `components/core`：事件总线实现，`REQUIRES contracts`。
- `components/svc_growth`：空业务模块，由 `CONFIG_PET_MODULE_GROWTH_ENABLED` 控制是否编入。

验证结果：默认构建 0 warning、镜像 181,240 B；关闭成长模块后构建通过，ELF 中 `pet_growth_init` 符号数为 0；`tools/ci/check_deps.py` 报告 3 个组件无依赖环；`tools/ci/test_host.sh` 运行事件契约主机测试通过。`hal`/`bsp_*`/`storage`/`presentation`/`ui_lvgl` 随首个真实实现落地，不建空壳组件。

**现象**：计划把 `ai_agent`、`message_hub`、`climate`、`growth` 放在 `firmware/main/modules/`，而 `main` 在 ESP-IDF 中是单一组件。`02` 已经展示了后果：49 个文件平铺在 `main/`，一个 `idf_component_register` 注册全部源码，任何文件都能 `#include` 任何头文件。`03` 则是 1589 行 CMake 加板级 `if/else` 链。

**风险**：

- 模块可以绕过契约直接调用彼此，低耦合只停留在文档。
- 无法用 `REQUIRES`/`PRIV_REQUIRES` 发现循环依赖和未声明依赖。
- 扩展无法干净地编译期裁剪；关闭一个能力仍可能被链接进来。
- 全量重编，编译并行度低，定位慢。

**建议**：`main` 只做组合根（composition root），业务与基础能力全部成为 ESP-IDF 组件：

```text
firmware/
├── main/                    # 只含 app_main + bootstrap，最多 2 个源文件
├── components/
│   ├── core/                # 事件总线、生命周期、结果码、时间、日志
│   ├── contracts/           # 跨模块公开头文件与 schema
│   ├── hal/                 # 纯接口，不含板级实现
│   ├── bsp_dodo_s3_lcd/     # 板级引脚、外设初始化、能力清单
│   ├── storage/             # NVS/LittleFS、迁移、CRC
│   ├── svc_ai_agent/
│   ├── svc_message_hub/
│   ├── svc_climate/
│   ├── svc_growth/
│   ├── presentation/        # 状态→表情/动作映射，纯逻辑
│   ├── ui_lvgl/             # LVGL 适配层
│   └── diag/                # 指标、故障码、coredump 解析
└── extensions/              # 可选组件，CMake 按开关加入
```

每个组件只暴露 `include/`，`src/` 私有；`CMakeLists.txt` 显式声明依赖。新增一条 CI 检查：扫描公共头文件的 include 关系，禁止 `svc_*` 互相包含私有头。

**验收**：删除任一 `svc_*` 组件后，除 `main` 的组合代码外无编译错误；`idf.py depgraph` 或脚本检查无环；扩展关闭时二进制中不出现其符号。

### P0-3 Flash 与语音模型预算（Q4 已确认，待落地）

**现象（评审时）**：`00-architecture.md` 的 16 MB 分区表没有 `model` 分区，只有 3 MB `assets`；但技术栈选择了 ESP-SR WakeNet。`03` 的 v1 分区表明确保留 `model`（960 KB，自定义唤醒词为 3.9 MB），v2 则把模型并入 8 MB `assets`。

**风险**：

- WakeNet 模型没有明确存放位置和大小，语音功能可能在链接或运行期直接失败。
- `assets` 同时承载中文字体、表情图、动画帧、提示音，预算不足会挤压表现资源。
- 4 MB OTA 槽要容纳 ESP-SR、LVGL、TLS、音频与业务代码，未做 size report 前不能视为可行。
- 没有 `factory` 分区或明确 USB 恢复流程，双槽 OTA 都失败时设备变砖。

**已确认（2026-11-10，Q4）**：采用独立 `model` 分区，不并入 `assets`。

- 分区：`model` 从 `0xE20000` 起 1 MB；`assets` 扩到 4 MB（`0xA20000`–`0xE20000`）；`coredump` 移到 `0xF20000`；保留 832 KB。
- 依据：当前 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y` 的 `srmodels.bin` 实测 291,069 B，1 MB 有约 3.5 倍余量。
- 加载：ESP-SR 使用 `esp_srmodel_init("model")` 直接 mmap，不经过 `assets` 索引；模型不随应用 OTA 更新。
- 边界：自定义唤醒词约 3.9 MB，首版不支持；启用需破坏性重分区并 USB 重刷。

**建议（剩余）**：

1. 提交 `tools/model.py`：按选型打包 `srmodels.bin`，CI 校验 ≤960 KB。
2. 在 CI 中加入 `idf.py size` 与资源打包体积门禁，超过预算直接失败。
3. 定义可验证的 USB 恢复流程；OTA 必须支持回滚。
4. 用实机 `esptool flash_id` 与 `parttool.py` 核对偏移，不靠文档推算。

**验收**：分区表可由 `parttool.py` 校验；`srmodels.bin` 能从 `model` 分区加载；app/DRAM/PSRAM 与资源体积均在预算内；双槽升级、回滚与 USB 恢复在实机通过。

### P0-4 仓库与版本边界（Q1 已确认，待落地）

**现象（评审时）**：工作区根不是 Git 仓库；`02`、`03` 各自独立；`04` 的设计文档原在根 `docs/` 下，现已迁入 `04/docs/`；`03` 的 `.gitignore` 忽略 `dependencies.lock`。

**风险**：

- 跨工程改动（协议、文档、共享组件）无法原子提交和回滚。
- `04` 的代码与设计文档可能版本不一致。
- `03` 的依赖不可复现，今天能构建、明天可能因上游版本变化失败。
- 没有统一 CI，无法保证“合入即构建”。

**已确认（2026-11-10）**：`04` 使用独立 Git 仓库，与 `02`、`03`、`nexus-core`、`mac-bridge` 分开版本化；不改为 monorepo。

落实要求：

- 文档随代码放在 `04/docs/`，已从工作区根 `docs/ai-desktop-pet/` 迁入；根 README 只保留入口链接。
- `04` 必须自己拥有 `VERSION`、`CHANGELOG.md`、`dependencies.lock`；锁文件必须提交。
- 跨仓库复用通过版本化契约或抽取组件完成；`nexus-core` 的接口以 OpenAPI/AsyncAPI/JSON Schema 形式在 `04/integration/contracts/` 固化版本。
- CI 在 `04` 仓库内运行；契约变更时由消费方测试锁定兼容性，不依赖工作区根脚本。
- `03` 忽略 `dependencies.lock` 的问题不直接修改其仓库；`04` 不复用其构建配置，只抽取源码与硬件经验。

**验收**：任一发布版本能由 commit + lockfile 重建；契约变更能追溯到消费方；CI 在 PR 上执行构建与测试。

### P0-5 服务端归属（Q3 已确认，待落地）

**现象（评审时）**：`04/server/ai-gateway/` 与 `04/server/message-gateway/` 是空目录，`00-architecture.md` 与项目 README 对 `nexus-core` 的职责描述不一致，待确认项第 4 条仍在问归属。

**风险**：空目录会诱导后续实现另起消息服务，形成第二套鉴权、审计与运维；AI 会话与消息中心混在同一服务内，会让音频扩容与消息可靠性互相牵制。

**已确认（2026-11-10）**：采用方案 C，按能力拆分归属。

- 消息中心归 `nexus-core`：第三方 Webhook/Halo 接入、消息去重与保留、设备注册与 MQTT 主题 ACL、配置下发、管理台。`04` 不新建 `message-gateway`。
- AI 网关独立：`04/server/ai-gateway/` 承载 ASR/LLM/TTS 会话编排、供应商适配与密钥、上下文策略；已确认首版复用 `03` 的小智 WebSocket 服务作为实现，对外仍以 `ai_session` 契约暴露。
- 集成边界：`04/integration/` 只放与 `nexus-core` 的 OpenAPI/AsyncAPI/JSON Schema、部署配置与联调脚本，不放业务代码。
- 鉴权：设备在 `nexus-core` 完成身份校验并换取短期 AI 会话令牌；`ai-gateway` 只校验该令牌，不直连设备管理数据，也不共享数据库。
- 部署：`nexus-core` 不代理音频流；MQTT 消息与 WSS 音频分别走各自服务。

**落地要求**：删除 `04/server/message-gateway/`；`00` 第 8 节部署拓扑、技术栈表与待确认项同步改写；`04/README.md` 与 `server/README.md` 不再声称 `nexus-core` 负责 AI 编排。

**验收**：`04` 内不存在消息服务空目录；消息与 AI 各只有一个归属；契约测试覆盖设备 ↔ `nexus-core` 的消息链路与设备 ↔ `ai-gateway` 的会话链路。

### P1-1 HAL 与 BSP 混在一起

**现象**：`firmware/main/hal/` 计划放 I2S、SPI、GPIO、I2C、RMT、Wi-Fi，同时 `firmware/boards/dodo-s3-lcd/` 放板级配置；`00` 又要求 `hal` 只做外设初始化与读写。

**风险**：板级引脚、时序、初始化顺序会渗进 `hal`，第二块板出现时只能加 `#ifdef`。`03` 的 145 个板级目录就是这种模式的极端形态。

**建议**：

- `hal/` 只定义接口：`audio_in`、`audio_out`、`display`、`sensor_bus`、`actuator_bus`、`net`、`time`。
- `bsp_dodo_s3_lcd/` 实现接口，持有引脚表、时钟、初始化顺序和能力清单 `board.json`。已确认（Q5）：`board.json` 为唯一事实源，构建生成 C 头与 Kconfig 默认值。
- 业务模块只依赖接口，不依赖具体 BSP；`app` 负责注入。
- 板型选择通过 Kconfig 选 BSP 组件，不写长 `if/else` 链。

**已完成（2026-11-10，S4）**：`pet_hal` 只提供 `pet_climate_sensor_t` 接口、DHT11 解码纯函数与 fake 实现；`bsp_dodo_s3_lcd` 提供 RMT 读取实现；`svc_climate` 只依赖接口，由 `app` 注入 `pet_bsp_climate_sensor()`。屏幕/音频等其余 HAL 接口仍待后续切片。

**验收**：新增一块板只新增一个 BSP 组件与一个 profile，不改业务模块；业务模块的主机测试不需要真实硬件。

### P1-2 UI 与表现逻辑耦合，资源管线缺失

**现象**：`ai_agent` 的职责包含“表情动作编排”，`message_hub` 包含“渲染调度”，`ui/` 又负责“表情/动作表现”。资源放在根 `resources/`，但没有转换工具、清单、分区映射和体积门禁。

**风险**：

- 业务模块知道 UI 细节，换一套 UI 或增加无屏版本要改业务代码。
- 表情/动作规则散落在 AI 与 UI 两处，状态冲突时无法判断谁负责。
- 字体、图片、动画的格式与 LVGL 版本绑定，缺少生成步骤会导致“仓库里有图、固件里没有”。

**建议**：

- 已确认（Q6）：新增 `presentation/`，只做 `pet_state + event → expression/action` 的纯函数映射，可主机测试。
- `ai_agent` 只产出状态与文本/音频事件，不知道表情名称。
- `message_hub` 只产出“展示意图”（优先级、停留时长、卡片类型），不负责绘制。
- `ui_lvgl/` 只消费展示意图与表现快照，负责 LVGL 对象与动画。
- 资源管线：`assets-src/` 存设计源文件，`tools/assets.py` 生成 LVGL 二进制与 `manifest.json`，构建时校验尺寸/格式/授权，产物进 `assets` 分区。
- 明确支持语言与字体子集，不复制 `03` 的 45 个 locale。

**已完成（2026-11-10，S4，部分）**：`presentation` 组件已落地 `pet_climate_card_render()`，输入快照、输出纯文本卡片，主机可测；`ui_lvgl` 尚未创建，首版由 `app` 把卡片写入串口日志，LVGL 实屏随板级 BSP 落地。

**验收**：删除 `ui_lvgl` 后业务模块仍可编译与主机测试；同一状态在主机快照测试中产出稳定表现；资源体积在预算内。

### P1-3 扩展机制只有目录，没有契约

**现象**：`extensions/device_control/`、`extensions/servo/` 只有 README；没有 `CMakeLists.txt`、公共头、manifest、版本号、能力声明和契约测试。

**风险**：扩展与核心的边界靠口头约定；关闭开关、升级契约、第三方接入时会出现 ABI/行为漂移。

**建议**：定义扩展包规范：

```text
extensions/<name>/
├── extension.json        # id、version、contract_version、requires、capabilities、risk
├── CMakeLists.txt
├── include/<name>/api.h  # 唯一公开头
├── src/
├── test/
└── README.md
```

- 根 CMake 只收集 `extension.json` 中启用的扩展。
- 编译期校验 `contract_version` 与核心匹配；运行期注册能力时再次校验。
- 扩展不得包含核心私有头；只能使用 `contracts/` 与事件总线。
- 每个扩展必须有主机契约测试和关闭开关下的构建测试。

**验收**：关闭所有扩展可构建；启用单个扩展不修改核心源码；契约版本不匹配时构建或启动明确失败。

**已完成（2026-11-10，S3）**：`extension.json` 规范落在 `config/schemas/extension.schema.json`；`device_control`、`servo` 均有 manifest、唯一公开头、组件 CMake 与主机清单测试；核心契约版本在 `pet/extension.h`，`tools/ci/check_contracts.py` 校验 manifest、契约版本、目录结构与 `CONFIG_PET_EXT_*` 开关。默认构建中两个扩展不产生符号；启用构建出现 `pet_ext_*_init` 且无需修改核心源码。运行期能力注册与适配器行为留到 S6。

### P1-4 三层配置没有单一事实来源

**现象**：板级引脚在 `firmware/boards/`，功能开关在 `config/profiles/`，运行配置在 NVS，服务端还有一套配置；`config/schemas/` 为空。

**风险**：同一参数多处定义，默认值不一致；配置升级无迁移；凭据可能被误放进 `config/profiles`。

**建议**：

- 以 `config/schemas/*.schema.json` 为唯一 schema，生成 C 头文件与 TypeScript 类型。
- `config/profiles/*.json` 只引用 `board_id` 与功能开关，不重复引脚。
- NVS 配置带 `cfg_version`，启动时校验 schema 并执行迁移；校验失败进入安全默认值，不静默覆盖。
- 凭据只走配网/服务端私有 env；`config/` 内禁止出现 token、密码、证书私钥。
- CI 校验所有 profile 与 example 通过 schema。

**验收**：修改 schema 后生成代码同步更新；旧版本 NVS 能迁移或明确拒绝；仓库扫描无凭据。

**已完成（2026-11-10，S3）**：`config/schemas/` 提供设备配置、设备 profile、消息信封、回执、扩展 manifest 五个 schema；`tools/schema_codegen.py` 无第三方依赖，生成 `firmware/components/contracts/include/pet/config_generated.h` 与 `integration/contracts/generated/pet-contracts.ts`；`config/profiles/dodo-s3-lcd.json` 与 `config/examples/*.json` 由 `tools/ci/check_contracts.py` 校验，并检查生成产物是否过期。NVS `cfg_version` 迁移与启动期校验随 `storage` 组件落地。

### P1-5 事件总线契约不完整

**现象**：`pet_event_t.payload` 是 `void *`，注释写“发布者持有，分发完成后释放”；没有 correlation id、payload 版本、错误分类、背压策略；高频音频电平与状态事件共用一条队列。

**风险**：

- 异步订阅者 deferred 处理时 payload 已释放，出现悬空指针。
- 请求/响应（意图、ACK、执行结果）无法关联。
- 队列满时丢弃策略可能丢关键状态事件。
- `esp_err_t` 无法表达“可重试/不可重试/需要确认”等领域错误。

**建议**：

- 事件改为固定大小内联 payload（如 64 B）+ 可选引用计数块；明确 `retain/release`。
- 增加 `correlation_id`、`schema_version`、`source`、`sequence`。
- 定义 `pet_result_t { code, retryable, message_id }`，替代裸 `esp_err_t`。
- 关键事件（状态变更、ACK、故障）与可合并事件（音频电平）分队列；后者用 latest-value 槽。
- 明确队列满时的优先级与指标，禁止静默丢弃。

**验收**：主机测试覆盖 payload 生命周期、队列满、乱序与重复事件；ASan 无悬空引用。

**已完成（2026-11-10，S3）**：`pet_event_t` 改为 64 B 内联 payload，补齐 `correlation_id`、`schema_version`、`priority`、`source`、`sequence`，固定 ≤96 B；队列按优先级拆为 4 条（4/16/8/4，总计 32），高优先级先出队，队满按优先级计数；`pet_event_prepare()` 统一初始化，主机测试覆盖字段校验、长度边界与清零。`pet_result_t` 领域错误码、可合并事件 latest-value 槽与事件池随首个真实业务模块落地。

### P1-6 测试分层与 CI 缺失

**现象**：只有 `tests/README.md` 与 `firmware/tests/.gitkeep`；没有统一测试入口、fixtures、覆盖率或 CI。`02` 用 `tools/check.py`，`03` 用 Python unittest，两者不一致。

**风险**：每个模块自建测试方式，回归成本高；主机测试通过被误当成实机验收；没有门禁导致坏提交进入主干。

**建议**：

```text
firmware/tests/host/      # 纯 C，CMake/CTest 或 Python 驱动，ASan/UBSan
firmware/tests/target/    # 目标板测试，串口夹具
tests/contract/           # JSON Schema、MQTT、Webhook、Bridge
tests/hil/                # 实机脚本与检查表
tests/fixtures/           # 固定报文、配置、资源样例
tools/test.py             # 唯一入口，分层执行
```

CI 分阶段：静态检查 → 主机测试 → 构建与 size gate → 契约测试 →（可选）HIL。覆盖率只对纯逻辑模块设门槛，不用 UI 静态细节凑数。

**验收**：新模块必须带主机测试；CI 能在无硬件环境跑完前三阶段；HIL 结果单独记录。

### P1-7 02/03/04 三度分叉风险

**现象**：`02` 有离线宠物、传感器、成长、OTA；`03` 有语音、DODO 板、消息收件；`04` 计划两者都复用。当前复用方式是“抽取思路”，没有共享组件仓库或同步机制。

**风险**：同一份 DHT11 解码、显示初始化、NVS 存档、宠物表情出现三份实现；修一个 bug 要改三处，且行为逐渐不一致。

**建议**：

- 明确 `02` 冻结为离线参考实现，不再加新功能；`03` 作为语音与 DODO 板 donor。
- 先抽取稳定组件：`dht_decode`、`st7789` 初始化、`pet_behavior`、NVS 双槽存档；放入 `04/firmware/components` 或独立组件仓库。
- 每次抽取附契约测试与来源说明；不整段复制。
- 设定删除期限：`04` 稳定后，`02/03` 的重复实现标记 deprecated，不再双修。

**验收**：同一能力只有一个维护点；`02/03` 不再接收同类修复；抽取组件有主机测试。

### P1-8 可观测性与故障隔离缺失

**现象**：没有日志规范、指标注册、故障码表、coredump 解析、诊断页面；`00` 只提到安全模式与故障码。

**风险**：实机问题只能靠串口猜；无法量化音频欠载、LVGL 帧耗时、MQTT 重连、NVS 写入次数；线上问题无法复现。

**建议**：

- `diag/` 组件提供结构化日志（JSON 行）、指标计数器、故障码枚举、启动原因、栈高水位。
- 定义最小指标集：heap 最低值/最大连续块、各任务栈高水位、LCD 帧耗时、音频欠载、重连次数、NVS 写入次数、事件丢弃数。
- 提供设备诊断页与导出接口；coredump 分区与解析脚本。
- 安全模式明确降级顺序：保屏幕与输入，关网络与扩展，显示故障码。

**验收**：断网、传感器失败、音频失败、存储失败各有可观察指标与降级行为；coredump 能解析到任务与栈。

### P2 级问题

| 编号 | 问题 | 建议 |
| --- | --- | --- |
| P2-1 | `storage/` 与 `core/hal` 平级，层级归属不清 | 作为服务组件 `components/storage`，依赖 HAL 接口，不被 `core` 反向依赖 |
| P2-2 | `resources/` 与固件 `assets` 分区无映射 | 区分 `assets-src`（设计源）与 `assets`（构建产物），manifest 记录版本与体积 |
| P2-3 | `bridge/` 与 `mac-bridge` 重复 | 改为 `integration/mac-bridge/README.md` 指针，不建第二份代码目录 |
| P2-4 | 文档、ADR、CHANGELOG、VERSION 缺失 | `04/docs/adr/` 记录决策；发布物带版本与变更记录 |
| P2-5 | 依赖、许可证、SBOM 无策略 | 提交 lockfile；限制托管组件数量；生成许可证清单；CVE 扫描 |
| P2-6 | 构建产物与系统文件混入源码树 | 统一 `build/<variant>` 并 gitignore；清理 `.DS_Store`、`__pycache__` |
| P2-7 | `nexus-core` 前端平铺，接口无 OpenAPI | 前端按 `app/views/components/api/stores` 分层；后端补 OpenAPI/AsyncAPI |
| P2-8 | 无 `factory`/恢复流程 | 增加 factory 分区或文档化 USB 恢复；OTA 回滚纳入验收 |

## 4. `00-architecture.md` 的内部不一致

| 位置 | 现状 | 问题 | 建议 |
| --- | --- | --- | --- |
| 第 28–29 行 | 图中 `modules` 含 `device_control(ext)`、`servo(ext)` | 与项目 README“扩展不放 `main/modules`”冲突 | 图中把扩展画成旁路组件，或明确“逻辑视图/物理视图”区别 |
| 第 41 行 | `modules → core/storage/hal` | 业务直接依赖 HAL，无法主机测试 | 业务依赖 `contracts` 与端口接口，BSP 由 `app` 注入 |
| 第 47 行 | `ai_agent` 负责“表情动作编排” | 与 `ui` 职责重叠，业务知道 UI | 新增 `presentation`，AI 只发状态 |
| 第 48 行 | `message_hub` 负责“渲染调度” | UI 才是渲染方 | 改为“展示意图与优先级调度” |
| 第 85 行 | payload 由发布者持有，分发后释放 | 异步订阅者会悬空 | 内联固定大小 + 引用计数块，明确 retain/release |
| 第 113 行 | 契约固定放 `main/core/contracts/` | 组件化后 `main` 不再是业务容器 | 独立 `components/contracts` |
| 第 156 行 | 风险等级 3 级 | `05` 文档使用 L0–L3 四级 | 统一为 L0 只读、L1 可逆、L2 破坏性、L3 高危 |
| 第 160、170 行 | 公共契约暴露 `cJSON *` | 契约耦合 cJSON 与堆所有权 | 序列化字节 + schema，或 opaque 参数句柄 |
| 第 217 行 | `assets` 3 MB | 未含唤醒模型，资源预算不闭合 | 已确认：独立 `model` 1 MB，`assets` 4 MB |
| 第 234 行 | `app` 任务优先级 1 | 可能被 sensor/growth 饿死 | 做速率单调分析，事件分发与看门狗独立 |
| 第 240 行 | 内部 heap 目标 ≥80 KB | 未扣除 Wi-Fi/TLS/ESP-SR 峰值 | 分模块预算，实机测最低值与最大连续块 |
| 第 262 行 | 首版复用“小智 WebSocket 服务” | 与 `01` 的云 ASR/LLM/TTS 选型需对齐 | 已改为独立 `ai-gateway` 承载，小智服务只作为首版实现 |
| 第 271 行 | Opus 16 kHz 上行 | S3 编码 CPU/延迟未验证 | 做编码耗时与丢帧实测，保留 PCM 回退 |
| 第 311 行 | 待确认引脚差异 | 背光 GPIO42 与 02 的 BLK 3V3 不一致 | 以实物与 `boards/dodo-s3-lcd` 为准，先测量再定稿 |

## 5. 建议的目标目录结构

在保留现有优点的基础上，把“目录分层”升级为“构建单元分层”：

```text
04-ai-desktop-pet/
├── README.md
├── VERSION
├── CHANGELOG.md
├── .gitignore
├── .editorconfig
├── .clang-format
├── docs/
│   ├── README.md
│   ├── adr/                         # 架构决策记录
│   └── contracts/                   # 生成的 API/事件文档
├── firmware/
│   ├── CMakeLists.txt               # 工程入口，EXTRA_COMPONENT_DIRS
│   ├── sdkconfig.defaults
│   ├── sdkconfig.defaults.dodo-s3-lcd
│   ├── partitions/
│   │   ├── 16m.csv
│   │   └── README.md                # 分区与容量预算
│   ├── main/                        # 组合根：app_main + bootstrap
│   ├── components/
│   │   ├── core/                    # 事件总线、生命周期、结果码、时间、日志
│   │   ├── contracts/               # 公开事件与接口头
│   │   ├── hal/                     # 纯接口
│   │   ├── bsp_dodo_s3_lcd/         # 板级实现 + board.json
│   │   ├── storage/
│   │   ├── svc_ai_agent/
│   │   ├── svc_message_hub/
│   │   ├── svc_climate/
│   │   ├── svc_growth/
│   │   ├── presentation/            # 状态→表情/动作
│   │   ├── ui_lvgl/
│   │   └── diag/
│   ├── extensions/
│   │   ├── device_control/
│   │   └── servo/
│   ├── assets-src/                  # 设计源文件，不入固件
│   ├── assets/                      # 生成产物 + manifest
│   └── tests/
│       ├── host/
│       ├── target/
│       └── fixtures/
├── server/
│   └── ai-gateway/                  # 独立 AI 会话服务：ASR/LLM/TTS 编排、供应商密钥
├── integration/                     # 与 nexus-core 的集成：契约与部署，不放第二套消息服务
│   ├── contracts/                   # OpenAPI/AsyncAPI/JSON Schema
│   ├── deploy/                      # compose、env 示例（无凭据）
│   └── mac-bridge/                  # 指向 mac-bridge 的集成说明
├── config/
│   ├── schemas/
│   ├── profiles/
│   └── examples/
├── tools/
│   ├── build.py
│   ├── test.py
│   ├── assets.py
│   └── size_report.py
└── tests/
    ├── contract/
    └── hil/
```

与当前结构的主要差异：

1. `main/modules/*` 上移为 `components/svc_*`，由构建系统强制边界。
2. `hal/` 只留接口，新增 `bsp_*` 承载板级实现。
3. 新增 `presentation/`、`ui_lvgl/`、`diag/`，拆开表现映射、渲染与可观测性。
4. `server/` 只保留独立 `ai-gateway`；与 `nexus-core` 的契约、部署移到 `integration/`，消息中心不再建第二套服务。
5. `extensions/` 进入 `firmware/` 的组件收集路径，并带 `extension.json`。
6. `resources/` 拆成 `assets-src` 与 `assets`，资源变成可验证的构建产物。

## 6. 生产级架构原则

以下不是口号，每条都给出落地物与验收方式。

### 6.1 边界由构建系统强制

目录只是提示，`REQUIRES`、`PRIV_REQUIRES`、公共/私有头目录才是边界。业务模块之间不得直接 include，只能依赖 `contracts`。

**落地**：组件化目录、依赖图检查脚本、关闭任一模块仍可构建。

### 6.2 先有可构建骨架，再有功能

任何架构在第一次 `idf.py build` 之前都只是假设。先交付可构建、可启动、可观测的最小固件，再按模块加能力。

**落地**：P0-1 的 walking skeleton 与 CI。

### 6.3 Ports & Adapters

业务逻辑依赖接口（port），BSP/网络/存储提供实现（adapter）。业务模块可在主机运行，不依赖 FreeRTOS、I2S、NVS。

**落地**：`hal/` 接口 + `bsp_*` 实现 + 主机 fake；纯逻辑测试无需硬件。

### 6.4 契约优先且版本化

设备与服务端、模块与模块之间以 schema 为真相：消息信封、事件 payload、配置、存档、扩展 manifest 都有 `schema_version`。新增字段必须向后兼容，破坏性变更升主版本并写迁移。

**落地**：JSON Schema/OpenAPI/AsyncAPI；C 与 TypeScript 代码生成；契约测试覆盖旧版本样例。

### 6.5 配置单一事实来源

同一参数只能有一个权威定义。板级引脚归 BSP，功能开关归 profile，用户设置归 NVS，凭据归安全存储；schema 生成代码，CI 校验所有 profile。

**落地**：`config/schemas` + codegen + 启动校验 + 迁移测试。

### 6.6 资源管线可重复、可计量

设计源与固件产物分离；转换脚本版本化；manifest 记录资源 ID、格式、尺寸、哈希；构建门禁控制分区占用。

**落地**：`tools/assets.py`、`manifest.json`、size gate。

### 6.7 失败隔离与优雅降级

任一非关键模块失败不得拖垮屏幕与输入；网络、传感器、音频、扩展各自可降级；看门狗与安全模式有明确触发条件。

**落地**：模块生命周期 `init/start/stop`、健康状态、故障码、安全模式验收。

### 6.8 可观测性默认开启

日志、指标、故障码、coredump 是产品能力，不是调试补丁。每个模块定义自己的关键指标与失败计数。

**落地**：`diag/` 组件、诊断页、指标清单、coredump 解析。

### 6.9 测试金字塔与 CI 门禁

主机测试覆盖纯逻辑与状态机；契约测试覆盖跨端协议；目标测试覆盖驱动；HIL 覆盖真实音频、屏幕、断网、掉电。不同层级结果分开记录，不互相冒充。

**落地**：`tools/test.py` 分层入口、CI 阶段、覆盖率门槛、HIL 检查表。

### 6.10 发布工程化

SemVer、签名 OTA、A/B 回滚、factory 恢复、依赖锁、SBOM、许可证清单、变更记录。发布物必须可重建、可追溯。

**落地**：`VERSION`、`CHANGELOG.md`、`dependencies.lock`、OTA 签名与回滚测试、许可证清单。

### 6.11 安全基线

每设备独立身份与主题 ACL；TLS 校验；令牌可轮换；高风险能力二次确认并绑定请求；麦克风在静音/免打扰时门控；日志不写音频、私信、令牌、Wi-Fi 密码。安全启动与 Flash 加密需在量产前做明确决策。

**落地**：威胁建模、权限矩阵、密钥生命周期、日志脱敏检查。

### 6.12 文档即代码

架构决策写 ADR；接口文档由 schema 生成；运行手册与故障码随版本更新；文档链接纳入 CI 检查。避免“代码一套、文档一套”。

**落地**：`docs/adr/`、生成的契约文档、链接检查、发布前文档清单。

## 7. 迁移计划与验收

| 阶段 | 内容 | 退出标准 |
| --- | --- | --- |
| S0 结构定稿 | 确认本报告、目标目录、仓库策略、服务端归属 | 第 8 节决策有结论；`00` 不一致项修正 |
| S1 可构建骨架 | CMake、sdkconfig、分区、CI、空事件总线、诊断串口 | `idf.py build` 通过；size report 产出；CI 绿 |
| S2 组件化（已完成 2026-11-10） | `contracts`/`core`/`svc_growth` 组件、模块开关、依赖图检查、主机测试；其余组件随实现落地 | 关闭模块可构建；依赖图无环；主机测试可跑 |
| S3 契约与配置（已完成 2026-11-10） | 5 个 JSON Schema、C/TS codegen、扩展 manifest 与 Kconfig 开关、事件契约扩展；配置/消息/扩展校验入口 `tools/ci/check_contracts.py` | 配置与消息 schema 校验通过；扩展开关生效（关闭无符号、启用出现 `pet_ext_*_init`） |
| S4 首个纵向切片（代码完成 2026-11-10，实机待验证） | 温湿度切片：`pet/climate.h` 契约、`pet_hal` 接口与解码、`bsp_dodo_s3_lcd` RMT 读取、`storage` NVS 封装、`svc_climate` 滤波/告警/极值、`presentation` 卡片、5 组主机测试 | 主机：默认/fake/关闭模块三种构建通过；实机：采样、串口卡片、掉电存档待验收 |
| S5 网络能力 | 消息中心、AI 会话、TLS、重连、离线队列 | 断网/重连/重复消息/ACK 通过；延迟与内存达标 |
| S6 扩展能力 | 设备控制、舵机，默认关闭 | 关闭可构建；启用后白名单、确认、急停通过 |

原则：S1、S2 未完成前，不进入具体业务功能开发；否则会在错误边界上写代码。

## 8. 待确认决策

| 编号 | 决策 | 选项 | 建议 |
| --- | --- | --- | --- |
| Q1 | 仓库策略 | **已确认：独立仓库**；不采用 monorepo | `04` 自管文档、锁文件、CI 与 tag；跨仓库用版本化契约 |
| Q2 | 02/03 定位 | **已确认：冻结 02，03 作 donor** | `02` 不再修改；`03` 只读抽取语音/板级经验，稳定组件抽到 `04`；不跟随 03 上游升级 |
| Q3 | 服务端归属 | **已确认：方案 C** | 消息中心（Webhook/Halo、MQTT/ACL、配置、管理台）归 `nexus-core`；AI 网关独立在 `04/server/ai-gateway`；`04/integration/` 只放契约与部署 |
| Q4 | 语音模型存放 | **已确认：独立 `model` 1 MB** | `0xE20000` 起 1 MB，ESP-SR mmap；`assets` 扩到 4 MB；当前模型 284 KB；自定义唤醒词不在首版 |
| Q5 | 板级能力描述 | **已确认：`board.json` 生成 C/Kconfig** | 单一事实源；构建时生成 C 头与 Kconfig 默认值，避免两处漂移 |
| Q6 | 表现层归属 | **已确认：`presentation` 独立** | 纯函数映射，主机可测；`ui` 只消费表现快照，便于无屏版本复用 |
| Q7 | 安全启动/Flash 加密 | **已确认：开发不启用，量产启用** | 开发期保持可烧录；量产启用 Secure Boot + Flash Encryption；eFuse 不可逆，量产前完成密钥与签名流程 |
| Q8 | 管理台归属 | **已确认：复用 `nexus-core`** | 同一套鉴权、审计、设备管理与消息时间线，不建第二套后台 |

### A 组已确认（2026-11-10）

| 项 | 决策 | 说明 |
| --- | --- | --- |
| A1 AI 供应商 | 沿用 `03` 小智服务 | 首版不自建 ASR/LLM/TTS，不引入第二家供应商 |
| A2 唤醒词 | 内置 `wn9_nihaoxiaozhi_tts`（你好小智） | 模型约 291 KB，1 MB 分区可容纳；可换其他内置词；自定义词约 3.9 MB 不在首版 |
| A3 AI 网关 | 复用 `03` + `ai_session` 契约隔离 | 固件只依赖契约，替换实现不改 UI/成长/消息模块 |
| A4 安全基线 | 开发不启用，量产启用 Secure Boot + Flash Encryption | eFuse 不可逆；开发期不锁芯片 |
| A5 板级基线 | `03` DODO-S3-LCD，背光 GPIO42 PWM | S1 只落此板；其他板型后续独立 board profile |

### B 组已确认（2026-11-10）

| 项 | 决策 | 说明 |
| --- | --- | --- |
| B1 消息保留 | 全文 7 天，元数据 30 天 | 到期每日清理；审计与导出默认不含正文 |
| B2 MQTT Broker | Mosquitto 2.0.22 | 与 `nexus-core` 现有 compose/ACL/集成测试一致；集群再评估 EMQX |
| B3 中文字体 | Noto Sans SC 子集（SIL OFL） | 16/24 px 子集，授权文本随资源入库 |

## 9. 结论

当前目录结构的**方向是对的**：部署单元分离、模块文档独立、扩展能力外置、复用优先。但它还没有成为“生产级架构”，原因不是目录不够细，而是缺少四件事：

1. **构建系统强制的边界**：模块必须在组件层解耦，而不是靠目录和约定。
2. **闭合的容量与分区预算**：语音模型、资源、OTA 槽位必须先算清。
3. **版本化的契约与配置**：消息、事件、配置、存档、扩展都要有 schema 与迁移策略。
4. **可验证的工程门禁**：walking skeleton、CI、测试分层、size gate、发布与回滚。

建议先完成 S0/S1/S2，再回到 `01`–`06` 的功能方案确认。这样后续每个模块的实现都能落在稳定边界上，而不是在骨架期反复搬迁。
