# firmware

ESP-IDF 固件工程。当前只有目录骨架，构建文件在架构确认后生成。

## 布局

- `main/app/`：启动、任务编排、事件分发、看门狗。
- `main/core/`：事件总线、模块生命周期、配置、契约。
- `main/modules/`：业务模块；每个模块只暴露 `include/` 契约，不互相直接调用。
- `main/hal/`：I2S、SPI、GPIO、I2C、RMT、Wi-Fi。
- `main/ui/`：LVGL 页面、组件、主题。
- `main/storage/`：NVS、LittleFS、版本化存档。
- `boards/dodo-s3-lcd/`：板级引脚、显示、音频配置。
- `components/`：可复用 ESP-IDF 组件。
- `partitions/`：新分区表与迁移说明，不复用 02/03 的分区表。
- `tests/`：主机测试与目标测试入口。

## 边界

- 不在本目录复制 02/03 的整仓源码；先抽取组件再引用。
- 不手改生成的 `sdkconfig`、`build/`、`managed_components/`。
- 构建与烧录分离；默认只构建，不烧录、不擦除 Flash。
- 详细方案见 [`../docs/00-architecture.md`](../docs/00-architecture.md)。
