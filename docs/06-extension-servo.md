# 06 · 扩展二：语音控制舵机等硬件

状态：首版单路舵机软件已实现（2026-11-12）：LEDC 驱动、限位/速度约束、有限序列、持久请求去重与急停。扩展默认关闭，GPIO 默认 `-1`，启动不输出 PWM；必须完成供电/机械校准后，通过物理维护入口显式 arm。尚未执行机械动作；下文多路方案属于后续设计。依赖：`00-architecture.md`、`01-ai-agent.md`。

## 1. 范围

做：定义执行器抽象、舵机配置、结构化指令、自然语言解析边界、安全限制、序列播放、急停、校准与 mock 测试。

不做：首版多路总线舵机闭环、力控/电流环、复杂机械臂逆运动学、无限制角度/速度。

## 2. 硬件方案对比

| 方案 | 优点 | 缺点 | 适用 |
| --- | --- | --- | --- |
| LEDC PWM 直驱 | 简单、无额外芯片、成本低 | 路数少、无位置反馈、抖动 | **首版 1–2 路**（尾巴/头） |
| PCA9685 I2C 16 路 | 路数多、布线简单 | 需外部 5V、无反馈、I2C 干扰 | 二期多路 |
| UART 总线舵机 | 位置/温度反馈、菊花链 | 成本高、协议差异、调试复杂 | 需要反馈/多轴时 |

首版建议：1 路尾部舵机 + 可选 1 路头部舵机，LEDC 50 Hz，独立 5V 供电；预留 `actuator_t` 以便换 PCA9685/总线舵机。

## 3. 电气与安全

| 项 | 要求 |
| --- | --- |
| 供电 | 独立 5V ≥2 A；不与 ESP32 3.3V 或音频功放共用细线 |
| 共地 | 舵机地、ESP32 地、功放地单点共地 |
| 去耦 | 舵机电源并联 470–1000 µF + 0.1 µF |
| 电平 | 舵机 PWM 3.3V 通常可识别；不确定时加电平转换 |
| 角度 | 默认 0–180°，按机械结构限制到安全范围 |
| 速度 | 限制 °/s，避免突然大幅动作 |
| 急停 | 软件急停 ≤100 ms；建议保留物理断电 |
| 上电位置 | 初始化到安全中位，不执行上次动作 |
| 堵转 | 无电流反馈时限制单次动作时长与频率，观察电源跌落 |

## 4. 接口契约

```c
typedef struct {
    uint8_t id;
    int8_t gpio;
    uint16_t min_us;        // 默认 500
    uint16_t max_us;        // 默认 2500
    int16_t min_angle;      // 默认 0
    int16_t max_angle;      // 默认 180
    bool inverted;
    uint16_t max_speed_dps; // 最大角速度
    int16_t safe_position;  // 上电/急停位置
} servo_config_t;

typedef struct {
    esp_err_t (*init)(const servo_config_t *cfg, size_t count);
    esp_err_t (*move_to)(uint8_t id, int16_t angle, uint16_t duration_ms);
    esp_err_t (*set_speed)(uint8_t id, uint16_t dps);
    esp_err_t (*stop)(uint8_t id);
    esp_err_t (*emergency_stop)(void);
    esp_err_t (*get_state)(uint8_t id, servo_state_t *out);
} actuator_t;
```

结构化指令（LLM 只产出意图，不直接写寄存器）：

```json
{
  "intent": "servo.move",
  "confidence": 0.9,
  "params": {
    "action": "set_angle",
    "servoId": 1,
    "angle": 30,
    "durationMs": 600,
    "easing": "ease_out"
  },
  "requiresConfirmation": false
}
```

支持的动作：

| 动作 | 参数 | 说明 |
| --- | --- | --- |
| `set_angle` | `servoId`, `angle`, `durationMs`, `easing` | 单次角度 |
| `sequence` | `servoId`, `name` 或 `keyframes[]`, `loop` | 预设/关键帧序列 |
| `home` | `servoId` | 回安全位置 |
| `stop` | `servoId` 或 `all` | 停止当前动作 |
| `emergency_stop` | 无 | 全部急停 |

## 5. 指令解析模块

```text
语音文本 → LLM 意图（servo.move）→ 参数校验
  → 安全守卫（范围/速度/时长/频率）
  → 序列播放器 / 单次动作
  → actuator_t 执行
  → 结果事件 + 表情/语音反馈
```

解析规则：

