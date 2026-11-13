# 01 · AI 智能体对话模块

状态：A1–A8 音频、会话、任务、小智协议/Opus、WSS、输入/UI 与 WakeNet/模型 VAD 接线软件已实现（2026-11-12）；AI 与模型默认关闭，真实推理、云端与实机验收待推进。依赖：`00-architecture.md`。首版路线：**混合云**（本地唤醒/录音，云端 ASR/LLM/TTS）。第 1–12 节为目标设计，已实现范围与验证边界见第 13–20 节。

后续 SW-07-A 的可选并行 HAL 与一次维护诊断见 [22](22-audio-duplex-diagnostic.md)。普通 AI 拥有者继续半双工，AEC 参考对齐与声学打断未完成；下文 A1–A8 保留各自交付时的证据。

## 1. 范围

做：唤醒、录音、语音识别、对话生成、语音合成、播放、打断、上下文管理、状态事件产出（表情/动作由 `presentation` 映射）、工具意图产出。

不做：消息投递、成长数值结算、具体设备执行、全本地大模型推理。

## 2. 技术选型

### 2.1 AI 接入路线对比

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| 混合云：本地唤醒 + 云端 ASR/LLM/TTS | 设备算力需求低；语音质量好；可复用 03 小智底座；供应商可替换 | 依赖网络；隐私需边界设计；端到端延迟受云影响 | **推荐首版** |
| 全云端实时语音 | 听说一体、打断自然、延迟低 | 供应商锁定；成本高；断网不可用；调试黑盒 | 二期评估 |
| 全本地推理 | 隐私最好；离线可用 | S3 算力/内存不足；模型与音质受限；开发周期长 | 不采用 |

### 2.2 子项选型

| 子项 | 推荐 | 备选 | 理由 |
| --- | --- | --- | --- |
| 唤醒词 | ESP-SR WakeNet（内置“小爱同学”） | 其他内置词 | A8 接入 `wn9_xiaoaitongxue`，默认关闭，识别率待实机 |
| 回声消除 | ESP-SR AEC | 半双工（播放时关麦） | 支持打断；AEC 失败时自动降级 |
| VAD | ESP-SR VAD | 能量 + 过零率 | 抗噪更好；端点判定稳定 |
| ASR | 流式云端 ASR（部分结果） | 整段上传识别 | 首字延迟低，可显示“正在听” |
| LLM | OpenAI 兼容 Chat Completions 流式接口 | 厂商专有 SDK | 易替换供应商；服务端统一适配 |
| TTS | 流式 TTS，24 kHz 单声道 PCM | MP3/Opus 流 | 解码简单；首包快；与 MAX98357A 匹配 |
| 音频上行 | Opus 16 kHz 单声道 20 ms 帧 | PCM 16 kHz | 节省带宽；弱网更稳；PCM 可回退 |
| 会话传输 | WSS 长连接 | MQTT 传音频 | 音频双向流适合 WebSocket；消息走 MQTT |
| UI | LVGL 9.x | 自绘 framebuffer | 动画、字体、页面管理成熟 |

### 2.3 供应商抽象

固件只依赖 `ai_session` 契约，不直接依赖某家 SDK。服务端/网关负责供应商适配：

```text
固件 ai_agent ──WSS──► AI 会话服务 ──► ASR / LLM / TTS 供应商
```

首版由独立 `server/ai-gateway` 承载，可复用现有小智 WebSocket 服务作为实现；固件只依赖 `ai_session` 契约，替换供应商或自建实现不改 UI/成长/消息模块。

## 3. 音频硬件与参数

| 项 | 参数 |
| --- | --- |
| 麦克风 | INMP441，I2S0，SCK16 / WS17 / SD18，L/R 接 GND 选左声道 |
| 麦克风采样 | 16 kHz，单声道，24-bit 数据装在 32-bit 槽，软件右移对齐 |
| 麦克风 DMA | 4 × 320 样本（20 ms/帧），internal RAM |
| 麦克风环形缓冲 | 1 s（32 KB），internal RAM |
| 功放 | MAX98357A，I2S1，BCLK21 / LRC38 / DIN39，VIN 5V，共地 |
| 播放采样 | 24 kHz，单声道，16-bit PCM |
| 播放 DMA | 3 × 480 样本（20 ms/帧），双缓冲/三缓冲 |
| 扬声器 | 8 Ω 1–3 W；具体阻抗/功率待实物确认 |
| 音量 | 软件数字增益 + 固定硬件增益；禁止削顶 |
| 静音 | DND/隐私模式关闭 I2S TX 或拉低 SD；状态在屏幕可见 |

注意：INMP441 与 MAX98357A 必须共地；功放电源不要从 3.3V 取；舵机等大电流设备另供 5V，避免音频掉压。

## 4. 语音输入实现

### 4.1 数据流

```text
I2S RX → DMA → 环形缓冲 → AEC → VAD → WakeNet → Opus 编码 → WSS 上行
                              │
                              └→ 音频电平事件（表情用）
```

### 4.2 状态机

```text
IDLE ──唤醒词──► LISTENING ──VAD 结束──► THINKING ──首个 TTS 包──► SPEAKING
  ▲                  │                       │                      │
  │                  └──超时/无语音──► IDLE   └──失败──► ERROR       └──播放完──► IDLE
  └────────────────────────── 打断/停止 ──────────────────────────────┘
```

| 状态 | 进入条件 | 退出条件 | 麦克风 | 屏幕 |
| --- | --- | --- | --- | --- |
| `IDLE` | 启动完成/会话结束 | 唤醒词/按键 | 开，低功耗监听 | 呼吸/眨眼 |
| `LISTENING` | 唤醒词/长按说话 | VAD 静音 300 ms 或 15 s 上限 | 开，AEC 参考播放流 | 耳朵抬起、电平条 |
| `THINKING` | 本地输入结束，等待 ASR/LLM/TTS | 首个 TTS 包/超时 15 s | 可选开（等待打断） | 思考气泡 |
| `SPEAKING` | 首个 TTS 包 | 播放队列空/被打断 | 开，AEC；AEC 不可用则关麦 | 口型同步、身体摆动 |
| `MUTED` | 用户静音/DND | 用户解除 | 关 | 静音图标/睡觉 |
| `ERROR` | 网络/ASR/LLM/TTS 失败 | 重试成功/回 IDLE | 按策略 | 汗滴/故障提示 |

### 4.3 唤醒与端点

- 唤醒词检测在 `IDLE` 常开；检测到后播放短提示音（可配置），进入 `LISTENING`。
- 端点判定：VAD 连续 300 ms 静音；最短语音 300 ms；最长 15 s；超时强制结束并提示。
- 误唤醒抑制：连续两次唤醒间隔 <1 s 忽略；同一唤醒词 3 s 内不重复触发。
- 按键回退：按住说话语义进入 `LISTENING`，松开结束。A7 在现有 BOOT 菜单提供独立说话项，按住至“正在听”后说话，松开结束；保留消息确认与成长操作。
- 打断：当前半双工状态机仅接受手动说话输入打断 `THINKING`/`SPEAKING`，请求取消旧轮次、停止播放并清空缓冲；唤醒检测不打断活动会话。AEC 验证后再接入语音打断。

### 4.4 音频前处理

- AEC：以当前播放 PCM 为参考；参考缓冲与播放缓冲同源，避免漂移。
- 降噪：ESP-SR NS，强度可配置；不要过度降噪导致语音失真。
- 增益：自动增益限制在 ±12 dB；削顶检测后降增益。
- 电平：每 20 ms 计算 RMS，映射到 0–100；事件合并，UI 只取最新值。

## 5. 语音输出实现

### 5.1 数据流

```text
WSS TTS 流 → 解包（PCM/Opus/MP3）→ 24 kHz PCM → 环形缓冲 → I2S TX → MAX98357A → 扬声器
```

- 首包预缓冲 100 ms 再开播，避免一上来就欠载。
- 欠载：填静音 20 ms 并计数；连续 5 次降级提示“网络不稳”。
- 播放完成以 I2S DMA 完成回调为准，不以“最后一包写入”判断。
- 停止/打断：停止解码、清空队列、短淡出 10 ms，避免爆音。
- 音量：0–100 映射到数字增益；保存到 `pet_cfg`；DND 时强制 0 或仅保留 critical 提示。

### 5.2 延迟预算

| 环节 | 目标 |
| --- | --- |
| 唤醒响应 | ≤200 ms |
| VAD 端点 | 300 ms |
| ASR final | ≤300 ms（流式） |
| LLM 首 token | ≤500 ms |
| TTS 首包 | ≤300 ms |
| 播放预缓冲 | 100 ms |
| 合计首响 | ≤1.5 s（良好网络） |

## 6. 对话上下文管理

### 6.1 数据结构

```c
typedef struct {
    char role[12];        // system/user/assistant/tool
    char *content;        // UTF-8
    uint32_t timestamp_ms;
    uint16_t token_estimate;
} chat_turn_t;

typedef struct {
    char session_id[40];
    chat_turn_t turns[12];
    uint8_t turn_count;
    char summary[1024];   // 旧轮次摘要
    uint32_t total_tokens;
} ai_context_t;
```

