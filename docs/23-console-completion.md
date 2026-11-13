# 23 · 管理台完成度与验收

更新：2026-11-13。管理台具备八个主导航和核心流程，但 [02 §5](02-message-hub.md#5-在线配置管理面板) 的完整规划尚未全部实现。软件检查、真实服务联调、设备回执和生产验收分别记录，不能用页面存在代替完成。

## CONSOLE-01 · 测试消息断线重试

根因是前端每次点击发送都重新生成 `eventId`。在“后端已保存、HTTP 响应丢失”的浏览器复现中，两次相同正文产生两个 ID、两条消息；后端原有 `connectorId + eventId` 去重正常。

修复将一次逻辑提交的 ID/标题/正文保存在 App 的当前页面内存中；未知结果冻结正文，显式重试同一消息，切换导航保留状态。有效 `{messageId}` 确认后释放；明确 4xx 拒绝允许编辑后以新 ID 提交，408/5xx/网络/无效确认均保留未知状态。退出与 401 清除待确认内容，不自动重放、不存浏览器持久存储。测试消息仍发到全部已启用设备，权限、优先级、API、数据库与投递规则保持。

刷新/关闭/退出后不能恢复页面内存，应先核对消息列表。该修复不能作为跨浏览器幂等、设备已展示或用户已读的保证。接口细节见 [CORE 文档](../../nexus-core/docs/console-messages.md)。

验证入口：

```sh
bash tools/ci/test_message_compose_ui.sh
bash tools/ci/test_management_ui.sh
bash tools/ci/test_remote_ota_ui.sh
bash tools/ci/test_growth_sync_ui.sh
# 同级 CORE：前端类型/构建、隔离 PostgreSQL/MQTT 与 Maven verify/JAR
python3 tools/run_local.py build
```

新增浏览器 33 项、既有管理 38/OTA 22/成长 18 项均已通过，共 111 项。浏览器使用实际编译的 Vue + 回环 HTTP 夹具，不接真实 CORE、数据库、Broker 或设备。CORE 完整 build 另行退出 0：64 条中 59 通过、5 条条件跳过、0 失败/错误，含消息 201/200 同 ID 公开 HTTP/真实数据库回归；类型/前端构建与 Maven verify/JAR 通过。JAR 内 index/JS/CSS 与本轮前端产物哈希一致，无旧 JS 混入。跳过的是三个可选 C peer、真实模型及固定桥接，不计作本轮通过。

旧页面的稳定失败证据与修复后的单消息/同 ID 核对均保留在本地临时日志，生成文件不提交。没有生产部署或真实设备投递结论。

旧 DEL-04 ZIP 仍绑定 CORE `3e38ee5` 的不可变快照，不包含本项前端修复；本项源码与构建 JAR 的配套提交见任务台账，不覆盖历史发布包。