- 只接受注册过的动作与参数名；未知字段拒绝。
- 角度必须是整数，范围内；`durationMs` 100–5000；速度 ≤`max_speed_dps`。
- 大幅动作（>90° 或速度 >120 °/s）要求确认；`emergency_stop` 永不确认、立即执行。
- 自然语言“摇尾巴”映射到预设序列，不允许动态生成任意关键帧。
- 连续的 `stop`/`emergency_stop` 优先于队列，清空未执行动作。
- 指令解析为纯函数，主机可测；执行层可 mock。

## 6. 序列播放与状态

```c
typedef struct {
    int16_t angle;
    uint16_t duration_ms;
    uint16_t hold_ms;
} servo_keyframe_t;

typedef struct {
    const char *name;          // "wag_tail", "nod_head"
    uint8_t servo_id;
    const servo_keyframe_t *frames;
    size_t frame_count;
    bool loop;
} servo_sequence_t;
```

- 播放器按时间轴插值，单次 tick ≤10 ms；不阻塞 UI/音频任务。
- 支持取消：收到 `stop`/急停后 100 ms 内停在当前位置。
- 序列结束发 `EVT_ACTUATOR_RESULT`；失败/超时同样上报。
- 上电不自动播放；首次使用需校准并保存 `servo_config_t`。

## 7. 前置架构交付物

- `extensions/servo/include/actuator.h`、`servo_sequence.h`、`command_parser.h`。
- `extensions/servo/README.md`：接线、供电、校准、安全规则、预设序列。
- `config/schemas/servo_command.schema.json`。
- Mock 执行器 + 主机测试：范围、速度、超时、取消、急停、序列插值。

本阶段不接真实舵机、不烧录、不移动机械结构。

## 8. 实现步骤（后续）

| 步骤 | 内容 | 产出 |
| --- | --- | --- |
| 1 | 契约 + mock + 解析器测试 | 主机测试 |
| 2 | LEDC 驱动 + 单舵机校准 | 角度准确、无抖动 |
| 3 | 安全守卫 + 急停 | 越界/超速拒绝 |
| 4 | 预设序列 + 播放器 | 摇尾/点头 |
| 5 | 语音意图接入 | “摇尾巴”端到端 |
| 6 | 多路 PCA9685 / 总线舵机 | 适配器替换 |
| 7 | 电源/温升/堵转测试 | 实机验收 |

## 9. 验收标准（接口阶段）

- 解析器拒绝未知动作、缺参、越界角度、超速、超时长；错误码明确。
- 同一 `requestId` 重复执行只移动一次（幂等）。
- `stop`/`emergency_stop` 在 100 ms 内生效，清空队列。
- 序列插值时间误差 ≤20 ms；取消后不再发新帧。
- 控制任务单次 tick ≤10 ms；不阻塞 UI/音频。
- Mock 测试覆盖范围、速度、超时、取消、急停、序列循环。
- 文档明确独立 5V 供电、共地与去耦要求。

## 10. 风险与待确认

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 舵机堵转/过流 | 电源跌落、重启 | 独立 5V、大电容、限频限时、物理断电 |
| PWM 抖动 | 抖动/噪音 | LEDC 硬件、稳定电源、避开 Wi-Fi 干扰 |
| 机械限位 | 损坏结构 | 软件角度限制 + 机械限位 |
| 语音误触发 | 意外动作 | 确认、预设动作、急停 |
| 多路扩展 | 供电/布线复杂 | 二期 PCA9685 + 独立电源 |
| 无位置反馈 | 无法确认到位 | 首版开环 + 限位；二期总线舵机 |

待确认：舵机型号/数量/安装结构、供电方案、角度范围、是否需要位置反馈、首批预设动作。

## 2026-11-12 当前实现

纯引擎支持校准 profile、插值、8 帧/10 秒序列、限位/速度/确认门控、持久去重、调度故障撤销 PWM、停止/急停。固件接单路 LEDC 拥有者，GPIO=-1 默认不启用，初始化不出 PWM，MCP 无 arm 权限。电气与机械校准后才允许物理串口 arm；状态是命令角度，没有位置反馈。操作与未验收范围见 [扩展说明](../extensions/servo/README.md)。


2026-11-15 硬件基线：舵机为可选扩展，发行版默认关闭。完整扩展接线建议预留 GPIO2 给 PWM，避免摇杆 GPIO4/5/6、光敏 GPIO7、DHT11 GPIO15 与基础显示/音频。是否可驱动由板级引脚守卫、显式配置和实物校准共同决定；不修改 GPIO=-1、限位、arm 和急停规则。见 [27](27-hardware-and-input.md)。