### 6.2 策略

- 保留最近 12 轮原文；超过 8 轮时把最早 4 轮压缩为 ≤200 字摘要，保留摘要 + 最近 8 轮。
- 总预算 4k tokens：system prompt ≤500，摘要 ≤300，历史 ≤2000，工具结果 ≤800，预留 400。
- system prompt 包含：宠物人设、当前状态（心情/饥饿/温湿度摘要）、可用能力、安全规则、禁止事项。
- 工具调用结果只保留结构化摘要，不回灌完整 JSON。
- 会话结束/静音/隐私清除时，本地上下文清空；服务端按保留策略短期保存。
- 本地只持久化摘要与用户偏好（≤2 KB），不持久化完整转写。

### 6.3 人设模板（示例）

```text
你是桌面宠物豆豆。说话简短、温和、有一点好奇，不说教。
你可以使用以下能力：查询温湿度、读取未读消息、记录成长互动、请求设备控制。
不知道就说不知道；不编造已经执行的动作；涉及打开应用、发消息、移动舵机时必须先确认。
当前状态：心情={mood}，饥饿={hunger}，温度={temp}，湿度={humidity}。
```

## 7. 表情设计规范（监听/说话状态）

本节是表现契约：映射与仲裁由独立 `presentation` 组件实现，`ai_agent` 只产出状态与音频电平事件。

### 7.1 状态与表情映射

| 状态 | 眼睛 | 嘴/脸 | 身体 | 动效 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| `IDLE` | 定期眨眼，视线轻微漂移 | 微笑 | 呼吸起伏 2 s | 低 | 5 |
| `LISTENING` | 睁大，瞳孔朝向声源 | 微张 | 耳朵/头顶抬起 | 电平条 0–100，平滑 100 ms | 3 |
| `THINKING` | 向上看，偶尔眨眼 | 抿嘴 | 轻微左右晃 | 三点省略号循环 | 4 |
| `SPEAKING` | 看向用户，眨眼减少 | 口型 6 帧循环 | 随音量轻摆 | 口型与 RMS 包络同步 | 2 |
| `ERROR` | 半闭/皱眉 | 波浪嘴 | 缩一下 | 汗滴 1 次 | 1 |
| `MUTED` | 闭眼 | 平嘴 | 静止 | 静音角标 | 0（最高） |

优先级数字越小越高：`MUTED > ERROR > SPEAKING > LISTENING > THINKING > IDLE`。成长情绪（开心/难过/生病）作为 `IDLE` 的子状态，不覆盖会话状态。

### 7.2 动画资源规范

- 画布 240×320，RGB565；透明用 alpha 混合或预乘。
- 帧率目标 30 fps，最低 20 fps；单帧解码 ≤8 ms。
- 精灵表按状态拆分：`idle_*.png`、`listen_*.png`、`think_*.png`、`speak_*.png`、`error_*.png`。
- 循环动画首尾帧一致；状态切换 100 ms 交叉淡入，避免跳变。
- 资源放 `resources/animations/`，通过 `assets` 分区打包；不动态 `malloc` 每帧。
- 中文字体用 16/24 px 子集，数字单独字库；缺字回退到内置点阵。

### 7.3 电平映射

```text
rms_db = 20 * log10(rms / 32768)
level = clamp((rms_db + 60) / 60, 0, 1)   // -60 dB → 0，0 dB → 1
smoothed = 0.7 * prev + 0.3 * level        // 100 ms 更新
```

- 监听：耳朵高度/电平条随 `smoothed` 变化；静音时回落到 0。
- 说话：口型帧 = `floor(smoothed * 6)`，限制在 0–5；低于阈值保持闭嘴，避免抖动。

## 8. 动作设计规范（播放/说话状态）

首版动作全部在屏幕内完成，不依赖舵机：

| 动作 | 触发 | 表现 | 时长 |
| --- | --- | --- | --- |
| `nod` | 肯定回复 | 头部/整体下压再回弹 | 400 ms |
| `shake` | 否定/拒绝 | 左右快速摆动 | 500 ms |
| `bounce` | 开心/打招呼 | 整体上跳 8 px 两次 | 600 ms |
| `lean` | 倾听 | 向声源方向倾斜 5° | 300 ms |
| `sleep` | 夜间/无交互 10 min | 闭眼、呼吸变慢、Zzz | 循环 |
| `wake` | 唤醒 | 睁眼、伸懒腰 | 500 ms |
| `alert` | critical 消息 | 红色边框、快速抖动 | 800 ms |

动作与表情可叠加，但同一通道（身体位移/表情）同时只允许一个主动作。动作结束发 `EVT_AI_STATE_CHANGED` 或专用完成事件，避免 UI 卡死。

预留 `motion_sink` 接口，后续把 `bounce/nod/shake` 映射到舵机，不改 AI 模块：

```c
typedef struct {
    esp_err_t (*play)(const char *action, uint16_t duration_ms);
    esp_err_t (*stop)(void);
} motion_sink_t;
```

## 9. 上下文与工具意图

AI 不直接执行设备动作，只产出意图：

```json
{
  "intent": "device.control",
  "confidence": 0.92,
  "params": { "deviceId": "mac", "capability": "app.open", "args": { "app": "idea" } },
  "sourceTurnId": "turn-18",
  "requiresConfirmation": true
}
```

意图路由：

- `climate.query` → `climate` 模块只读查询。
- `message.list/read` → `message_hub`。
- `growth.interact` → `growth`。
- `device.control` → `device_control` 扩展。
- `servo.move` → `servo` 扩展。

未知意图、低置信度（<0.6）、缺参数：回复澄清，不执行。

## 10. 实现步骤

| 步骤 | 内容 | 产出 |
| --- | --- | --- |
| 1 | 音频 HAL：I2S RX/TX、环形缓冲、电平计算 | 麦克风录音回放、电平日志 |
| 2 | 唤醒 + VAD + 状态机 | 状态迁移主机测试、按键回退 |
| 3 | WSS 会话 + 流式 ASR | 屏幕显示部分/最终文本 |
| 4 | LLM 流式 + TTS 播放 | 首响 ≤1.5 s、可停止 |
| 5 | 打断 + AEC + 半双工降级 | 播放中可打断，不自我触发 |
| 6 | 上下文管理 + 摘要 + 人设 | 10 轮上下文保持 |
| 7 | presentation 映射 + 资源管线 | 监听/思考/说话表现 |
| 8 | 工具意图 + 路由 | 意图 JSON、未知意图澄清 |
| 9 | 异常与弱网 | 断网/超时/欠载/静音恢复 |
| 10 | HIL 验收 | 唤醒率、延迟、24 h 稳定性 |

## 11. 验收标准

### 功能

- 唤醒词在 1 m、正常室内噪声下检出率 ≥95%，误唤醒 ≤1 次/小时（固定测试音频与真人各一轮）。
- 唤醒到开始录音 ≤200 ms；VAD 端点 300 ms ±50 ms；首响 ≤1.5 s（局域网/良好云网络）。
- 播放中可被唤醒词或按键打断，停止到静音 ≤300 ms。
- 连续 10 轮对话保持上下文；超过 8 轮触发摘要；token 不溢出。
- 监听/思考/说话/错误/静音五种状态的表情与动作符合第 7、8 节；无卡死状态。
- 断网后 10 s 内进入错误态并可重试；恢复后无需重启。
- DND/静音时麦克风关闭或门控，屏幕有可见标识。

### 资源与稳定性

- 内部 heap 最低值 ≥80 KB；最大连续块 ≥32 KB；PSRAM 最低值 ≥1 MB。
- 音频任务栈高水位 ≥1 KB；UI 单帧 ≤16 ms；无 WDT。
- 24 h 循环对话/待机无重启、无明显内存下降。
- 日志不含音频原文、令牌、Wi-Fi 密码、完整转写。

### 测试

- 主机测试：状态机、VAD 端点、上下文裁剪、意图校验、欠载恢复。
- 目标测试：I2S 回环、AEC 参考、屏幕动画、WSS 重连。
- HIL：唤醒率、端到端延迟、打断、断网、掉电、24 h soak。

## 12. 风险与待确认

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| AEC 在 S3 上效果不足 | 打断自我触发/回声 | 半双工降级；限制音量；优化参考缓冲 |
| 云 ASR/LLM/TTS 延迟波动 | 首响超时 | 流式、预缓冲、供应商切换、超时降级 |
| 唤醒词误触发 | 打扰/隐私 | 阈值调优、二次确认、按键模式 |
| 中文 TTS 音色授权 | 无法商用 | 提前确认授权与音色 |
| PSRAM 带宽/动画帧率 | 卡顿 | 精灵表、局部刷新、30→20 fps 降级 |
| 上下文 token 估算偏差 | 超限/截断 | 服务端 tokenizer 校准；预留 400 tokens |

