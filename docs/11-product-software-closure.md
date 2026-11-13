# 产品软件闭环记录

2026-11-12，延续 [设备首版交付](09-software-delivery.md)，本轮四项软件优先工作已完成实现与本地验证。远程管理使用真实 Java HTTP、PostgreSQL、MQTT 和设备 C 配置服务的隔离回环；云端接入完成协议与正式工具注册表回环，尚未连接真实供应商。部署配置仍是待绑定实际环境的模板。

后续运行维护实现与当前构建见 [13](13-operational-software.md)；本页构建哈希、测试数和体积保留本轮时点，不能用于识别后续镜像。

| 顺序 | 已交付范围 | 本次证据 |
| --- | --- | --- |
| 1 | CORE 配置事务、遥测/确认消费、管理台、设备主题 ACL、审计与升级工具 | HTTP/PostgreSQL/MQTT/C 回环、鉴权/CAS/失败边界测试、管理台浏览器检查通过 |
| 2 | 小智上下文/工具接入、隐私清除、MCP 诊断计数和云端联调说明 | 34 个 WSS 场景的普通、ASan/UBSan、TSan 检查通过 |
| 3 | Linux CORE 服务、管理台/可选 Mac 控制 TLS 代理、升级与恢复模板 | Caddy 2.10.2 实际配置校验及隔离 HTTP 路由、鉴权转发、体积限制、POST 不重试检查通过 |
| 4 | 文档现状、版本、契约与最终构建核对 | 入口/契约/维护文档同步，四种 ESP-IDF 6.1 固件与 NEXUS 前后端构建通过 |

## 远程管理

`nexus-core` 新增 Flyway V8 与配置请求/审计持久化。每设备最多一个活动请求，expectedRevision 与完整配置绑定；同 ID 同内容幂等，异内容冲突。网络发送前先保存尝试记录，重投使用同一请求与到期时间。已尝试但无法确认的请求保留 unconfirmed，只有有效 ACK 或匹配的新鲜完整设置回读才能收口，不能换 ID 强行覆盖。

遥测与原 state/LWT 分离；retained 遥测不会把设备标为在线。新请求需要 90 秒内的完整可写配置，旧/异常时间或非法 JSON 不覆盖当前状态。管理台保留编辑草稿，版本变化要求重新加载；HTTP 结果未知时缓存原请求供核对/重试，显示待确认、已应用、拒绝和未知结果及审计。亮度 0 的熄屏后果保留提示。

设备本地保存入队前预留 revision，远程配置不能穿插覆盖尚未完成的本地事务；入队失败释放预留。静音/DND 在本地存储失败时仍保持运行期隐私门控，并提示未保存。配置 applied 仅代表本地存储事务完成，不代表物理输出验收。

管理接口沿用共享管理员令牌，尚无多用户/RBAC；devices.enabled 仍只控制新消息投递。ACL 与包限制提供新装配置及默认只读的升级工具，自定义规则拒绝自动覆盖。完整主题、REST、状态与升级规则见 [device-management-v1](../integration/contracts/device-management-v1/README.md)。本次没有升级真实数据库或修改真实 ACL。

## 语音与部署

WSS 回环运行真实 A4、上下文、意图路由与 `app_tools` 注册表：完成播放后跨连接读取同一轮上下文、重复只读调用、拒绝额外参数、清除期间禁止发送旧文本、取消连接后读取空历史。voiceTransport 诊断增加请求/响应/失败计数，仍需结合供应商记录判断具体工具是否调用。协议上限、提示词准备与真实云端验收步骤见 [12](12-cloud-interop.md)。本地通过不证明 ASR/LLM/TTS、云端采用人设/历史或云端历史删除。

[部署目录](../deployment/README.md)提供 Java 21/systemd、私有 env、Caddy TLS、V8/ACL 升级、备份恢复与 OTA 公钥绑定步骤。代理保留鉴权、限定控制路由/请求体且禁用 POST 重试；每路由连接池限制不等于全局速率配额。未安装服务、申请证书或执行生产迁移。ESP32 基础语音直接连接小智，NEXUS 可独立部署在长期在线服务器；Mac 运行依赖仅来自可选的 Mac 控制扩展。

## 验证结果与复现

以下检查已核对退出状态为 0：

