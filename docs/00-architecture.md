# 00 · 总体架构

状态：当前软件架构（2026-11-13）。分层、消息/配置、语音、扩展与维护软件已实现，本地验证见 [11](11-product-software-closure.md)、[13](13-operational-software.md)、[15](15-memory-lifecycle.md)、[16](16-voice-memory-tools.md)与[17](17-voice-bridge-regression.md)；真实云端、部署及硬件验收待做。[08 结构评审](08-structure-review.md)保留历史决策。适用范围：`04-ai-desktop-pet` 固件、与 `nexus-core` 的集成契约、控制网关及扩展包；自建 AI 网关仍为预留。

## 1. 目标与非目标

### 目标

- 基础固件承载语音对话、消息展示和养成玩法；温湿度、摇杆、光敏和舵机为显式启用的可选扩展，硬件与菜单见 [27](27-hardware-and-input.md)。
- 模块之间低耦合：业务模块不直接调用彼此，只通过事件总线与版本化契约通信。
- 新能力以扩展包接入，不改核心模块；扩展可以编译期关闭、运行期禁用。
- 主机可测的逻辑与硬件相关代码分离；硬件失败可降级，不阻塞 UI。
- 复用现有 02/03/nexus-core/mac-bridge 的已验证能力，避免第二套同构实现。

### 非目标（首版）

- 不做全本地大模型推理；不做多设备集群；不做复杂 3D 渲染。
- 不做任意 shell/脚本执行；设备控制只允许白名单能力。
- 不在首版实现舵机闭环反馈与多路总线舵机。
- 不迁移 02/03 的 NVS；新工程使用独立分区与存档版本。

## 2. 分层与依赖方向

```text
┌─────────────────────────────────────────────────────────────┐
│ app/          启动、任务编排、事件分发、看门狗               │
├─────────────────────────────────────────────────────────────┤
│ modules/      ai_agent  message_hub  climate  growth         │
│               device_control(ext)  servo(ext)                │
├─────────────────────────────────────────────────────────────┤
│ core/         事件总线、模块生命周期、配置、契约、错误码     │
├─────────────────────────────────────────────────────────────┤
│ presentation/ 状态→表情/动作映射（纯逻辑，可主机测试）        │
├─────────────────────────────────────────────────────────────┤
│ ui/           LVGL 页面、组件、主题、动画                     │
├─────────────────────────────────────────────────────────────┤
│ hal/          I2S、SPI、GPIO、I2C、RMT、Wi-Fi、时间          │
├─────────────────────────────────────────────────────────────┤
│ storage/      NVS、LittleFS、版本化存档、CRC                 │
└─────────────────────────────────────────────────────────────┘
```

依赖只能向下：`modules → core/storage/hal`，`app → modules`，`presentation` 只依赖契约与状态快照，`ui` 消费事件、只读快照与 presentation 输出。禁止 `modules` 反向依赖 `app`，禁止 `hal` 依赖业务。

## 3. 模块职责

| 模块 | 职责 | 不负责 |
| --- | --- | --- |
| `ai_agent` | 唤醒、录音、ASR/LLM/TTS 会话、上下文、意图产出、状态事件 | 消息投递、成长数值、具体设备执行、表情绘制 |
| `message_hub` | 消息接入、校验、去重、优先级队列、渲染调度、ACK | 第三方平台协议细节、UI 绘制 |
| `net` | Wi-Fi station、MQTT over TLS、主题发布/订阅、重连与 LWT | 消息业务解析、渲染、存档 |
| `climate` | 温湿度采样、滤波、校准、异常检测、告警事件 | 图表绘制、成长数值修改 |
| `growth` | 成长数值、互动结算、状态推导、存档 | 表情绘制、语音播放 |
| `device_control`（扩展） | 能力注册、意图路由、风险确认、适配器调用 | 任意命令执行、绕过白名单 |
| `servo`（扩展） | 指令解析、角度/速度限制、序列播放、急停 | 语音识别、意图理解 |
| `presentation` | 状态→表情/动作映射、动作序列选择、优先级仲裁；S4 起含温湿度卡片快照 | LVGL 绘制、网络、存储 |
| `ui` | 页面、表现播放、消息卡片、图表 | 业务规则、状态映射、网络、存储 |
| `core` | 事件总线、模块生命周期、配置、契约、错误码 | 业务逻辑 |
| `hal` | 外设初始化与读写 | 业务状态机 |
| `storage` | NVS/LittleFS 封装、版本迁移、CRC | 业务字段解释 |

