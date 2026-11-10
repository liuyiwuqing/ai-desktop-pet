# config

非敏感配置与契约。`schemas/` 是唯一事实源，C 头与 TypeScript 类型由它生成。

## 目录

- `schemas/`：`device-config`、`device-profile`、`message`、`message-ack`、`extension` 五个 JSON Schema。
- `profiles/`：板型与功能开关，例如 `dodo-s3-lcd.json`；只引用 `boardId` 与开关，不含引脚与凭据。
- `examples/`：设备配置、消息、回执示例，供文档与 CI 校验使用。

## 生成与校验

```bash
python3 tools/schema_codegen.py         # 生成 C 头与 TS 类型
python3 tools/schema_codegen.py --check # 检查生成物是否过期
python3 tools/ci/check_contracts.py     # schema、profile、example、扩展 manifest 全量校验
```

生成产物（入库，不手改）：

- `firmware/components/contracts/include/pet/config_generated.h`
- `integration/contracts/generated/pet-contracts.ts`

## 规则

- 凭据、Wi-Fi 密码、设备令牌、API Key 不进入本目录，也不进入 Git。
- 运行期用户配置存 NVS `pet_cfg`；服务端私有配置存 `nexus-core/data/local.env`。
- 所有 schema 带 `schemaVersion`；破坏性变更必须新增版本与迁移说明。
- 配置读取失败要可见、可回退，不静默使用默认值覆盖用户设置。
