# 00 · 总体架构

状态：待评审。目录分层、分区预算与契约问题见 [08 结构评审](08-structure-review.md)，确认后再定稿本文。适用范围：`04-ai-desktop-pet` 全部固件、独立 AI 网关、与 `nexus-core` 的集成契约及扩展包。

## 1. 目标与非目标

### 目标

- 一个固件同时承载语音对话、消息展示、温湿度、养成玩法和可插拔扩展。
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
| `climate` | 温湿度采样、滤波、校准、异常检测、告警事件 | 图表绘制、成长数值修改 |
| `growth` | 成长数值、互动结算、状态推导、存档 | 表情绘制、语音播放 |
| `device_control`（扩展） | 能力注册、意图路由、风险确认、适配器调用 | 任意命令执行、绕过白名单 |
| `servo`（扩展） | 指令解析、角度/速度限制、序列播放、急停 | 语音识别、意图理解 |
| `presentation` | 状态→表情/动作映射、动作序列选择、优先级仲裁 | LVGL 绘制、网络、存储 |
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
    EVT_GROWTH_CHANGED,        // payload: growth_snapshot_t
    EVT_GROWTH_STATE_CHANGED,  // payload: pet_mood_t
    EVT_INTENT_READY,          // payload: intent_t
    EVT_ACTUATOR_RESULT,       // payload: actuator_result_t
    EVT_SYSTEM_FAULT,          // payload: fault_t
} pet_event_id_t;