## 4. 事件总线

### 事件定义

```c
typedef enum {
    EVT_AI_STATE_CHANGED,      // payload: ai_state_changed_t
    EVT_AI_ASR_PARTIAL,        // payload: text_span_t
    EVT_AI_REPLY_TEXT,         // payload: text_span_t
    EVT_AI_AUDIO_LEVEL,        // payload: audio_level_t
    EVT_MSG_RECEIVED,          // payload: message_view_t
    EVT_MSG_DISPLAYED,         // payload: message_ack_t
    EVT_MSG_READ,              // payload: message_ack_t
    EVT_CLIMATE_READING,       // payload: climate_reading_t
    EVT_CLIMATE_ALERT,         // payload: climate_alert_t
    EVT_NET_STATE_CHANGED,     // payload: pet_net_event_t
    EVT_GROWTH_CHANGED,        // payload: growth_snapshot_t
    EVT_GROWTH_STATE_CHANGED,  // payload: pet_mood_t
    EVT_INTENT_READY,          // payload: intent_t
    EVT_ACTUATOR_RESULT,       // payload: actuator_result_t
    EVT_SYSTEM_FAULT,          // payload: fault_t
} pet_event_id_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t correlation_id;   // 同一次交互/消息链路共享，0 表示未关联
    uint32_t sequence;         // 发布时由事件总线赋值，单调递增
    pet_event_id_t id;
    uint16_t schema_version;   // 当前 1；不匹配拒收
    uint16_t payload_len;
    pet_event_priority_t priority;
    pet_event_source_t source;
    uint8_t payload[64];       // 固定内联，禁止跨任务裸指针
} pet_event_t;
```

> S3 已落地：payload 64 B，补齐 `correlation_id`、`sequence`、`schema_version`、`priority`、`source`；`pet_event_t` 固定 ≤96 B。可合并事件的 latest-value 槽与长文本事件池待对应模块落地时引入，避免分发后悬空。

### 规则

- 事件 payload 使用固定大小结构体或事件池，不在中断/回调中 `malloc`。
- 事件队列总深度 32，按优先级拆为 4 条（critical 4 / high 8 / normal 16 / low 4）；高优先级先出队，同优先级保序，队满按优先级计数丢弃并暴露 `dropped_total`。
- 订阅者处理时间上限 10 ms；超时任务降级为“标记 + 异步处理”，禁止在事件回调里做网络/Flash 写入。
- 事件只描述“发生了什么”，不描述“该做什么”；业务决策留在模块内。
- 高频事件（音频电平）可合并，只保留最新值；状态类事件必须保序。

### 模块生命周期

```c
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
    void (*on_event)(const pet_event_t *evt);
} pet_module_t;
```

`app` 先启动事件总线、配置存储/工作任务和物理维护控制台，再按编译开关组合成长、传感器、网络、扩展、AI 与 UI。各服务报告初始化错误，不把可选服务失败等同于整机可用。显式启用恢复预设后，连续三次早期 panic/watchdog 重启进入维护启动，在业务服务之前返回；UI 开启时只额外尝试一次恢复屏幕/BOOT，显示再次崩溃后退回 USB 维护。两次长按及松手门控见 [14](14-recovery-and-device-names.md)，健康判定与诊断见 [13](13-operational-software.md)。该保护不替代启动必需模块的错误处理。

## 5. 核心契约

所有跨模块契约放在 `firmware/components/contracts/include/pet/`，实现放在各组件内部；`main` 只做组合根。

```c
// ai_session.h
typedef enum { AI_IDLE, AI_LISTENING, AI_THINKING, AI_SPEAKING, AI_MUTED, AI_ERROR } ai_state_t;

esp_err_t ai_session_start(const ai_config_t *cfg);
esp_err_t ai_session_push_audio(const int16_t *pcm, size_t samples);
esp_err_t ai_session_text(const char *utf8);
esp_err_t ai_session_barge_in(void);
esp_err_t ai_session_stop(void);
ai_state_t ai_session_state(void);
```