已确认：AI 供应商沿用 `03` 小智服务；唤醒词为内置 `wn9_xiaoaitongxue`（小爱同学），可替换为其他内置词；TTS 音色沿用供应商默认。
待确认：TTS 商用授权、是否允许云端保存转写、AEC 实测效果、扬声器阻抗/功率。


## 13. A1 音频基础软件切片（2026-11-11）

本切片实现 HAL、PCM、有限缓冲和录完再播放的诊断；不代表完整 AI 语音链路。`02`/`03` 未修改，未烧录设备。

### 接口与所有权

- `contracts/pet/audio.h` 固定业务 PCM：signed 16-bit 单声道，RX 16 kHz、TTS TX 24 kHz、20 ms 帧。`pet_hal` 提供 PCM 转换、衰减/电平、调用方持有的 FIFO 与 `pet_audio_device_t`；`bsp_dodo_s3_lcd` 实现硬件；`svc_audio` 编排诊断。
- HAL 由一个任务串行独占。初始化不开麦；录音、播放互斥。读写返回实际样本数，返回前消费调用方缓冲；错误后先 `deinit` 再重试。`write` 成功仅表示提交 DMA，完成需单独 `drain`。
- 当前诊断最多录制 1 秒，按 20 ms 整帧处理；FIFO 满时整帧拒绝，不覆盖未消费声音。录音未完整成功时不回放；取消在帧与尾部等待边界检查，读写单次超时 100 ms，尾部等待最多 250 ms。失败立即清理，不在错误片段上无限重试。
- 报告区分录入量、提交量、DMA 尾部完成与清理错误；清理失败不能宣称已静音。结束、取消、失败均清除调用方录音区与临时帧，不写 NVS、不联网、不记录原始音频。

### 板级实现

引脚来自 `firmware/boards/dodo-s3-lcd/board.json`：麦克风 I2S0 BCLK16/WS17/DIN18，功放 I2S1 BCLK21/WS38/DOUT39。生成器一并检查 LCD、BOOT、DHT11 与音频引脚冲突；DHT11 的运行编译配置覆盖值另在音频初始化时拒绝冲突。

- INMP441 使用 Philips 32-bit 左槽，取高 16 位；不复制 donor 曾用于排查的额外放大。RX DMA 为 4×320 个 32-bit 样本。
- TX 将单声道复制到左右 16-bit 槽，兼容 MAX98357A 的声道选择；DMA 为 3×480 帧（24 kHz 下每个 20 ms，16 kHz 诊断下 30 ms）。每次启动先预填整个 DMA 环为零，完成的描述符自动清零，避免欠载或停止/重启后重播旧语音。
- `drain` 在最后提交之后等待一个完整 DMA 环再多一帧的 `on_sent` 回调，给 FIFO 尾部留出传输时间；回调没有推进则超时，不用固定休眠冒充播放完成。此为保守尾部等待，非精确逐包延迟计量。
- 诊断回放仍为原始 16 kHz，防止直接用 24 kHz 导致变速；后续 TTS 支持 24 kHz。音量只支持 0–100% 衰减，电平计算覆盖 `INT16_MIN` 与满幅 RMS，不引入数字增益削顶。

### 开关与诊断入口

`CONFIG_PET_MODULE_AUDIO_ENABLED=y` 默认只编入能力，麦克风/功放保持关闭。`CONFIG_PET_AUDIO_FAKE=y` 使用确定性 400 Hz 方波，逻辑播放完成不能作为硬件证据。

`CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT` 默认关闭，且 DND 开启时不可选。显式启用后，由独立任务执行一次 1 秒录音、25% 音量回放，随后释放资源；不复用 BOOT 长按，不改变成长菜单或消息操作。需要约 32 KB internal PCM 区、约 10.9 KB DMA 区、约 3.2 KB 静态工作区和 6 KB 任务栈，实际 heap/栈水位仍需实机测量。

公共服务边界：`pet_audio_loopback_run(device, config, report)`；支持 20..1000 ms、原位音量衰减与可选取消回调。今后的 AI 流式输入/输出应使用同一 HAL，不复用“一秒后回放”的诊断流程。

### 验证与剩余工作

`tools/ci/test_host.sh` 新增 PCM 对齐/音量/RMS、FIFO 顺序/回绕/溢出、真实诊断服务与 I2S BSP 测试。硬件/IDF 调用由边界替身控制，覆盖逐阶段失败、部分读写、取消、重试、清理失败、无 DMA 进度超时、16/24 kHz 尾部等待、重新开播静音及自定义 DHT11 引脚冲突；另用真实 fake 实现跑最短录放音。

上述 5 组 C 程序通过 ASan/UBSan；macOS 使用 `ASAN_OPTIONS=detect_leaks=0`，未做 LeakSanitizer 验证。构建与资源核对如下（均未烧录）：

| 配置 | 验证范围 | 镜像 / 静态 DIRAM |
| --- | --- | --- |
| 默认 `firmware/build` | 完整构建、链接与 4 MB 分区检查通过；未链接诊断/录放音/I²S 写入入口 | 2,840,592 B / 145,034 B |
| 真实诊断 `build-a1-real` | 开机诊断开启；完整构建、链接与分区检查通过；四个关键音频符号存在 | 2,868,048 B / 148,514 B |
| fake `build-a1-fake` | 真实 CMake 配置与 7 个组合根/HAL/服务编译单元的交叉编译通过 | 未做整镜像链接 |
| 关闭音频 `build-a1-off` | 真实 CMake 配置与 4 个组合根/HAL 编译单元的交叉编译通过 | 未做整镜像链接 |

已核对完整镜像的 ELF 晚于最新 A1 目标文件，两种镜像应用分区均余 32%。静态 DIRAM 不包含启动后申请的 PCM、DMA 与任务栈，不能当作运行时 heap 水位。构建中仍有 ESP-IDF 自带组件的 CMake 私有包含检查/旧 Kconfig 写法提示，本轮未修改 SDK。

后续范围：A2 端点/会话状态机见下一节；WakeNet/模型 VAD、AEC、Opus、WSS/ASR/LLM/TTS、AI 状态与屏幕联动、音量持久化、流式预缓冲/欠载统计和淡入淡出仍待实现。实物接线、I2S 波形、录音音质、真实静音/爆音、DMA 时序、并发负载、内存/栈水位和功耗均未验收，P1 仍未达到实机退出条件。

## 14. A2 端点与会话状态机（2026-11-11）

本切片提供纯 C 会话引擎与离线 CLI，不启动麦克风、网络或 UI 任务。`CONFIG_PET_MODULE_AI_ENABLED=y` 默认编译 `svc_ai_agent`；关闭时不编译引擎实现。BOOT 成长菜单及 02/03 均未修改，未烧录。

### 公开接口与执行边界

- `contracts/pet/ai.h` 定义六种状态、转换原因和只读快照；`svc_ai_agent/session.h` 提供初始化、快照、控制输入和 20 ms PCM 输入。调用方持有实例，由单一任务串行拥有；没有全局状态、动态分配、存储或云供应商依赖。
- 控制输入与 PCM 使用同一 64-bit 单调毫秒时钟；每轮 `turn_id` 递增且不回绕。松开、响应、播放完成、错误与 PCM 必须携带原轮次。旧轮次、乱序时间和重复帧不改变状态或推进时钟；重新初始化前必须停止旧音频并丢弃旧异步事件。
- 返回 `actions` 是控制意图：拥有者先执行旧轮次的 cancel/stop/clear，再执行新轮次的 begin/start；同一帧先转发 PCM 再结束输入。HAL/网络失败需回送携带轮次的错误，播放完成需由真实 DMA drain 确认。状态机本身不能证明硬件已静音、音频已上传或声音已播完。

### 当前规则

| 场景 | 可观察结果 |
| --- | --- |
| 唤醒结果注入 | 空闲/错误时开启新轮次；3 s 内重复唤醒忽略；静音及活动会话不接受唤醒 |
| 能量回退/模型标签 | 16 kHz mono signed 16-bit，320 样本/帧；RMS 开/关阈值默认 800/400，带滞回；也可注入明确 speech/silence 标签。能量阈值不是人声识别，实机需校准 |
| 自动端点 | 至少 300 ms 有效语音后连续 300 ms 静音进入 thinking；不足 300 ms 的片段经 300 ms 静音清零，避免短噪声累加；15 s 上限结束，有效语音不足则取消 |
| 按住说话 | press 开始、release 结束；不自动按静音收口，仍受 15 s 上限约束。活动监听中的重复 press 不重开；thinking/speaking 中 press 取消旧轮次并开始新轮次 |
| 静音/取消/错误 | 产生停止、取消与清空意图；解除静音只回到 idle，不自动开启采集。错误可经新 press/wake 重试 |
| 断流/超时 | PCM 间隔超过 100 ms 进入 input_gap 错误；输入结束后 15 s 无响应进入 response_timeout，边界时刻到达的响应也不启动播放。拥有者需周期性发送 tick |

