# servo（扩展）

语音控制舵机等硬件的扩展包。当前仅规划，未实现。

## 计划内容

- `include/actuator.h`：初始化、角度、速度、停止、急停、状态。
- `include/command_parser.h`：自然语言/JSON 指令解析与校验。
- `include/servo_sequence.h`：关键帧、循环、取消。
- `src/`：LEDC 驱动、序列播放器、安全守卫。
- `README.md`：供电、共地、校准、限位、急停。

## 约束

- 首版 1–2 路 LEDC，独立 5V 供电；不直接从 3.3V 取电。
- 越界角度、超速、超时长一律拒绝；急停 ≤100 ms。
- 详细方案见 [`../../docs/06-extension-servo.md`](../../docs/06-extension-servo.md)。