typedef struct {
    pet_event_id_t id;
    uint32_t timestamp_ms;
    uint16_t payload_len;
    uint8_t payload[32];       // 固定内联，禁止跨任务裸指针
    // S3 补齐：priority、correlation_id、schema_version、source、sequence
} pet_event_t;
```

> S1 事件总线仅支持 ≤32 B 内联 payload；S3 扩展到 64 B + 可选引用计数块，长文本/消息通过事件池传递，避免分发后悬空。

### 规则

- 事件 payload 使用固定大小结构体或事件池，不在中断/回调中 `malloc`。
- 事件队列深度 32；S1 为 FIFO，队满丢新并累加 `dropped_total`；S3 改为关键/可合并分队列并按优先级丢弃。
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

`app` 按依赖顺序启动：`storage → hal → core → presentation → climate → growth → message_hub → ai_agent → ui`。任一必需模块失败进入安全模式：保留屏幕与基础输入，关闭网络与扩展，显示故障码。

## 5. 核心契约

所有跨模块接口放在 `firmware/main/core/contracts/`，实现放在各模块内部。

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

| 层 | 位置 | 内容 | 是否入库 |
| --- | --- | --- | --- |
| 编译期 | `firmware/boards/<board>/board.json`（引脚/能力唯一事实源，构建生成 C 头与 Kconfig）、`config/profiles/*.json` | 板型、引脚、功能开关、默认参数 | 是（无凭据） |
| 运行期 | NVS `pet_cfg` | 用户设置、DND、音量、亮度、校准 | 否 |
| 凭据 | NVS `nexus` / 服务端私有 env | Wi-Fi、设备令牌、证书、API Key | 绝不入库 |

### NVS 命名空间

| 命名空间 | 内容 | 版本策略 |
| --- | --- | --- |
| `pet_cfg` | 用户配置 | `cfg_version` + JSON schema |
| `pet_growth` | 成长存档 | 双槽 + CRC + `save_version` |
| `msg_inbox` | 未读消息与去重环 | 有界队列 + CRC |
| `net` | Wi-Fi 与 SNTP | 每次连接校验 |
| `nexus` | 设备身份与 MQTT 配置 | 只由配网写入 |

### 分区建议

新建独立分区表，不复用 02/03；以下为 16 MB Flash 分区表，已落盘 `firmware/partitions/16m.csv`。首次烧录前用 `esptool flash_id` 和实际 bootloader 偏移核对。

| 分区 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 0x6000 | 24 KB NVS |
| `otadata` | 0xF000 | 0x2000 | OTA 元数据 |
| `phy_init` | 0x11000 | 0x1000 | PHY 校准 |
| `ota_0` | 0x20000 | 0x400000 | 4 MB 应用槽 A |
| `ota_1` | 0x420000 | 0x400000 | 4 MB 应用槽 B |
| `storage` | 0x820000 | 0x200000 | 2 MB LittleFS |
| `assets` | 0xA20000 | 0x400000 | 4 MB 表情/字体/音效 |
| `model` | 0xE20000 | 0x100000 | 1 MB WakeNet 模型（SPIFFS） |
| `coredump` | 0xF20000 | 0x10000 | 崩溃转储 |
| 保留 | 0xF30000 | 0xD0000 | 832 KB 后续扩展 |

约束：OTA 镜像必须校验项目 ID/版本/签名；迁移与回滚单独设计，不能直接互刷 02/03。

Q4 已确认：模型使用独立 `model` 分区，不并入 `assets`。首版 WakeNet9 你好小智打包后约 284 KB（291,069 B），1 MB 分区保留约 3.5 倍余量；构建期校验 `srmodels.bin` ≤ 960 KB。ESP-SR 通过 `esp_srmodel_init("model")` 直接 mmap 加载，不进入 assets 索引。模型不随应用 OTA 更新：首次烧录写入，后续变更走独立数据写入或 USB 恢复。自定义唤醒词约 3.9 MB，不在首版范围；启用时必须调整分区表并 USB 重刷。

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
消息面
┌──────────────┐  MQTT/TLS 8883   ┌────────────────┐   HTTPS    ┌───────────────────┐
│ 04 固件      │◄────────────────►│ MQTT Broker    │◄──────────►│ nexus-core        │
│              │                  │ Mosquitto      │            │ 消息/配置/管理台  │
└──────┬───────┘                  └────────────────┘            └────┬──────┬───────┘
       │                                                             │      │
       │ I2S/SPI/GPIO                                                │      ▼
       ▼                                                             │  第三方 Webhook / Halo
 ST7789 / INMP441 / MAX98357A / DHT11 / 扩展执行器                   │
                                                                     ▼
                                                            ┌───────────────────┐
                                                            │ mac-bridge (本机) │
                                                            │ 白名单 + 确认     │
                                                            └───────────────────┘

AI 会话面
┌──────────────┐   WSS 音频 + HTTPS 控制   ┌──────────────────────┐   HTTPS    ┌────────────────────┐
│ 04 固件      │◄─────────────────────────►│ ai-gateway（独立）   │◄──────────►│ ASR/LLM/TTS 供应商 │
│              │   短期会话令牌            │ 会话编排/供应商适配  │            └────────────────────┘
└──────┬───────┘                           └──────────────────────┘
       │ HTTPS 设备认证 → 换取短期令牌
       ▼
┌───────────────────┐
│ nexus-core        │  设备身份校验 + 令牌签发（JWKS/内省供网关校验）
└───────────────────┘
```

服务端按能力拆成两个部署单元（Q3 方案 C）：

| 服务 | 归属 | 负责 | 不负责 |
| --- | --- | --- | --- |
| 消息中心 | `nexus-core`（现有） | 第三方 Webhook/Halo 接入、消息去重与保留、设备注册与 MQTT 主题 ACL、配置下发、管理台 | 音频流、ASR/LLM/TTS |
| AI 网关 | `04/server/ai-gateway`（独立） | ASR/LLM/TTS 会话编排、供应商适配与密钥、上下文策略、打断 | 消息队列、设备管理、管理台 |

- 固件通过 MQTT/TLS 收发消息与配置，通过 WSS/HTTPS 建立 AI 会话；两条链路互不代理。
- 设备身份由 `nexus-core` 校验并签发短期 AI 会话令牌；`ai-gateway` 只校验令牌，不共享数据库、不直连设备管理数据。
- 已确认（A3）：AI 网关首版复用 `03` 的小智 WebSocket 服务作为实现；固件只依赖 `ai_session` 契约，替换供应商或自建实现不改 UI/成长/消息模块。
- 第三方 Webhook 只进 `nexus-core`，不直连 AI 网关；与 `nexus-core` 的契约和部署配置放在 `integration/`。

## 9. 技术栈总表

| 层 | 选型 | 理由 |
| --- | --- | --- |
| 固件 | ESP-IDF 6.1 + C17 | 复用 02/03 与 ESP-SR、I2S、MQTT 生态 |
| UI | LVGL 9.x | 240×320 屏、动画、中文子集、PSRAM 可行 |
| 语音前端 | ESP-SR WakeNet + AEC + VAD | 片上唤醒与回声消除，降低云端依赖 |
| 音频编码 | Opus 16 kHz 单声道 / PCM 回退 | 带宽与音质平衡；TTS 输出 24 kHz PCM |
| 设备消息 | MQTT 3.1.1 over TLS，QoS 1 | 持久会话、离线队列、成熟运维 |
| 消息 Broker | Mosquitto 2.0.22 | 与 `nexus-core` 一致；单设备足够，集群再评估 EMQX |
| 设备 HTTP | HTTPS + Bearer/HMAC | 配网、测试、第三方 webhook |
| 消息中心 | Java 21 / Spring Boot / PostgreSQL / Flyway | 复用 nexus-core，事务、审计与设备管理成熟 |
| AI 网关 | 独立服务；首版复用 03 小智 WebSocket 实现 | 音频会话独立扩缩容；供应商密钥集中管理 |
| 管理台 | Vue 3 + Vite | 复用现有管理台，归 nexus-core |
| 本机桥 | Python 3 + 标准库 + SQLite | 复用 mac-bridge，依赖少 |
| 测试 | Python unittest + 主机 C 测试 + HIL | 与现有工程一致 |

被放弃的首版方案：Arduino/MicroPython（生态与性能不匹配）、全本地推理（S3 算力不足）、HTTP 轮询（延迟与功耗差）、消息中心独立拆分（归 `nexus-core`；仅 AI 网关按 Q3=C 独立）。

## 10. 扩展机制

- 扩展目录：`extensions/<name>/`，包含 `include/`、`src/`、`README.md`、契约测试。
- 编译期开关：`CONFIG_EXT_DEVICE_CONTROL`、`CONFIG_EXT_SERVO`。
- 运行期能力注册：`capability_registry` 保存能力名、参数 schema、风险级别、适配器 ID。
- 核心模块不 `#include` 扩展头；扩展通过 `EVT_INTENT_READY` 订阅意图，通过 `EVT_ACTUATOR_RESULT` 回报结果。
- 新增扩展不得修改 `ai_agent` 的状态机；只消费意图、产出结果。

## 11. 安全与隐私

- 设备身份：每设备独立令牌/证书，主题 ACL 隔离；禁止共用 CORE 管理员账号。
- 传输：MQTT TLS、HTTPS/WSS；证书校验，不复用 02/03 的明文配置。
- 消息：payload 上限 8 KB；签名/HMAC；时间戳容差 5 分钟；重放保护。
- 设备控制：L0 只读、L1 可逆、L2 破坏性；L2 必须二次确认并绑定请求。
- 隐私：麦克风在 DND/静音时硬件或软件门控；日志不写原始音频、私信、令牌、Wi-Fi 密码；会话转写默认只存服务端短期；消息全文保留 7 天、元数据保留 30 天。
- 记忆：只把公开、可复核的内容写入 OpenViking；不写凭据与 NVS 二进制。

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
2. 分区表已确认 Q4（`model` 1 MB、`assets` 4 MB）；安全启动与 Flash 加密已确认（A4）：开发不启用、量产启用；待确认 OTA 身份校验细节与 USB 恢复流程。
3. 已确认（A1/A2）：AI 供应商沿用 `03` 小智服务；唤醒词用内置 `wn9_nihaoxiaozhi_tts`（你好小智），可替换为其他内置词；TTS 音色沿用供应商默认。待确认 TTS 商用授权。
4. 已确认（Q3=C/A3）：消息中心复用 `nexus-core`，AI 网关独立在 `server/ai-gateway`；首版复用 `03` 小智 WebSocket 服务，对外以 `ai_session` 契约隔离。
5. 舵机数量、供电与安装结构。
6. 已确认（B3）：中文字体用 Noto Sans SC 子集（SIL OFL），16/24 px，授权文本随资源入库。
7. 已确认（B1/B2）：Broker 用 Mosquitto 2.0.22；消息全文保留 7 天、元数据 30 天；设备未读队列默认 8 条。
8. 已确认（D 组）：冻结 `02`，`03` 作 donor；`board.json` 为源，构建生成 C 头与 Kconfig；`presentation` 独立组件；管理台复用 `nexus-core`。

以上确认后再进入编码；未确认项不得用占位实现冒充已落地。
