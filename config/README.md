# config

非敏感配置与契约。

## 目录

- `schemas/`：消息信封、回执、设备能力、舵机指令、存档等 JSON Schema。
- `profiles/`：板型与功能开关，例如 `dodo-s3-lcd.json`；不含凭据。
- `examples/`：示例配置，供文档与测试使用。

## 规则

- 凭据、Wi-Fi 密码、设备令牌、API Key 不进入本目录，也不进入 Git。
- 运行期用户配置存 NVS `pet_cfg`；服务端私有配置存 `nexus-core/data/local.env`。
- 所有 schema 带 `schemaVersion`；破坏性变更必须新增版本与迁移说明。
- 配置读取失败要可见、可回退，不静默使用默认值覆盖用户设置。
