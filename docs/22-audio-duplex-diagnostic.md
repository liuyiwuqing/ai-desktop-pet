# 22 · 并行音频 HAL 与有限诊断

2026-11-13，SW-07-A。该切片为后续声学验收提供可执行入口，普通 AI 会话继续半双工。没有烧录、实物采集、AEC 校准或播放中声学打断的验收结论。

## 范围与接口

`pet_audio_device_t` 末尾增加可选 `duplex_start(playback_sample_rate)`；原 `capture_start` / `playback_start` 的互斥规则保持。新增入口只允许从两个方向均停止时启动 RX/TX，仍由单一任务串行拥有，不允许跨任务并发调用。未提供该端口的设备可以继续使用旧流程，诊断返回 `ESP_ERR_NOT_SUPPORTED`。

DODO BSP 显式启动 I2S0 的 16 kHz RX 和 I2S1 的 16/24 kHz TX。开始采集前先预填 TX 全 DMA 环静音；若 TX 启动失败，尝试停止已经启动的 RX，调用方无论如何仍负责 `deinit`，清理失败不能记作已关麦。原单方向的部分读写、超时、DMA drain 和资源保留语义不变。

两控制器各有时钟；`duplex_start` 只提供并行 I/O，不提供同步采样或时间戳。不能直接将 `playback_write` 的数据或提交时刻当作已播放的 AEC 参考。

## 一次诊断

`pet_audio_duplex_run` 独占 HAL 生命周期，使用固定栈帧，无录音缓冲、网络、NVS 或转写：

- RX：16 kHz、320 样本/帧，只累计样本量与峰值，处理后清除临时 PCM。
- TX：24 kHz、480 样本/帧，只生成 400 Hz 正弦；基准峰值 10000，再按 0–25% 衰减，首尾 10 ms 淡入淡出。不把麦克风数据回送扬声器。
- 时间：20–1000 ms，20 ms 整数帧。每次 I/O 超时 100 ms，采集阶段有 250 ms 总余量；超出后停止诊断。先停 RX，再最多 250 ms 等 TX drain，避免尾部等待产生未计入的采集。
- 报告：启动是否成功、阶段、录入/提交样本量、峰值、软件经过时间/最大采集调用间隔、DMA drain 与独立清理错误。上述软件间隔不能用于声学对齐或精确采样率漂移结论。
- 中止：帧边界和 drain 前后检查取消，随后停止/释放硬件；清理失败单独保留。短读、短写或超界计数直接失败，不补零冒充完整录放。

默认不开诊断。显式 `CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT=y` 后，选择 `CONFIG_PET_AUDIO_DIAGNOSTIC_DUPLEX=y`；原一秒录完再播放仍是默认诊断模式。并行诊断不允许 fake 音频，且与启动 AI、通知音频拥有者和编译期 DND 互斥。它不占用 BOOT 菜单。

两种启动诊断均要求组合根提供持续有效的 `allowed` 策略：配置读取失败/忙、不可写、持久静音或当前 DND 时拒绝；未对时的定时 DND 按现有保守规则处理。启动前、任务实际运行前、HAL 初始化后及帧边界重新检查，策略变化后取消并清理；不会在取消后自动重开。该诊断固件是维护用途，普通 AI/通知拥有者均不启动，不能作为产品的持续全双工模式。

隔离构建计划及执行：

```sh
python tools/build_matrix.py --profile duplex
# 激活项目 ESP-IDF 6.1 后；仅构建，不烧录。
python tools/build_matrix.py --profile duplex \
  --build-root firmware/build-duplex-matrix --execute --jobs 2
```

`--all` 新增第五个 `duplex` 预设；full 仍是常规 AI/SR/DSP 软件预设。CI 同样分别构建五个预设，full 继续运行主机/协议/DSP/LVGL 检查，交付工具任务另执行公开构建计划/Git 所有权回归。远端结果单独核对并记录到 [任务台账](18-requirements-and-progress.md)。

## 验证与后续

已建立并执行公开 HAL 和诊断服务的失败反馈，随后通过针对性 ASan/UBSan 检查。真实 BSP 源码在主机替代 I2S 边界下检查两个采样率、原互斥行为、双向运行、独立停止、TX 静音预填、启动失败回滚与清理重试；诊断服务检查样本量、输出来源/幅度/淡入淡出、期限、取消、短读写、错误与独立清理失败。没有使用 fake 端口证明真实声学效果。

两种启动模式的公开服务检查还覆盖任务调度前撤销、HAL 初始化期间撤销、帧间撤销、内存/任务创建失败后的释放及重试、一次启动所有权，共 12 个隔离场景。初始化期间撤销的用例先复现仍会启动采集；补充初始化后、启动采集前的复核后通过，且不会提交任何 PCM。最终完整主机 ASan/UBSan 再次退出 0。

本地验证结果：

| 入口 | 结果与边界 |
| --- | --- |
| `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash tools/ci/test_host.sh` | 退出 0；含真实 BSP/诊断服务与故障/隐私检查，I2S 为主机边界 |
| `test_sr_frontend_host.sh`、`test_ai_protocol_host.sh`、`test_ai_wss_host.sh` | 均退出 0；保持原 DSP/协议/WSS 行为，不是声学验收 |
| `bash tools/ci/test_ui_host.sh` | 实际 LVGL 10 项通过，无设备屏幕结论 |
| `python3 tools/ci/test_build_matrix.py` | 4 项通过，含 duplex 计划与真实 Git 所有权兼容 |
| 依赖/Schema/板型检查 | 依赖 19 项，14 Schema/12 示例/1 配置/2 扩展，板型检查通过 |

冻结最终源码后执行 `python tools/build_matrix.py --all --build-root firmware/build-duplex-matrix --ota-public-key firmware/build-remote-ota-evidence/test-public.der --execute --jobs 2`，五种预设均退出 0。full 使用既有测试公钥。每种预设的 369 份源码清单均与当前输入一致，6 个产物及 sdkconfig 的哈希核对通过：

| 预设 | `nexus_pet.bin` 字节 |
| --- | ---: |
| default | 2,885,120 |
| full | 3,622,496 |
| off | 225,760 |
| tones | 2,925,984 |
| duplex | 2,909,776 |

证据保留在忽略目录 `firmware/build-duplex-matrix/<profile>/build-evidence.json`，`flashExecuted=false`、`signedRelease=false`。default 仍不启动 AI/诊断，full 不开诊断，tones 仍只开通知音频；duplex 独占音频且关闭普通 AI/通知拥有者。此前不含初始化后复核的构建不作为最终产物证据。

实现 `9376eda` 已推送，后续管理台配套提交 `82edd36` 对应的 [CI 43389006975](https://github.com/liuyiwuqing/ai-desktop-pet/actions/runs/43389006975) 已核对完成，五个固件预设及交付/桥接共七个 job 全部 success，含 full 的主机/协议/DSP/LVGL。`9376eda` 的先前执行被新提交取消，不计作成功。该结果仍不提供真实 I2S/声学或设备验收结论。

SW-07 的剩余条件：先取得真实 RX/TX 同时运行的 DMA/堆/栈/欠载和静音证据，再设计参考帧的采样率转换、播放时间戳、时钟漂移、延迟校准和丢帧降级，最后接入播放中唤醒/取消状态机。当前 `pet_ai_dsp_reference` 只接受新鲜 16 kHz/320 样本参考，不能用未校准的 24 kHz TTS 强行填入；缺少参考仍回退降噪与半双工。