```c
// message_source.h
typedef struct {
    const char *id;
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
    esp_err_t (*poll)(message_view_t *out, uint32_t timeout_ms);
} message_source_t;
```

```c
// sensor.h
typedef struct {
    float temperature_c;
    float humidity_pct;
    uint32_t timestamp_ms;
    uint16_t error_code;
    bool valid;
} climate_reading_t;

typedef struct {
    const char *id;
    esp_err_t (*init)(void);
    esp_err_t (*read)(climate_reading_t *out);
} climate_sensor_t;
```

```c
// command_adapter.h（扩展）
typedef enum { RISK_READ, RISK_REVERSIBLE, RISK_DESTRUCTIVE } risk_level_t;

typedef struct {
    char request_id[40];
    char device_id[32];
    char capability[64];
    cJSON *params;
    risk_level_t risk;
    uint32_t timeout_ms;
    char confirmation_token[65];
} command_request_t;

typedef struct {
    const char *adapter_id;
    esp_err_t (*discover)(cJSON *out);
    esp_err_t (*execute)(const command_request_t *req, command_result_t *out);
} command_adapter_t;
```

```c
// actuator.h（扩展）
typedef struct {
    esp_err_t (*init)(const servo_config_t *cfg);
    esp_err_t (*move_to)(uint8_t id, float angle, uint16_t duration_ms);
    esp_err_t (*stop)(uint8_t id);
    esp_err_t (*emergency_stop)(void);
} actuator_t;
```

## 6. 配置与存储

### 三层配置

| 层 | 位置 | 内容 | 是否提交 Git |
| --- | --- | --- | --- |
| 编译期 | `firmware/boards/<board>/board.json`（引脚/能力唯一事实源，构建生成 C 头与 Kconfig）、`config/profiles/*.json` | 板型、引脚、功能开关、默认参数 | 是（无凭据） |
| 运行期 | NVS `pet_cfg` | 用户设置、DND、音量、亮度、校准 | 否 |
| 凭据 | NVS `pet_provision/bundle_v1` / 服务端私有 env | 设备侧 Wi-Fi、MQTT/WSS/控制凭据；服务端保管供应商密钥 | 否，且不导出/不写日志 |

### NVS 命名空间

| 命名空间 | 内容 | 版本策略 |
| --- | --- | --- |
| `pet_cfg` | `settings_v1` 用户配置 | version/revision/CRC/规范 JSON，提交后回读 |
| `pet_growth` | 成长存档 | 双槽 v4 104 B + CRC + 16 B 提交记录，兼容读 v1–v3 |
| `pet_climate` | 当日极值与温湿度校准偏移 | 28 B blob：magic + version + length + CRC32，5 分钟或跨天保存 |
| `msg_inbox` | 未读消息与去重环 | 有界队列 + CRC |
| `pet_provision` | `bundle_v1` 完整凭据包 | 原子提交/回读，已有设备身份不能更换 |
| `net` / `nexus` / `xiaozhi` / `control` | 旧凭据命名空间兼容 | 只有新包不存在时回退，不从旧键拼接损坏的新包 |

S5-B 的旧 NVS 字符串键如下；当前物理配网写完整 `pet_provision/bundle_v1`，分组与字段见 [config](../config/README.md)。Kconfig 仅作开发期回退：

| 命名空间 | 键 | 内容 |
| --- | --- | --- |
| `net` | `wifi_ssid` / `wifi_pass` | Wi-Fi SSID 与密码 |
| `nexus` | `device_id` | MQTT clientId 与主题中的 `{deviceId}` |
| `nexus` | `mqtt_uri` | Broker URI，仅接受 `mqtts://`（8883） |
| `nexus` | `mqtt_user` / `mqtt_pass` | MQTT 用户名与令牌 |
| `nexus` | `mqtt_ca` | 可选 PEM CA；缺省使用 ESP-IDF 证书包 |

### 分区建议

新建独立分区表，不复用 02/03；以下为 16 MB Flash 分区表，已落盘 `firmware/partitions/16m.csv`。首次烧录前用 `esptool flash_id` 和实际 bootloader 偏移核对。

