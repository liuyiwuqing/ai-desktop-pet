# servo

2026-11-12：纯执行引擎、FreeRTOS 拥有者与 LEDC 接线已实现，默认关闭；没有执行机械动作。

`actuator.h/servo_engine.c` 支持最多两路校准 profile、角度/脉宽/速度约束、最多 8 帧且总长 10 秒的序列、停止/软件急停、请求去重及运动前持久预留。超过 90° 位移或 120°/s 需本地确认；超 100 ms 调度缺口故障撤销 PWM。角度是命令位置，不是编码器反馈。

当前固件服务配置一路，`CONFIG_PET_SERVO_GPIO=-1` 不启用；必须选择未被板型、Flash、USB、按键等占用的 GPIO，并按实物校准。LEDC 使用独立 timer/channel，启动仅配置服务，不输出 PWM。默认限位 30–150°、安全位 90°、最高 90°/s 是软件保守配置，不能当作某型号已校准。

供电/共地、脉宽、机构限位和停止方式确认后，物理串口执行 `configure_device.py servo-arm --calibrated-power`；仍需 `--send --port` 才发送。MCP 无 arm 权限，只提供 status/move/home/wag/nod/stop/emergency。急停撤销 PWM，重新工作需本地 arm；软件急停不等于硬件断电，实际停止时延需上板测试。

`test_host.sh` 运行真实插值/限位/去重/序列/故障逻辑，LEDC/GPIO/电源为边界接口；完整功能构建检查实际驱动链接。没有验证物理转角、扭矩、电源跌落或机械安全。