上行音频尚未执行，`FORWARD_PCM` 只要求后续拥有者消费本帧，不保存指针。响应事件、显式模型标签及播放完成均可注入；当前没有 WakeNet、ESP-SR VAD、AEC、ASR/LLM/TTS 或真实流式播放。播放中的传输停滞/欠载检测、隐私/DND 配置路由和屏幕联动属于后续运行任务。

### 桌面演练

```bash
./tools/simulate_voice.sh <<'EOF'
wake
voice 300
silence 300
tts
press
tts 1
voice 300
release
mute
unmute
quit
EOF
```

`voice`/`silence` 生成合成 PCM；`wake`/`tts`/`done` 注入检测、响应和播放完成。`tts 1` 可演练旧轮次拒收；`wait` 仅推进时钟，不生成静音，监听中 `wait 101` 可演练断流，thinking 中 `wait 15000` 可演练响应超时。`status` 查看状态，`help` 查看参数；非法命令返回退出码 2。所有演练均无实际录音、网络、云端响应或设备操作。

### 验证记录

- `tools/ci/test_host.sh` 已包含 `test_ai_session` 和 `test_voice_simulator.py`。主机全套测试、端点/控制/旧轮次/重复帧/时间边界与 CLI 回归通过；测试使用真实引擎和 PCM 电平逻辑，外部检测与响应为显式输入。
- `test_ai_session` 通过 ASan/UBSan；macOS 使用 `ASAN_OPTIONS=detect_leaks=0`，不包含 LeakSanitizer 验证。
- 组件依赖图、板级生成检查与现有契约校验通过；默认固件完整构建、链接与 4 MB 分区检查通过，镜像 2,840,592 B，静态 DIRAM 145,034 B，应用分区余 32%。已核对最新状态机目标文件、静态库及四个 API 符号；当前应用尚未调用引擎，链接器会丢弃未使用实现，镜像与 A1 默认配置大小相同。这不是设备运行会话的证据。
- 关闭 AI 的 `firmware/build-a2-off` 使用独立 SDKCONFIG：真实 CMake 配置确认不编译引擎，组合根针对性交叉编译通过；未执行该变体完整链接。配置时仍有 ESP-IDF 内部 Wi-Fi 私有包含目录提示，未修改 SDK。

拥有者任务与 HAL 采集/播放执行见第 16 节，小智协议/编解码与 WSS 接线见第 17–18 节；真实 WakeNet/VAD 与设备云端联调仍待接入。当前不构成完整 AI 对话或 P1/P2 实机验收证据。

## 15. A3 运行拥有者与前端适配边界（2026-11-12）

本切片固定运行任务与 AI 状态机之间的所有权边界，不创建 FreeRTOS 任务、不打开 I²S、不连接云端。`svc_ai_agent/runtime.h` 的 `pet_ai_runtime_t` 由一个任务持有；任务读取 20 ms PCM 后调用 `pet_ai_runtime_frame`，按键、云端响应、取消、静音和真实 DMA drain 通过 `pet_ai_runtime_handle` 注入。API 不跨任务共享，也不保存 PCM 指针。

### 端口契约

- `pet_ai_runtime_sink_t` 是执行端口：`begin/start/stop/clear` 对应音频与轮次资源，`forward_pcm/end_input` 对应传输；回调返回 `PET_AI_PORT_OK` 才表示已接受本次动作。接受不等于 DMA 已完成、网络已送达或扬声器已发声。
- `pet_ai_runtime_frontend_t` 是前端端口：`detect_wake` 输出唤醒布尔值，`classify_vad` 输出 `ENERGY/SPEECH/SILENCE`。活动轮次端口失败时，运行边界回送当前轮次的 `PET_AI_AUDIO_FAILED` 或 `PET_AI_TRANSPORT_FAILED`，状态机进入 `ERROR`，并依次尝试取消/停止/清空；每个清理动作最多执行一次，单项失败仍继续其余清理，不递归重试。
- 端口失败返回 `PET_AI_RUNTIME_PORT_FAILED`，`cleanup_result` 单独报告清理是否失败；清理失败后拥有者需要执行硬件级恢复。旧轮次清理失败会阻止新轮次的 begin/start。取消或静音已生效后的清理失败保留控制状态；尚无轮次的唤醒检测失败保留 idle，并返回端口错误，不能伪造一轮会话。
- 运行边界在进入前端之前拒绝乱序时间，避免旧 PCM 触发模型或传输副作用；同一拥有者串行执行清理旧轮次、开启新轮次、转发 PCM、结束输入和播放动作。前端可为空，VAD 为空时沿用 A2 的 RMS 滞回；WakeNet 为空时不会伪造唤醒。
- `PET_AI_PORT_EMPTY` 也按失败处理，不能静默丢帧；后续若要做背压，必须新增可观察的队列/重试契约，不能把“未接受”当作成功。

### 验证边界

`test_ai_runtime` 使用真实 A2 引擎和同步端口替身，覆盖空闲唤醒、VAD 端点转发、动作顺序、响应播放、旧轮次拒收、PCM 乱序预检、前端失败、采集启动失败、传输失败后的清理、清理单项失败继续执行、无递归重试及旧轮次清理失败阻止新轮次启动；主机全套入口与 ASan/UBSan 均通过（macOS `detect_leaks=0`，不包含 LeakSanitizer）。测试替身不代表 WakeNet 命中率、ESP-SR VAD、I²S DMA、FreeRTOS 调度、云端送达或真实播放。

`CONFIG_PET_MODULE_AI_ENABLED` 开启时，固件组件编译 `ai_session.c` 与 `ai_runtime.c`；关闭时两者都排除。默认镜像完整构建与 4 MB 分区检查通过，`nexus_pet.bin` 为 2,840,592 B，静态 DIRAM 为 145,034 B；应用组合根尚未调用 runtime，因此链接器会丢弃未使用实现，镜像大小不能作为运行时内存证据。关闭 AI 的独立 `build-a3-off` 配置确认两个源文件均未进入编译命令，并完成组合根交叉编译，未做该变体整镜像链接。

## 16. A4 拥有者任务与 HAL 流式执行（2026-11-12）

`svc_ai_agent/api.h` 提供真正的 FreeRTOS 服务入口，`ai_agent.c` 在单一任务中持有 A3 runtime、音频设备与传输端口。默认固件不开麦、不创建此任务；A6 提供显式开启的 WSS 组合入口，BOOT 成长/消息操作保留。02/03 未修改，未烧录。

### 接入与所有权

- 组合根一次性调用 `pet_ai_agent_start(config)`，显式传入 `pet_bsp_audio()`、`begin_turn/forward_pcm/end_input/cancel_turn` 传输回调、可选前端与 `volume_pct`（0..100）。配置按值复制；设备和回调 context 必须覆盖运行期。没有缺省传输，也不把缺少云端当作成功；服务运行时不能再启动 A1 诊断或直接操作同一 HAL。
- `ESP_OK` 只代表任务已创建；拥有者收到启动通知后才初始化 runtime 和 HAL，硬件结果由 `pet_ai_agent_snapshot` 的 `ready/faulted/last_error/cleanup_error` 查询。队列或任务创建失败释放本次资源，可重新启动；硬件初始化失败由拥有者 deinit 并锁住服务，运行期故障后不自动重建任务或重置轮次。
- 跨任务调用 `pet_ai_agent_post(kind, turn_id)` 复制控制事件，消费任务生成单调时间。作用域沿用 A2；`TICK` 与 `PLAYBACK_DRAINED` 不允许外部伪造。回调必须非阻塞、不重入，不能保存 PCM 指针。传输端口需要自己的异步队列/送达确认，不应在拥有者里等待网络。
- `pet_ai_agent_play(turn_id, pcm, samples, finish)` 复制 24 kHz mono s16 PCM，生产者返回后可立即复用缓冲区。一个生产者按序提交 1..480 样本的数据块，再提交 `pcm=NULL, samples=0, finish=true` 结束标记；控制队列深度 8，播放队列深度 4。非阻塞队满返回 `ESP_ERR_TIMEOUT` 并累计 `rejected_commands`，生产者必须处理重试/取消，不能丢帧后继续假装完整播放。A7 将静音/解除静音/取消改为独立优先邮箱，不受普通控制队满影响；仍是异步软件请求，不是物理麦克风断电开关。

### 执行规则