| 分区 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 0x10000 | 64 KB NVS（含 `msg_inbox` 双代快照） |
| `otadata` | 0x19000 | 0x2000 | OTA 元数据 |
| `phy_init` | 0x1B000 | 0x1000 | PHY 校准 |
| `ota_0` | 0x20000 | 0x400000 | 4 MB 应用槽 A |
| `ota_1` | 0x420000 | 0x400000 | 4 MB 应用槽 B |
| `storage` | 0x820000 | 0x200000 | 2 MB LittleFS |
| `assets` | 0xA20000 | 0x400000 | 4 MB 表情/字体/音效 |
| `model` | 0xE20000 | 0x100000 | 1 MB WakeNet/VAD 原始模型包（data/spiffs） |
| `coredump` | 0xF20000 | 0x10000 | 崩溃转储 |
| 保留 | 0xF30000 | 0xD0000 | 832 KB 后续扩展 |

约束：OTA 镜像必须校验项目 ID/版本/签名；迁移与回滚单独设计，不能直接互刷 02/03。S5-C2 将 NVS 由 24 KB 扩至 64 KB 以容纳 `msg_inbox` 双代快照，`otadata`/`phy_init` 顺延，`ota_0` 起始地址保持 0x20000。

Q4 已确认：模型使用独立 `model` 分区，不并入 `assets`。当前固定 WakeNet9“小爱同学”与 `vadnet1_medium`，构建期校验 `srmodels.bin` ≤ 960 KiB，双模型实包大小与 SHA256 见 `01-ai-agent.md` A8。ESP-SR 通过 `esp_srmodel_init("model")` 直接 mmap 加载，不挂载为文件系统、不进入 assets 索引。应用 OTA 只接受相同模型哈希；变更模型须使用配套 USB 包，当前不提供模型 OTA。自定义唤醒词约 3.9 MB，不在首版范围；启用时必须调整分区表并 USB 重刷。

## 7. 任务与内存预算

| 任务 | 优先级 | 栈 | 运行位置 | 说明 |
| --- | --- | --- | --- | --- |
| `audio_rx` | 6 | 4 KB | internal | I2S 读、AEC/VAD、环形缓冲 |
| `audio_tx` | 6 | 4 KB | internal | TTS 解码与 I2S 写 |
| `ai_session` | 5 | 6 KB | internal | WebSocket、ASR/LLM/TTS 事件 |
| `ui` | 4 | 8 KB | internal | LVGL 刷新，单次 ≤16 ms |
| `message` | 3 | 6 KB | internal | MQTT、队列、ACK |
| `sensor` | 2 | 3 KB | internal | 5 s 采样、滤波 |
| `growth` | 2 | 3 KB | internal | 结算、存档 |
| `app` | 1 | 4 KB | internal | 事件分发、看门狗 |
| `pet_events` | 5 | 3 KB | internal | 事件队列分发，订阅者处理 ≤10 ms |

内存策略：

- LVGL 绘制缓冲 2 × 240 × 320 × 2 B ≈ 300 KB，放 PSRAM；DMA 描述符与音频缓冲放 internal。
- 字体/图片/动画放 `assets` 分区，按需映射；不整包加载。
- 唤醒模型放独立 `model` 分区，ESP-SR 直接 mmap；模型不进入 `assets`，也不随应用 OTA 更新。
- 每个任务记录栈高水位；内部 heap 最低值目标 ≥80 KB，最大连续块 ≥32 KB。
- 禁止在 UI 任务里读 Flash/NVS；禁止在音频任务里做网络请求。

## 8. 部署拓扑

```text
消息/配置：ESP32 ◄── MQTT/TLS ──► Mosquitto ◄── MQTT/TLS ──► nexus-core
                                                              ▲
                                                     HTTPS 管理台/Webhook/Halo

语音对话：ESP32 ◄── WSS：Opus、转写、MCP ──► 小智服务 ──► ASR/LLM/TTS

可选控制：ESP32 ── HTTPS ──► control_gateway ── 回环 HTTP ──► mac-bridge
                              （目标 Mac）                    （同一 Mac）

本地硬件：ESP32 ── I2S/SPI/GPIO ──► 屏幕、麦克风、功放、温湿度及执行器
```

当前部署职责：