- `bash tools/ci/test_host.sh`：完整主机回归，包含本地配置预留、失败释放与隐私选择保护。
- `python3 tools/ci/test_management_contract.py`：真实 Java HTTP/PostgreSQL/MQTT→C 配置服务→ACK；C 的 OS/Flash 驱动在主机托管。该回环也通过 C 端 ASan/UBSan 检查。
- 在 `nexus-core` 执行 `python3 tools/run_local.py build`：前端类型检查、生产构建与 Maven verify 通过；20 项 Java 测试，0 失败/错误、2 跳过。跳过项为显式真实模型调用与另行运行的 Java/C 回环，后者单独通过。
- 管理台实际浏览器检查：空态、新鲜/过期/只读、草稿与版本冲突、提交→设备夹具确认→回读、审计、HTTP 结果未知后沿用原 ID 重试均通过。另以 390 CSS px 检查 DOM 无横向溢出；未据此声称手机实机视觉验收。
- `IDF_PATH=<ESP-IDF-6.1路径> bash tools/ci/test_ai_wss_host.sh`：34 场景通过；分别设置 `PET_HOST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0` 与 `PET_HOST_TSAN=1` 的全场景检查通过。不包含 LeakSanitizer、真实 SDK 网络/Opus/I²S 或 ESP32 SMP。
- `python3 tools/ci/test_deployment.py`：2 项测试通过，Docker/Caddy 2.10.2 实际校验两份 TLS 配置；路由测试使用隔离 HTTP 夹具，不申请证书。
- `python3 tools/ci/check_contracts.py`、`python3 tools/ci/check_deps.py`、`python3 tools/board_codegen.py --check` 与 `git diff --check` 通过；当前 10 个 Schema、1 个 profile、8 个示例、2 个扩展及 19 个无环组件。首版的 LVGL、协议、模型、OTA/签名与其他 sanitizer 结果保留在 [09](09-software-delivery.md)。

最终四种配置均由已配置的独立构建目录执行 `ninja -C firmware/<目录> -j2` 成功，应用分区均为 4 MiB。默认使用 `firmware/sdkconfig`，其他使用各自目录的 sdkconfig；没有切换或覆盖默认配置。

| 配置 | 构建目录 | app 镜像字节数 |
| --- | --- | ---: |
| 当前默认 | `firmware/build` | 2,884,480 |
| 完整功能：ESP-SR/DSP、维护、设备/舵机扩展、OTA | `firmware/build-a8-sr` | 3,591,648 |
| 业务模块关闭，保留物理维护入口 | `firmware/build-software-off` | 225,344 |
| 独立消息提醒音 | `firmware/build-software-tones` | 2,924,752 |

对应 `nexus_pet.bin` SHA-256：

```text
build                47ecf2d87b5f7e89f2fcfe844a9d7f0525ececb2d6a242104b5726664804c729
build-a8-sr           0f1c7b55e96c21aef8312f0ae60411a9c57e81f97a62d236487787228c15a268
build-software-off   0505835a38088debc5679b14b425550c25572a5d5622eed1edb3c9a5c270f259
build-software-tones 6596964e38cdb561e328c4fb668fcf7abe7580cb1c52fa4ec3a832a961f9981c
```

完整配置的 `srmodels/srmodels.bin` 为 579,215 字节，SHA-256 为 `8f859601e0873438fa57553e648f25db5d64ce3ec2ba966a707e7d27ef8cd5cb`。完整配置采用临时编译测试公钥和 `.invalid` 地址，镜像不是生产发布。

固件版本保持 `0.4.0-dev`，CORE 为 `0.1.0-SNAPSHOT`，未创建 release/tag。最终 `nexus-core-0.1.0-SNAPSHOT.jar` 为 26,232,745 字节，SHA-256 为 `d9762d18c8881ed36d6e6c43c66c7a4d9a5d6f49a2f387f68be2f82ec4a1b44d`；版本号相同不表示与旧产物相同，部署应核对哈希并保留相容备份。

## 后续验收边界

本轮可在现有环境完成的软件实现与验证已收口。真实供应商工具/上下文行为、生产域名/TLS/常驻服务/数据库与 ACL 升级、实际烧录、物理 NVS 掉电、声学效果/AEC、机械校准及 24 h/7 d 运行仍需对应环境验收；当前保持半双工回退。手机/更多平台、旧存档映射、语义摘要等后置能力保持独立范围。

本轮源码、测试和模板修改位于 `04-ai-desktop-pet` 与同级 `nexus-core`。测试使用独立容器和测试凭据，未读取真实运行凭据；测试进程与临时容器已退出。没有执行 Git 提交/推送、真实部署或设备动作。