| 场景 | 可观察结果 |
| --- | --- |
| 空闲与唤醒 | 无唤醒器时空闲不开麦，press 才开始采集；有唤醒器时 idle/error 在同一 RX 持续检测，唤醒后沿用设备，不重复 enable。muted 不采集，unmute 后才恢复检测 |
| 采集 | 每次最多等 20 ms，短读/带进度的 timeout 累积成完整 320 样本帧；同毫秒立即返回的完整帧暂存至时钟前进，不填零或伪造未来时间。停止采集保留仍需 forward 的最后一帧，之后清空工作缓冲区 |
| 调度与超时 | 每轮消费至多一个控制并驱动 tick、采集或播放；控制积压不跳过 PCM/tick。快速 fake 输入按 20 ms 节拍让出 CPU；真实阻塞读取已消耗节拍时仅 yield。时间间隔仍受 A2 断流/响应超时约束，真实调度时序待测 |
| 播放 | 仅 speaking 消费当前轮次 PCM，按配置衰减一次，支持部分 write/带进度 timeout；旧轮次丢弃并计数。结束标记且至少有数据写入后，等待 HAL drain（最长 120 ms），成功才由拥有者生成 drained |
| 空响应/断流 | 空结束或开始播放后 500 ms 无后续数据，回送 transport_failed 并取消；不会发 drained。采集和写入有 100 ms 无进度保护，A2 tick 同时检查输入断流；不重复发送已写入的部分 |
| 打断与静音 | 先取消/停止旧轮次，清空旧播放队列，再开始新轮次；迟到响应由 turn_id 拒收。idle 的唤醒采集也会在 mute 后停止，不只处理 listening 状态 |
| 失败 | 传输失败且清理成功时保留 ERROR，可由新 press/wake 重试。HAL 错误或清理失败先回送会话错误、尽力 deinit、记录清理结果并锁住服务，禁止继续使用不确定设备；不会凭逻辑状态宣称物理已静音 |

服务资源按一次启动、持续至重启设计；故障退出会释放任务栈，保留快照与队列供诊断，拒绝后续提交。重启前不重新分配实例，避免旧异步事件与新实例轮次混淆。当前没有运行时重启/停止销毁 API。

### 验证与预算

- `test_ai_agent` 运行真实 service/runtime/session/PCM 逻辑，使用主机 pthread 执行任务，FreeRTOS API、音频 HAL 与传输为边界替身。25 个独立场景覆盖启动前运行门控、分配/队列/任务失败清理及同进程重试、HAL 初始化/读写/清理失败、短读短写、同毫秒立即返回、无进度、最后一帧、唤醒与静音、并发队满、旧轮次、打断、空/断流响应及 drain 失败。已纳入 `tools/ci/test_host.sh`，主机全套回归通过。
- 25 场景通过 ASan/UBSan（macOS `detect_leaks=0`，不含 LeakSanitizer）；成功流程、并发队满、唤醒/静音、打断、同毫秒读取与读取失败 6 场景通过 ThreadSanitizer。主机锁/线程证据不替代 FreeRTOS SMP 或 I²S 实测。
- 默认固件完整构建、链接与 4 MB 分区检查通过；已核对新的 `ai_agent.c` 目标文件、静态库及四个公开 API。组合根尚未调用服务，链接器丢弃未使用实现，默认镜像仍为 2,840,592 B、静态 DIRAM 145,034 B、分区余 32%。这不代表运行服务不占内存。
- 关闭 AI 的 `build-a4-ai-off` 使用独立 SDKCONFIG，真实 CMake 排除全部 AI 源文件，组合根交叉编译通过；关闭音频的 `build-a4-audio-off` 排除 `ai_agent.c`，组合根、session 与 runtime 三个单元交叉编译通过。两种变体未做整镜像链接。
- ESP32-S3 目标 DWARF：拥有者对象 1,896 B，控制项 8 B，播放项 972 B；对象、队列 payload 与 6,144 B 栈合计 11,992 B，尚不包含队列/TCB/堆管理开销、HAL DMA、前端模型和传输适配器。真实 heap、栈水位、欠载与响应延迟待上板测量。

协议与 Opus 适配库见第 17 节，WSS 与可选应用接线见第 18 节；真实 WakeNet/VAD 和状态事件仍待接入；隐私/DND 与 UI 仲裁见 A7。播放目前直接按块写入，预缓冲、欠载统计与音质调优待后续验证。A4 切片本身没有引入 ESP-SR、Opus、WSS、AEC、云端凭据、音频持久化或自动录音，不构成完整 AI 对话或 P1/P2 实机验收。

## 17. A5 小智 v1 协议与 Opus 适配库（2026-11-12）

本切片把 03 默认的云端格式隔离在 `svc_ai_agent/xiaozhi.h` 与 `opus.h`，不移植 03 的应用、板级或任务代码。A5 只提供串行适配库，不启动网络或读取凭据；后续 A6 在第 18 节完成网络任务与 A4 接线。02/03 未修改，未烧录。

### 首版兼容范围

| 项目 | 当前行为 |
| --- | --- |
| 协议 | 固定 WebSocket v1；客户端 hello 声明 Opus、16 kHz、单声道、60 ms，不声明 MCP/AEC/字形推送能力 |
| 服务端 hello | 必须明确返回 `transport=websocket`、非空且不超过 64 字节的 `session_id`、Opus、24 kHz、单声道、60 ms；`version` 如存在必须为 1。缺失或不兼容直接拒绝，不沿用隐式默认值 |
| 会话控制 | hello 后生成 `listen/start`，使用 `manual` 让 A2 的本地端点控制结束；末包之后生成 `listen/stop`，手动取消生成 `abort` |
| 上行 | 每次接受真实的 320 样本/20 ms PCM，三个帧聚合为 960 样本/60 ms；结束时仅对最后不足一包的尾部补 20/40 ms 零，不丢最后一帧、不补整包空音频 |
| 下行 | 每条完整二进制消息为一个 Opus 包，最多 1,500 字节；先按 TOC 检查单声道与总时长 60 ms，真实 codec 再校验包体、完整消费与 24 kHz/16-bit/单声道/1,440 样本输出 |
| 背压 | 一包下行 PCM 以三个 480 样本/20 ms 块 `peek`；A4 `play` 成功入队才 `commit`。队满时保留当前块；有未提交 PCM 时新包返回 `BUSY`，不解码、不覆盖，网络拥有者必须保留原消息 |
| 播放结束 | `tts/start` 产出一次 response-begin；`tts/stop` 保留尚未提交的 PCM，之后才暴露零样本 finish 标记。空响应失败；finish 不能作为 DMA drained，实际完成仍由 A4/HAL 判断 |
| 隔离与取消 | 每轮递增 `epoch`，且本地 `turn_id` 严格递增；一条物理连接只能承载这一轮。旧代次在解析/解码前拒绝；可选的服务端 `session_id` 如与本轮不符也拒绝。关闭/取消清空内部 PCM；不能把旧连接的音频重新标成新轮次 |
| 文本边界 | 完整消息最多 1,024 字节、嵌套最多 8 层，校验 UTF-8、原始控制字符、嵌入 NUL、重复字段和尾随内容；安全转义输出会话 ID。转写、句子、表情和工具消息暂不保存或路由到 UI |
| 失败 | codec 失败/异常输出长度锁住本轮并清空 PCM；远端 error/goodbye 产出失败事件。无自动重试或静默格式回退，发送失败必须关闭本轮，不能重放已消费 PCM |