| 服务 | 归属 | 负责 | 不负责 |
| --- | --- | --- | --- |
| 消息中心 | `nexus-core`（现有） | 第三方 Webhook/Halo 接入、消息去重与保留、设备注册与 MQTT 主题 ACL、配置下发、管理台 | 音频流、ASR/LLM/TTS |
| 语音供应商 | 小智服务（设备直接连接） | ASR/LLM/TTS、供应商侧人格与工具调用 | CORE 消息队列与设备管理 |
| 可选记忆工具 | CORE MCP、标准库 stdio 适配器与供应商桥接 | 公开评论的受限检索与源状态复核，独立令牌 | 音频流、私聊存档、管理员 API |
| 可选控制网关 | `04/server/control_gateway.py` 与同机 `mac-bridge` | 设备作用域、白名单、确认、去重与审计 | 基础语音服务 |
| 自建 AI 网关 | `04/server/ai-gateway` 预留 | 后续独立供应商适配 | 当前没有可部署实现 |

- 固件通过 MQTT/TLS 收发消息与配置，使用单独的小智 WSS 凭据建立语音会话；CORE 不代理音频，也未实现 AI 短期令牌签发或 JWKS。早期令牌交换方案尚未采用。
- 语音的本地上下文、隐私门控与 MCP 工具由设备运行，供应商侧人格与真实云端验收见 [12](12-cloud-interop.md)。基础语音日常不依赖开发 Mac。
- 公开记忆是默认关闭的服务器工具链，可随 CORE 部署在常在线 Linux；令牌不下发设备，接口见 [16](16-voice-memory-tools.md)，固定桥接、受控进程与 TLS 回归见 [17](17-voice-bridge-regression.md)。
- 第三方 Webhook/Halo 只接 CORE。Mac Bridge 为可选控制能力；它在线与否不改变基础语音链路。

## 9. 技术栈总表

| 层 | 选型 | 理由 |
| --- | --- | --- |
| 固件 | ESP-IDF 6.1 + C17 | 复用 02/03 与 ESP-SR、I2S、MQTT 生态 |
| UI | LVGL 9.x | 240×320 屏、动画、中文子集、PSRAM 可行 |
| 语音前端 | ESP-SR WakeNet/VAD、可选 DSP/参考输入 | 当前保持半双工；声学 AEC/全双工未验收 |
| 音频编码 | WSS Opus：上行 16 kHz、下行 24 kHz 单声道 | 设备内部 PCM，协议不提供网络 PCM 回退 |
| 设备消息 | MQTT 3.1.1 over TLS，QoS 1 | 持久会话、离线队列、成熟运维 |
| 消息 Broker | Mosquitto 2.0.22 | 与 `nexus-core` 一致；单设备足够，集群再评估 EMQX |
| HTTP 接口 | CORE、Webhook 与可选控制网关各自鉴权 | 配网用物理串口，设备不提供通用 HTTP 管理服务 |
| 消息中心 | Java 21 / Spring Boot / PostgreSQL / Flyway | 复用 nexus-core，事务、审计与设备管理成熟 |
| AI 会话 | 设备直接连接小智 WSS | 自建 AI 网关预留，当前不经过 CORE |
| 管理台 | Vue 3 + Vite | 复用现有管理台，归 nexus-core |
| 本机桥 | Python 3 + 标准库 + SQLite | 复用 mac-bridge，依赖少 |
| 测试 | Python unittest + 主机 C 测试 + HIL | 与现有工程一致 |

被放弃的首版方案：Arduino/MicroPython（生态与性能不匹配）、全本地推理（S3 算力不足）、设备 HTTP 轮询（延迟与功耗差）、另建消息服务（已有 `nexus-core`）。

## 10. 扩展机制

- 扩展目录：`extensions/<name>/`，每个扩展必须包含 `extension.json`、`CMakeLists.txt`、唯一公开头 `include/pet_ext/<name>.h`、`src/`、`test/host/` 与 `README.md`。
- `extension.json` 由 `config/schemas/extension.schema.json` 约束：`id`、SemVer `version`、`contractVersion`、`capabilities`、最高 `risk`、`defaultEnabled=false`、`description`。核心契约版本在 `firmware/components/contracts/include/pet/extension.h`。
- 编译期开关：`CONFIG_PET_EXT_DEVICE_CONTROL`、`CONFIG_PET_EXT_SERVO`，默认关闭；组合根只调用已开启扩展的 `*_init()`。
- 校验入口：`tools/ci/check_contracts.py` 校验 manifest、契约版本、目录结构与 Kconfig 开关；`tools/ci/test_host.sh` 运行每个扩展的清单契约测试。
- 运行期工具由组合根 `app_tools.c` 组装成 `pet_intent_tool_t` 表，显式提供名称、说明、输入 schema、副作用标记和执行回调；`svc_intent` 严格解析参数并处理重复请求。
- 核心语音模块不依赖扩展头；组合根调用扩展公开服务 API，并把受保护的状态/结果返回 MCP。没有任意文本或 shell 执行入口。
- 新增扩展通过工具表和公开 API 接入，不修改 `ai_agent` 会话状态机。

