# 26 · 本地上手

先在电脑上理解栖团的成长、语音状态和界面，再进入 ESP32 与服务端联调。以下命令均在仓库根目录执行；模拟与主机检查无需开发板、云端凭据或真实消息。

## 1. 体验成长

需要支持 C17 与 POSIX 线程的 C 编译器；macOS 可使用 Xcode Command Line Tools，Linux 可使用 GCC 或 Clang。

```sh
bash tools/simulate_growth.sh
```

输入：

```text
status
feed
claim-feed
pet
play
achievements
sleep
wait 3600
wake
quit
```

模拟器复用真实成长服务；`wait` 推进模拟时间，`climate 31 90` 开始模拟温湿度输入。默认存档位于 `firmware/build-growth-sim/state/`，下次运行会继续使用；需要独立体验时传入新的 `--state-dir`，例如：

```sh
bash tools/simulate_growth.sh --state-dir "$PWD/build/try-qitomo/growth"
```

该存档验证桌面进程恢复，不代表物理 NVS 掉电可靠性。完整规则见[成长模块](04-growth-game.md)。

## 2. 演练语音状态

```sh
bash tools/simulate_voice.sh
```

依次输入以下命令，观察 `listening`、`thinking`、`speaking` 等状态与动作输出：

```text
wake
voice 600
silence 400
tts
done
mute
unmute
status
quit
```

`voice` / `silence` 为合成 PCM，`wake` / `tts` / `done` 为注入事件。此入口复用会话状态机，既不录音，也不访问语音供应商。真实语音启用与验收见[语音模块](01-ai-agent.md)及[云端联调](12-cloud-interop.md)。

## 3. 查看真实 LVGL 动画

需要 C/C++ 编译器、CMake、Python 3 和已准备好的锁定 LVGL 源码。检查入口使用 `firmware/managed_components/lvgl__lvgl`；若该目录不存在，先按[固件构建说明](../firmware/README.md)准备 ESP-IDF 与托管依赖。

```sh
bash tools/ci/test_ui_host.sh
python3 tools/export_pet_preview.py
```

在浏览器中打开生成的 `firmware/build-ui-preview/pet-animation.html`，可切换八种成长状态和四种环境表现，并暂停动画。页面使用固件同一套 LVGL 图元的主机输出；桌面刷新效果不代表实屏帧率。

品牌 Logo 的预览与导出另见[品牌资产](assets/brand/README.md)，其资源不参与固件链接。

## 4. 进入开发验证

需要 Python 3；主机脚本另需 C 编译器。按本次修改选择对应检查，常用入口为：

```sh
python3 tools/ci/check_contracts.py
python3 tools/ci/check_deps.py
python3 tools/board_codegen.py --check
bash tools/ci/test_host.sh
```

目标固件使用 ESP-IDF 6.1；完整构建方式、模块开关与引脚配置见[固件说明](../firmware/README.md)。服务端 Java/MQTT 契约、真实隔离数据库或浏览器检查有各自依赖，见[集成说明](../integration/README.md)、[工具目录](../tools/README.md)和[部署说明](../deployment/README.md)。

当前没有目标服务器，`04` 尚未烧录。真实服务凭据、生产迁移、设备接线、OTA 与整机稳定性按[当前任务台账](18-requirements-and-progress.md)和[产品验收标准](07-roadmap-acceptance.md)另行验收。