实际格式依据是当前 03 的 `main/protocols/websocket_protocol.cc`、`protocol.cc`、`main/audio/audio_service.cc` 与 `docs/websocket.md`，其中服务端参数的严格校验是 04 首版的明确兼容限制。TOC 位与包帧数依据 [RFC 6716 第 3 节](https://www.rfc-editor.org/rfc/rfc6716.html#section-3)。早期规划中的“20 ms 上行”在 A5 指内部 PCM 粒度，线上按 03 默认聚合为 60 ms；下行线上 Opus 解码后才进入 A4 的 PCM 接口。

### 所有权与后续接线约束

- `pet_xz_t` 生命周期只初始化一次，由单一网络/codec 拥有者串行调用；旧连接关闭且回调全部撤销后才 `close/begin`。网络回调必须捕获原 `epoch`，关闭自动重连复用；代次耗尽不得回绕。
- `pet_opus_t` 零初始化后显式 `open`，每轮 reset 编解码状态；使用锁定的 `espressif/esp_audio_codec 2.5.0` 直接 API。编码为 16 kbps VBR、复杂度 0、VOIP，不启用 FEC/DTX；解码不请求 PLC。SDK 部分创建失败释放已分配资源；释放失败锁住实例至重启，避免重复释放不确定句柄。
- 网络任务需要向 A4 提供非阻塞入队端口，并实现握手超时、有限 PCM/消息队列、WebSocket 分片重组、连接关闭、凭据注入与 TLS 验证。不能在 A4 的采集循环中等待 hello、执行阻塞 WSS 或做编解码。
- A5 事件/数据由 A6 网络任务映射到 A4：先成功投递 `PET_AI_RESPONSE_BEGIN`，再按原序 `play` PCM 与 finish；队满必须保留并重试，超时/传输错误需投递 `PET_AI_TRANSPORT_FAILED`。主机任务接线不构成设备云端链路验收。

### 构建与验证

`CONFIG_PET_AI_XIAOZHI_ENABLED` 默认开启，依赖 AI 与音频开关；只编译适配库，不启动任何服务。`idf_component.yml` 锁定 `cjson 1.7.19~2` 和 `esp_audio_codec 2.5.0`，锁文件由 IDF 生成；功能关闭时仍解析依赖，但不编译对应适配源文件。

```sh
# 首次先用 ESP-IDF 6.1 解析锁定依赖；不烧录。
idf.py -C firmware reconfigure
bash tools/ci/test_ai_protocol_host.sh
PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_ai_protocol_host.sh
```

- 新主机入口运行真实协议状态/解析器与锁定的 cJSON 源码；codec SDK 使用真实头文件，外部二进制编解码函数为边界替身。覆盖握手、控制、20/60 ms 聚合/拆分、尾包补齐、包长上限、播放背压与提前结束、取消清空、旧连接、逐字节截断、畸形 JSON/TOC、SDK 启动/大小/消费/格式/清理失败。普通检查和 ASan/UBSan 通过；macOS `detect_leaks=0`，不包含 LeakSanitizer。测试不证明真实 Opus 算法执行或音质。
- 原有 `test_host.sh` 全套回归通过；组件依赖图、板级生成、契约校验与差异空白检查通过。`build_firmware.sh` 在默认固件构建后执行新增协议测试，首次运行无需依赖 02/03 的文件。
- 默认 ESP32-S3 完整固件构建、链接与 4 MB 分区检查通过。额外使用链接器 `--undefined` 强制保留 15 个 A5 公开 API，并确认四个真实 vendor Opus open/process/decode 符号均已链接；这验证 ABI/依赖闭合，不执行设备代码。
- 关闭小智适配、关闭 AI、关闭音频三种独立 `SDKCONFIG` 均通过真实 CMake 源文件门控检查，分别交叉编译组合根与适用 AI 源文件 4/1/3 个单元；没有进行这三种变体的整镜像链接。
- 默认组合根尚未引用适配库，链接器会裁掉未使用代码，默认镜像仍为 2,840,592 B、静态 DIRAM 145,034 B；不能据此估算启动 AI 后的资源峰值。ESP32-S3 DWARF 中 `pet_xz_t` 为 4,912 B、`pet_opus_t` 为 12 B，未包含原生 codec 堆、JSON 树、网络队列/包缓冲、任务栈与 TLS。

WSS 网络拥有者与 A4 队列接线见下一节。真实 WakeNet/VAD、云端协议联调、真实编解码执行、握手/TLS/授权、ASR/LLM/TTS、时延、音质、栈/堆水位与硬件验收均未完成。

## 18. A6 WSS 网络拥有者与 A4 接线（2026-11-12）

`svc_ai_agent/wss.h` 提供一次性 `pet_xz_wss_start(config, &transport)` 和只读快照。独立 FreeRTOS 任务独占 A5 状态、Opus 与 WebSocket；返回的传输端口只供一个 A4 拥有者调用，非 ISR、非阻塞，不回调或重入 A4。服务在设备上运行，日常对话不依赖 Mac。当前只完成软件与构建验证，未烧录、未激活设备、未连接真实云端。

### 启动与凭据

- `CONFIG_PET_AI_XIAOZHI_WSS_ENABLED` 默认编译，依赖小智、真实网络、PSRAM、WS transport 与 TLS 证书包；只编译不会创建任务。锁定 `espressif/esp_websocket_client 1.8.0`，由 IDF 生成依赖锁文件。
- `CONFIG_PET_AI_XIAOZHI_ON_BOOT` 默认关闭；显式开启后组合根在网络启动之后调用 `app_ai_start`，与开机录放音诊断、编译期 DND 互斥。`CONFIG_PET_AI_VOLUME_PCT` 默认 25，范围 0–100。没有添加凭据到 sdkconfig 或写入 NVS。
- 启动读取 NVS `xiaozhi` 字符串：必需 `ws_uri`（最多 255 字节）、`ws_token`（512）、`device_id`/`client_id`（各 64）；可选 `ws_ca`（4096，PEM）。只有 `ws_ca` 不存在时选择系统证书包；存在但为空、类型错误或超长均失败。必需项缺失或无效跳过 AI 启动。临时凭据存于静态缓冲，复制后显式清零，避免占用 main 的 4 KB 栈。
- URI 只接受合法 `wss://`，拒绝 userinfo、fragment、无主机、非法端口和控制/空白字符；token 接受原值或 `Bearer ` 前缀，拒绝头部注入。每次握手发送 v1、Authorization、Device-Id、Client-Id，始终验证证书与主机名，不提供跳过 TLS 校验的开关。
- `start` 返回成功只表示任务创建，codec 与 HAL 异步结果分别查询 WSS/A4 快照。初始化不开麦、不建立连接；调用 `pet_ai_agent_post(PET_AI_TALK_PRESS, 0)` 才开始一轮，松开使用该轮 `turn_id` 投递 `PET_AI_TALK_RELEASE`。BOOT/UI 输入与隐私路由见 A7；真实 WakeNet/VAD 和状态事件尚未接入，原 BOOT 消息/成长功能保留。

WSS 开轮前要求 Wi-Fi 已有 IP 且 SNTP 已同步，不依赖 MQTT 连接状态。Wi-Fi 启动仍使用现有 `svc_net`，其组合根要求 Wi-Fi/MQTT 配置完整；A6 没有拆分网络初始化或新增独立配网/激活/OTA 凭据流程。仅填小智 NVS 项不能让未配置的网络自动连上。

### 队列、时序与失败

| 边界 | 已实现行为 |
| --- | --- |
| 上行 | PSRAM 队列深度 128，每项一帧 320 样本 PCM 或结束标记，容量约 2.56 s；入队复制，结束按序处理。队满明确失败，取消/最新 begin 使用独立锁邮箱，不被旧积压阻塞；新轮清掉旧队列并保留已经取出的新轮首帧 |
| 物理连接 | 每轮新建连接，禁用两类自动重连；旧 socket 的 destroy 等待 SDK 任务和事件循环退出后才复用 binding。事件捕获原 epoch，旧轮数据不会被重新标记为新轮。发送不完整即失败，不自动重放 PCM |
| 接收 | SDK 回调只复制最多 512 字节分块到深度 32 的 PSRAM 队列；队满独立记录错误。拥有者重组 SDK offset 与 RFC continuation，PING/PONG 不打断应用分片；文本上限 1024、二进制上限 1500 字节，非法顺序/尺寸直接关闭本轮 |
| 超时 | hello 5 s；完整消息组装 2 s；A4 播放端持续无进度 500 ms；单次发送等待 200 ms，SDK 网络超时配置 1 s。它们是软件检查/调用预算，不是实际 DNS/TLS/任务调度延迟保证 |
| 下行与完成 | response-begin 成功入 A4 后才接受音频，按原序 peek/play/commit；队满保留当前 PCM 和未解码消息。finish 只入队，真实完成仍由 A4/HAL drain 判断。`tts/stop` 后远端立即关闭也会先消费已排队的完整响应 |
| 故障通知 | 网络/分片/发送/背压失败投递本轮 transport-failed，控制队满重试；A4 快照尚未发布该轮时保留通知，更新轮次才淘汰旧失败。普通传输错误允许显式新 press 重试，不自动重连 |
| 故障锁定 | codec 失败或 SDK 销毁失败锁定至重启；销毁失败保留不确定句柄，不再次销毁或复用，已接受的新轮次也立即收到失败。取消端口仍接受合法轮次，让 A4 完成硬件清理 |

WSS 服务及队列持续至重启，没有运行期重建 API；若后续 A4 创建失败，已创建的 WSS 保持空闲到重启。模块不打印凭据、PCM 或转写，不持久化音频。SDK 的 destroy 内部包含无限等待，尚不能保证关闭耗时上限；该操作只阻塞网络任务，A4 不等待它，但实际 CPU/网络资源竞争仍需实测。

### 验证入口与边界

```sh
# 先加载 ESP-IDF 6.1 并解析本仓库锁定依赖；均不烧录。
bash tools/ci/build_firmware.sh
PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_ai_wss_host.sh
PET_HOST_TSAN=1 bash tools/ci/test_ai_wss_host.sh
```

`test_xiaozhi_wss` 运行真实 WSS/A5/Opus 适配/A4/runtime/session 与锁定的 cJSON、IDF HTTP parser；只在 SDK WebSocket、vendor codec 二进制、OS、网络和 I²S 边界使用替身。场景覆盖启动资源/凭据错误、TLS 配置、配置/PCM 复制、两层分片、线序、IP/SNTP 门控、队满取消、新轮首帧、音频背压、远端关闭、超时、解码/销毁失败、失败通知竞争与控制队满重试。主机测试不执行真实 TLS、Opus 算法或 I²S，也不能证明云端授权、音质或 FreeRTOS SMP 行为。

验证记录：31 场景普通检查、ASan/UBSan 和 ThreadSanitizer 均通过；macOS ASan 使用 `detect_leaks=0`，不包含 LeakSanitizer。`build_firmware.sh` 的依赖/板级/契约、原有主机全套回归、默认完整固件、协议与 WSS 检查均以退出码 0 通过。独立 `build-a6-on` 的启动开关完整构建、链接和分区检查通过；已核对启动入口、WSS、A4 和真实 SDK WebSocket/Opus 入口在 ELF 中，全部公开 WSS/A4 API 在目标静态库中。未调用的查询 API 可被链接器裁掉。

关闭 WSS、关闭网络、启用 fake 网络三种独立 SDKCONFIG 均通过真实 CMake 门控检查，WSS 源文件被排除，各自组合根和适用 AI 源文件 7 个单元交叉编译通过；三种变体未做整镜像链接。另以真实 `kconfgen` 验证 DND 或开机诊断开启时，AI 启动开关解析为关闭；未把配置解析当作运行时静音验收。

| 配置 | 镜像字节 | 静态 DIRAM 字节 | 4 MB 应用分区余量 |
| --- | ---: | ---: | ---: |
| 默认，不启动 AI | 2,840,592 | 145,034 | 约 32% |
| `build-a6-on`，显式启动 AI | 3,072,992 | 153,554 | 1,121,312 B，约 27% |

ESP32-S3 DWARF 中 WSS 对象为 14,344 B、TX 项 648 B、RX 项 532 B。两条队列 payload 合计 99,968 B PSRAM；对象和 8,192 B worker 栈合计 22,536 B 内部内存，另有 SDK 6,144 B 栈及 A4 的 11,992 B 基础预算。这些数值不包含队列/TCB/堆管理、codec/JSON/SDK 对象、TLS/Wi-Fi、HAL DMA 与模型；静态余量不能作为运行时 heap、栈水位或稳定性证据。

输入/UI/静音与 DND 仲裁见 A7。真实 WakeNet/VAD、云端联调与实机资源/时延验收仍待推进；A6 不构成 P1/P2 完整验收。

## 19. A7 BOOT 输入、AI 状态与隐私仲裁（2026-11-12）

本切片实现 BOOT 语音入口、AI 状态页与运行期静音/DND 仲裁。沿用 `ui_lvgl` 单任务拥有 LVGL、A4 单任务拥有音频 HAL；UI 通过公开 API 提交意图并复制快照，不从按键或绘图代码启停 I²S。没有新增依赖，没有修改 02/03，没有烧录或连接真实云端。

### 输入与显示

- 保留 BOOT 的 30 ms 消抖、800 ms 长按、消息确认与成长菜单。AI 已就绪时长按打开菜单默认选择说话项；未启用 AI 时保持原成长入口。语音菜单也可在关闭成长模块时使用；开机录放音诊断模式不提供这组操作。
- 选中“按住说话”后持续按住 800 ms 开始一轮，出现“正在听”后说话，松开结束。松开由拥有者绑定原轮次，UI 不猜测 `turn_id`。同一任务周期内完整按下/松开可合并为未开始；短于最短语音时取消，不补录按下前的声音。
- 活动对话短按取消，长按打开菜单；静音/DND 项短按切换、长按执行。返回项回到主页。开机按住、消息/显示故障抢占、策略变化都会屏蔽旧手势，恢复后必须重新松开再按；旧松开不能确认新消息或重新开麦。
- 监听、思考、播放、错误、静音和不可用状态使用同一 AI 视图；复用现有字体和宠物图元。输入电平只在实际采集的 LISTENING 中显示，其他状态清零。未启动、初始化、失败及清理未确认均有独立反馈，不用 MUTED 逻辑状态冒充物理已停止。
- 显示顺序为 critical、用户打开的菜单、活动 AI、待显示普通消息、成长主页；已经显示的消息保留原画面代次/回执规则。活动 AI 和菜单期间普通弹窗延后，critical 仍可抢占。错误/空闲页面不阻止普通消息；被遮挡的成长/AI 图元停帧，正常播放结束恢复成长主页。

### 策略与跨任务契约

`svc_ai_agent/api.h` 新增 `pet_ai_agent_ptt` 和 `pet_ai_agent_set_policy`。`pet_ai_policy_t` 的 `muted`、`dnd`、`interrupted` 彼此独立：

| 策略 | 已实现行为 |
| --- | --- |
| 用户静音 | 取消当前轮，停止采集/播放，清空待播放数据；阻止 PTT 与唤醒。解除后不恢复旧会话 |
| DND | 同样门控 AI 采集/播放，显式 PTT 也不能越过；UI 同时将 normal/low 消息留在队列，high/critical 保留既有规则。关闭 DND 不解除用户静音 |
| critical/显示故障 | 取消当前轮，并在 critical 画面存在或主页刷新失败期间阻止重新唤醒/PTT。解除只允许后续新输入，不重播旧音频 |
| 优先请求 | MUTE/UNMUTE/CANCEL、完整策略和 PTT 电平使用独立锁邮箱；不受普通控制队列容量影响。策略/取消递增控制代次，作废更早排队控制 |
| 快照 | `policy` 是请求值，`policy_pending` 表示拥有者未完成处理；`capture_active/playback_active` 是最近成功 HAL 启停。faulted/cleanup_error 优先于“已停止”展示 |

普通 `post` 和 PCM `play` 仍按原有限队列返回背压错误。优先请求是异步软件控制，不能撤回已经交给网络或硬件的工作，也不保证物理停止延迟；采集读操作中到达的隐私请求会在转发前丢弃该帧。HAL/清理失败仍尽力 deinit 并锁住至重启。

UI 通过 `set_dnd`、`set_interrupted` 和单项 MUTE/UNMUTE 原子更新各自开关，保留其他来源的策略。UI 每 10 ms 读取按键和快照，活动 AI 页面最多每 100 ms 更新；这些是软件调度设置，不是实屏延迟保证。DND/静音选择仅本次开机有效；编译期 DND、开机诊断与 AI 启动互斥保持不变。本轮不实现配置下发/持久化、真实 WakeNet/VAD、AEC、转写/上下文、完整 AI 专用口型动画或音质优化。

### A7 验证记录

- `tools/ci/build_firmware.sh` 的依赖/板级/契约检查、原有主机回归、默认固件、协议和 WSS 检查全部以退出码 0 通过。更新后的音频拥有者共 34 场景，全部通过普通检查、ASan/UBSan 和 TSan；WSS 共 32 场景，包含隐私取消/新 PTT，全部通过普通检查、ASan/UBSan 和 TSan。ASan 使用 `detect_leaks=0`，不包含 LeakSanitizer。
- `test_growth_input` 验证 BOOT 菜单/持续按住/松开/取消/旧手势保护；`test_message_presenter` 验证语音期间延后普通弹窗与 critical 抢占，原消息代次/回执回归通过。`test_ui_host.sh` 的真实 LVGL 两组测试通过；AI 帧检查不可用、初始化、监听/思考/播放/错误/静音、真实采集条件、电平上限/归零、待处理与清理故障、菜单和布局，输出 `firmware/build-ui-preview/ai-*.ppm`。
- 默认与独立 `build-a6-on` 启用配置均完成最终完整构建、链接和分区检查，并检查对象时间戳覆盖最新源码。六种独立配置（关闭 AI、音频、成长、UI、同时关闭 AI/成长，以及开机诊断）通过真实 CMake 源文件门控，适用 UI/AI/组合根分别 7/9/13/9/4/13 个单元交叉编译；这些变体没有整镜像链接。

| 配置 | 镜像字节 | 静态 DIRAM 字节 | 4 MB 应用分区余量 |
| --- | ---: | ---: | ---: |
| 默认，不启动 AI | 2,846,752 | 145,050 | 1,347,552 B，约 32% |
| 显式启动 A4/WSS | 3,078,896 | 153,554 | 1,115,408 B，约 27% |

目标 DWARF 中 A4 对象 1,944 B、控制项 16 B、播放项 972 B；对象、8 项控制/4 项播放 payload 与 6,144 B 栈合计基础预算 12,104 B，取代 A4/A6 历史记录中的 11,992 B。当前完整 UI 上下文为 27,248 B，仍优先分配 PSRAM；未包含 LVGL 对象、字体/绘图缓冲、队列/任务管理开销、codec/TLS/Wi-Fi/模型堆和 DMA。静态余量不代表运行期资源峰值或稳定性。

以上是公开 API、主机调度边界、真实视图与目标构建证据，未运行完整 UI FreeRTOS 任务的端到端测试。没有烧录、真实录音/放音、云端授权/协议联调、物理静音、按键/屏幕时延、堆/栈水位或长稳验收；A7 不构成 P1/P2 整体验收。

## 20. A8 WakeNet 与模型 VAD 接入（2026-11-12）

`svc_ai_agent/sr.h` 提供零初始化实例和前端工厂，固定使用 `wn9_xiaoaitongxue`（“小爱同学”）与 `vadnet1_medium`。ESP-SR 锁定为 2.5.5，通过公开模型接口调用；没有复制 03 的实现，也没有引入 AFE/AEC。官方 [更新记录](https://github.com/espressif/esp-sr/blob/master/CHANGELOG.md)记录了 IDF 6.1 兼容修复和 2.5.5 的镜像体积优化。

唤醒词于 2026-11-12 按用户要求切换为内置“小爱同学”；构建项为 `CONFIG_SR_WN_WN9_XIAOAITONGXUE`，型号与文字对应关系同时核对了锁定 SDK 的 Kconfig/模型资源及官方 [WakeNet 说明](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/wake_word_engine/README.html)。对话供应商、NVS `xiaozhi` 命名空间与 WSS 协议保持原接线。复用已有 `SDKCONFIG` 时须关闭先前唤醒模型再选新模型；仅追加 defaults 不会覆盖已有选项，同时打入旧模型会被固定双模型检查拒绝。

### 显式启用与资源所有权

- `CONFIG_PET_AI_ESP_SR_ENABLED` 默认关闭，依赖 AI、真实音频与 PSRAM。工厂本身不加载模型或开启麦克风；A4 音频任务在 HAL 初始化后 `open` 前端，成功后才发布 ready。`open` 失败自行回滚部分资源，A4 尽力 deinit 音频并锁住至重启，不回退为能量监听。
- 模型创建、检测、reset 和销毁均在同一个音频任务串行执行。检测失败或返回非法活动标签也按音频故障锁住服务，停止采集并关闭前端，不按普通网络错误重新监听。启用带 `open` 的前端时该任务栈为 8,192 B；模型适配状态与有界 PCM 缓冲放 PSRAM。模型内部堆和栈峰值仍需上板测量。
- 停止采集、取消、策略中断和模式切换会清除未满模型窗口的 PCM、VAD 标签及模型内部状态。故障清理先销毁模型句柄，再解除模型包 mmap，最后清零并释放适配状态；成功 open 对应一次 close。
- 开启模型与 AI 开机服务、且 NVS 凭据有效后，IDLE/可重试 ERROR 状态开始本地唤醒采集。唤醒或 PTT 才开云端轮次；空闲监听音频不上传、不保存。THINKING、SPEAKING、静音、DND 与 critical/显示故障中断关闭采集。解除门控后允许新的本地监听，不恢复旧轮次。
- UI 在实际空闲采集时显示“等待唤醒”“请说‘小爱同学’”，主页保留采集标记，菜单显示“麦克风已开启”；错误页在保留失败原因的同时显示仍在采集，不能用“连接失败”遮住本地监听状态。隐私请求在模型回调期间到达时，网络 begin 和 PCM 转发前再次门控；软件异步控制仍不能承诺物理停麦延迟。

### 帧适配与模型包

输入保持 16 kHz/单声道/s16、每帧 320 样本。适配层查询并校验两个模型的采样率、声道和窗口大小，接受 160–960 且为 16 倍数的窗口；只用一个最多 960 样本的缓冲拼接窗口，不保留调用方 PCM 指针。唤醒与会话 VAD 互斥；VAD 尚未凑足窗口时初始返回静音，之后保留最近的模型标签。重复/倒退时间拒绝，超过既有帧间隔限制则清理历史状态。

VAD 使用 `VAD_MODE_1` 和 32 ms 最短语音/噪声参数，由既有 session 统一执行最短语音、300 ms 静音端点和 15 s 上限。模型本身存在窗口与判别延迟，软件的 300 ms 阈值不能视为物理语音结束后恰好 300 ms；见官方 [VADNet 说明](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/vadnet/README.html)。播放期间关闭 RX，当前不支持声学打断。

`tools/check_sr_models.py` 在编译适配器前校验打包产物：只接受这两个模型、完整且不重叠的文件范围、无额外尾部数据，镜像不超过 960 KiB。`model` 分区保持 `data/spiffs`、偏移 `0xE20000`、大小 1 MiB；这是 SDK 的原始模型包，不挂载为文件系统。校验后生成镜像长度与 SHA256 头文件，启动先验证分区内容，再交给 SDK 解析；错误或不配套镜像在开麦前失败。固件与模型包必须配套更新，当前没有新增 OTA 或迁移流程。

### 构建与验证入口

在仓库根目录加载 ESP-IDF 6.1 后运行，命令均只构建或做主机检查：

```sh
# 独立配置避免改变默认的“不启动 AI、不录音”。
idf.py -C "$PWD/firmware" -B "$PWD/firmware/build-ai-sr" \
  -D SDKCONFIG="$PWD/firmware/build-ai-sr/sdkconfig" \
  -D "SDKCONFIG_DEFAULTS=$PWD/firmware/sdkconfig.defaults;$PWD/firmware/sdkconfig.ai-sr.defaults" build
bash tools/ci/test_host.sh
bash tools/ci/test_sr_frontend_host.sh
PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_sr_frontend_host.sh
```

模型主机测试使用锁定 SDK 的真实 API 头文件；推理二进制、分区、分配器和密码学函数为外部边界替身。测试覆盖 480/512 样本窗口拼接、切换/断流/时间错误、标签保持、reset、重复生命周期、模型/分区不符、hash/分配/加载/接口/部分创建失败与资源回收。包校验 CLI 用合成模型验证有效包、篡改/缺失/额外模型、越界/重叠与旧头文件保护，实际构建另检查 vendor 模型包。它们不执行真实 WakeNet/VAD 推理或设备 SHA256，不构成识别率、音质或硬件验收。

音频拥有者当前共 42 场景，全部通过普通检查、ASan/UBSan 与 TSan，新增前端初始化/reset/close、推理期间策略/取消，以及唤醒/会话检测失败和非法标签的锁定回收检查。模型前端通过普通与 ASan/UBSan 检查；包校验 CLI 两组测试通过。真实 LVGL 两组测试通过，包含空闲/错误监听的采集可见性与布局；人工查看了对应主机帧。ASan 使用 `detect_leaks=0`，不包含 LeakSanitizer。

`build_firmware.sh` 的依赖图、板级生成、契约、全部主机回归、默认完整固件/size、协议、32 场景 WSS 与模型前端检查均以退出码 0 通过。独立 `build-a8-sr` 使用默认 debug 优化，完成最终完整构建、链接与分区检查；核对对象时间戳覆盖最新拥有者/前端/启动/UI 源码，ELF 保留模型加载/选择、SHA256、A4、WSS 与 codec SDK 入口。未调整分区或依靠未启用代码被裁剪来证明模型版可用。

| 配置/产物 | 字节 | 限制/余量 |
| --- | ---: | --- |
| 默认，不启动 AI | 2,847,008 | 4 MiB 应用分区剩余 1,347,296 B，约 32% |
| 显式启动 AI 与双模型 | 3,426,944 | 同一应用分区剩余 767,360 B，约 18% |
| 双模型包 `srmodels/srmodels.bin` | 579,215 | 低于 960 KiB 检查上限，存入固定 1 MiB 分区 |
| 默认/模型版静态 DIRAM | 145,050 / 171,754 | 不包含运行期分配、任务栈与模型堆 |

实包 SHA256 为 `8f859601e0873438fa57553e648f25db5d64ce3ec2ba966a707e7d27ef8cd5cb`。ESP32-S3 DWARF 中 A4 对象与前端状态分别为 1,976 B；带模型时 A4 对象、8 项控制/4 项播放 payload 与 8,192 B 栈合计基础预算 14,184 B，另有 1,976 B 前端 PSRAM 状态。没有前端 open 时栈仍为 6,144 B。此预算不包含 TCB/队列管理、vendor 模型与 codec、JSON/TLS/Wi-Fi、LVGL 或 DMA，不能作为运行期内存余量。

真实 `kconfgen` 检查七种配置：关闭 AI/音频/PSRAM、启用 fake 音频或显式关闭 SR 都会关闭模型；开机 DND 或录放音诊断会关闭 AI 自动启动。默认与模型版的真实 CMake 源文件选择也已核对。除这两种完整固件外，没有重新链接七种变体。IDF 6.1 的 Wi-Fi/wpa_supplicant 私有 include 检查仍有五条上游 CMake 警告与既有 Kconfig 弃用提示，不宣称零警告。

本次“小爱同学”切换后重新通过包校验 CLI（包含拒绝旧唤醒模型）、前端普通/ASan/UBSan、两组真实 LVGL 与完整默认 CI；模型版完整构建、链接和分区检查通过，核对新模型选择、实包哈希、嵌入校验头及目标对象时间戳，屏幕提示已人工查看。以上模型前端测试仍使用 vendor 推理边界替身。

未烧录、未连接真实云端；识别率、噪声环境、模型耗时、I²S DMA 积压、PSRAM/堆/栈水位、物理静音与端到端对话仍待实机验收。AEC/声学打断尚未实现。

## 2026-11-12 软件补齐

新增 100 ms 播放预缓冲、欠载计数/静音/超时、尾部淡出、转写/回复、电平口型与点头。RAM 保存最多 8 轮，旧轮合并为有界提取式摘要；人设和宠物状态经 self.get_context 提供，隐私/超时/主动清除丢弃本地上下文，不宣称删除云端历史。MCP 路由温湿度、未读、成长、专注与可选扩展，严格参数和幂等。

NS/AEC 适配已接锁定 ESP-SR 算法；当前半双工 BSP 不提供同步参考，AEC 回退 NS，未实现可验收的声学全双工打断。AI 未启用时可通过 PET_MESSAGE_TONES_ON_BOOT 仅启动提示音拥有者，不创建录音/云端会话。详细测试与边界见 [09](09-software-delivery.md)。