## 11. 安全与隐私

- 设备身份：每设备独立令牌/证书，主题 ACL 隔离；禁止共用 CORE 管理员账号。
- 传输：MQTT TLS、HTTPS/WSS；证书校验，不复用 02/03 的明文配置。
- 消息：设备 JSON 输入上限 4096 B，严格校验信封、设备/投递身份与有效期；依赖 TLS 和 Broker ACL，不声称 MQTT 消息另有 HMAC。Webhook 的鉴权按 CORE 对应入口执行。
- 设备控制：L0 只读、L1 可逆、L2 破坏性；L2 必须二次确认并绑定请求。
- 隐私：DND/静音在软件层阻止新音频/上下文外发，物理停止延迟仍待实测；日志不写原始音频、私信、令牌、Wi-Fi 密码。设备对话上下文有界且不持久化；云端副本由供应商策略管理。CORE 消息 7/30 天保留功能默认关闭，启用、备份及预览流程见 [13](13-operational-software.md)。
- 记忆：显式保存公开、可复核的评论；检索逐条核对源公开状态和正文哈希，撤回/非公开/改文后停止检索并持久化排队删除。已保存副本依据源状态管理，本地正文仍遵守 7 天规则；账本只留来源/哈希/状态，见 [15](15-memory-lifecycle.md)。

## 12. 验证策略

| 层级 | 覆盖 | 不覆盖 |
| --- | --- | --- |
| 主机测试 | 状态机、解析器、成长公式、CRC、配置 schema、队列 | 真实 RTOS、DMA、屏幕、音频 |
| 目标测试 | HAL 回环、I2S 采样、LCD 刷新、传感器时序、MQTT 重连 | 云端真实服务、长期稳定性 |
| 契约测试 | MQTT envelope、HTTP API、webhook 签名、Bridge 白名单 | 第三方账号权限 |
| HIL | 唤醒率、端到端延迟、断网恢复、掉电存档、24 h 观测 | 量产一致性 |

每个模块文档必须列出自己的验收项；总体验收见 `07-roadmap-acceptance.md`。

## 13. 待确认项

1. 已确认（A5）：S1 板级基线为 `03` DODO-S3-LCD，背光 GPIO42 PWM；其他板型后续以独立 board profile 接入。
2. 分区表已确认 Q4（`model` 1 MB、`assets` 4 MB）；安全启动与 Flash 加密决策为开发不启用、量产启用，实际烧写尚未执行。签名/身份、配套 USB 包与回滚软件已实现，物理恢复待验收，见 [10](10-maintenance-recovery.md)。
3. 已确认（A1/A2，唤醒词于 2026-11-12 更新）：AI 供应商沿用 `03` 小智服务；唤醒词用内置 `wn9_xiaoaitongxue`（小爱同学）；TTS 音色沿用供应商默认。待确认 TTS 商用授权。
4. 消息中心复用 `nexus-core`；当前设备直连小智，自建 `server/ai-gateway` 仍预留，详见上方当前拓扑。
5. 舵机数量、供电与安装结构。
6. 已确认（B3）：中文字体用 Noto Sans SC 子集（SIL OFL），16/24 px，授权文本随资源入库。
7. 已确认（B1/B2）：Broker 用 Mosquitto 2.0.22；消息全文保留 7 天、元数据 30 天；设备未读队列默认 8 条。
8. 已确认（D 组）：冻结 `02`，`03` 作 donor；`board.json` 为源，构建生成 C 头与 Kconfig；`presentation` 独立组件；管理台复用 `nexus-core`。

以上确认后再进入编码；未确认项不得用占位实现冒充已落地。
