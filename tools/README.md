# tools

构建、契约生成、资源打包、迁移与诊断脚本。

## 已落地

- `schema_codegen.py`：读取 `config/schemas/`，生成 `firmware/.../config_generated.h` 与 `integration/contracts/generated/pet-contracts.ts`；`--check` 用于 CI 检查过期。
- `ci/build_firmware.sh`：依赖图检查 → 契约校验 → 主机测试 → `idf.py build` → `idf.py size`。
- `ci/check_deps.py`：组件依赖图无环。
- `ci/check_contracts.py`：schema、profiles、examples、扩展 manifest、生成产物一致性。
- `ci/test_host.sh`：事件契约、生成配置头、扩展清单的主机测试。

## 计划内容

- 构建变体：板型、语言、功能开关；输出到隔离 build 目录。
- 资源打包：字体子集、图片转 RGB565、动画精灵表、LittleFS 镜像。
- 迁移/备份：NVS 导出、存档校验、分区备份与回读；默认只读，写入需显式参数。
- 诊断：串口日志、堆/栈高水位、I2C/SPI 扫描、传感器读数。

## 规则

- 默认不烧录、不擦除 Flash；危险操作必须显式参数与二次确认。
- 不在脚本中硬编码令牌、Wi-Fi 密码、设备证书。
- 生成物放 `build/` 或临时目录，不入库；契约生成物例外，必须入库并由 CI 检查一致性。
- 生成脚本只依赖 Python 标准库；遇到不支持的 JSON Schema 关键字必须失败，不能静默忽略。
