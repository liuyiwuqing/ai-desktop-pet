# 长期记忆检索与副本失效（2026-11-12）

本轮在同级 `nexus-core` 补齐显式保存→受限检索→源评论复核→失效/删除队列，以及管理台“记忆”页。按已确认规则，评论撤回、转为非公开或规范化正文变化后，停止返回关联结果，并排队删除可追踪的外部副本。仍为本地软件交付，没有启用真实记忆服务、迁移真实库、执行外部删除或烧录。

此前屏幕/BOOT 恢复与设备更名记录见 [14](14-recovery-and-device-names.md)，本地消息保留与运维步骤见 [13](13-operational-software.md)。固件运行输入未变，本轮不重新生成固件或沿用旧验证声称新的硬件验收。

## 数据与权限边界

- 只处理受信 Halo Connector 取得的公开评论；手工伪造 `source=halo` 不获得公开数据资格。显式保存仍要求分析成功、未超过已启用的本地保留期限，以及当前公开状态/正文哈希一致。
- OpenViking 只给出排序候选；服务端限制用户、actor peer、固定项目目录、标签和资源层级，严格解析目录下的规范 UUID 文件名。其他目录、未知 ID、重复 URI、上游摘要均不能成为返回正文。
- 每条命中重新读取 Halo，校验评论审核/隐藏、所属页面发布/公开状态和规范化纯文本哈希。正文来自本次 Halo 结果，最长 1200 UTF-8 字节；本地仍有效的 AI 摘要最多 300 字节，并标注为推断。
- 确认已保存的副本按源评论状态管理。CORE 原有 7 天正文/30 天消息元数据规则保持有效；原消息清理后，副本账本只保留来源标识、路径、哈希和状态，检索时重新核验正文，不把评论或 AI 摘要存入账本。
- 来源网络错误、认证失败、接入关闭或来源身份不匹配属于“无法核验”：隐藏相关结果并返回 `partial=true`，不当作已经撤回，不据此删除副本。确认不可公开或哈希变化时才进入失效处理。
- 本文列出的管理 HTTP 接口沿用管理员 Bearer 鉴权和 `Cache-Control: no-store`。检索词会发送给配置的 OpenViking，CORE 不新增检索词持久化或正文日志。服务端密钥不进入管理台、设备或云端模型。

## 接口与管理台

```http
POST /api/v1/memory/search
Content-Type: application/json

{"query":"专注提醒","limit":3}
```

`query` 必填、非空白、最长 256 字符；`limit` 可省略，默认 3，范围 1–5。响应为 `items/partial/dataPolicy`，每条包含 `messageId/content/summary/originalUrl/verifiedAt`。`dataPolicy=verified_public_comments_only`；本地摘要已清理时 `summary` 为空。

检索最多取 `min(limit*2,10)` 个候选，每实例并发上限 2。远端查找超时 5 秒、单条源核验最多 2 秒，并使用 12 秒远端请求预算；预算用尽返回已核验的部分结果。此预算不等同于数据库等待或整个 HTTP 调用的硬实时上限，也不保证召回候选范围之外的记录。未配置返回 `409 memory_not_configured`，繁忙返回 `429 memory_search_busy`，上游检索失败返回 502。

`GET /api/v1/memory` 保留 `configured/dataPolicy`，新增 `invalidationPolicy=stop_retrieval_and_delete`、`storedCopies/pendingDeletions/deletedCopies/deletionErrors/identityBlockedCopies`。计数反映账本状态，不保证所有已保存副本当前可检索。

“记忆”页提供检索及删除队列计数，覆盖加载、空结果、部分核验失败、检索失败、服务关闭、重复提交和退出清理。新查询清空旧结果，失败保留输入；结果 60 秒后清空，切换页面/退出会取消请求。评论按纯文本显示，只允许无凭据的 HTTP(S) 来源链接；AI 摘要折叠且明确标注推断。桌面及 390×844 窄屏已查看。

## 失效与持久化删除

新增 Flyway `V10__memory_copy_lifecycle.sql`：`memory_copies` 不依赖原消息的外键，包含 `writing/stored/delete_pending/deleting/deleted` 状态，导入既有分析中仍可追踪的 `memory_uri`。没有对整个 OpenViking 做扫描或清理。

写入前持久化固定路径、来源和哈希，保留写入令牌及 2 分钟保护窗口。成功完成还需核对分析 lease 与本地内容资格；本地清理发生在写入过程中时，已保留补偿删除任务，旧结果不能重新创建已删除的分析/消息。写入结果未知或写入者失联时，先处理可能存在的副本，再允许显式重存。

源复核默认每 5 秒执行一次、每轮检查 1 条至少 1 分钟未复核的已保存记录，并领取 1 条删除任务；这是有界后台轮询，不是即时源站事件订阅。检索本身始终重新核验，不依赖轮询时效。

失效与入队在同一事务内完成；删除任务带 lease，过期可重新领取。失败按尝试次数等待 1–60 分钟后重试，保持不可检索，任务不会因本地原消息的清理而丢失。删除只针对当前配置身份的固定单文件 URI，使用非递归请求；副本同时绑定服务地址、account/user/peer 的身份指纹，密钥本身不入指纹以支持同身份轮换。服务或身份变化时暂停检索、保留待处理任务并记错，不用新身份删除旧身份路径。V10 导入的旧记录没有完整身份历史，先隔离并在管理页计数提示；需维护者核对旧实例和身份后再作受控迁移，本轮不自动补造身份。删除成功后，源评论重新公开也不会自动恢复副本；需要用户重新核验/分析并显式保存。

外部接口依据 [OpenViking 检索文档](https://github.com/volcengine/OpenViking/blob/main/docs/en/api/06-retrieval.md)和[文件删除文档](https://github.com/volcengine/OpenViking/blob/main/docs/en/api/03-filesystem.md)，用隔离 HTTP 夹具验证请求/响应契约，未验证当前真实实例的版本与 ACL。删除成功仅确认指定 URI 的请求获服务端成功响应，不声称清除了供应商备份、历史未登记副本、其他 actor 派生记忆，或已被第三方读取的内容。极端网络超时后远端延迟提交仍受上游实现约束，需真实联调检查。

## 升级与验证

升级前按[部署说明](../deployment/README.md)停服务、备份数据库并验证恢复，保留旧 JAR/配置。V10 无自动 down；回滚须使用相容 JAR 与数据库。记忆仍默认关闭；本次未改真实 env。已有 `NEXUS_MEMORY_ENABLED=true` 的环境升级后会按以上规则复核可追踪副本，因此启用前应核对 actor peer 对固定目录的检索、写入、非递归删除权限及队列状态，不更换 account/user/peer 掩盖旧任务。

| 验证 | 结果 | 证据范围 |
| --- | --- | --- |
| CORE 标准 `python3 tools/run_local.py build`（含 Java 21 / Maven verify） | 37 项，35 通过，2 条件跳过，0 失败/错误 | 真实 HTTP/PostgreSQL/MQTT；Halo/模型/OpenViking 为本地 HTTP 夹具 |
| 新增副本生命周期流程 | 8 项通过 | 改文/后台撤回、删除失败与重试、来源及身份边界、清理后检索、在途写入补偿 |
| Vue 类型检查/生产构建 | 通过 | 当前管理台源文件 |
| 固定 ego-browser 回归 | 38 项通过 | 真实 Vue/DOM + HTTP 边界夹具；保留全部原有更名/配置回归 |
| 视觉检查 | 桌面与 390×844 结果页已查看，无横向溢出 | 单页样例，不代表完整移动端或实机验收 |
| 打包 | 当前 JAR 包含 V10；3 个前端文件的集合及内容逐字节匹配，含旧 hash 资源清理 | 见下方产物哈希 |
| 契约/依赖/板级及脚本检查 | 通过 | 10 schemas、1 profile、8 examples、2 extensions；19 组件无环 |

完整检查按条件跳过真实兼容模型调用，以及需单独启用的 Java/C 管理回环；不能把这两项计入本次通过数。新增流程覆盖“首次保存前改文→重新核验/分析→显式保存”，以及来源接入与记忆服务身份变化的隔离。

完整复跑曾暴露测试夹具 `TRUNCATE` 与后台联表查询争用表锁；已改为事务内 DML 清理，最终 8 项生命周期流程及全部检查通过。回滚用例中的 `fixture_write_failure` 是主动注入的失败，断言事务回滚后通过。标准构建入口清理生成的旧静态资源后重新检查、构建与打包；没有修改全局 JDK/Maven 设置。

本轮 JAR：`nexus-core/target/nexus-core-0.1.0-SNAPSHOT.jar`，26,262,013 B，SHA256 `a739293ce309bb256edb42e58ac2f6a3968f80d388df9248bebcdf27e0c6ff81`。固件四预设的源码清单仍匹配 [14](14-recovery-and-device-names.md)记录的 `19d1452e…cc63b`，本轮没有重新构建或烧录。

此交付快照时尚未完成语音工具入口；后续独立 MCP/stdio 软件与接入手册已补于 [16](16-voice-memory-tools.md)。真实 OpenViking/Halo 权限与删除、真实库迁移及真实语音供应商采用仍待验收，没有将管理员令牌或记忆服务密钥交给设备/模型。跨端成长合并、凭据轮换、远程 OTA 管理、资源/语言与更多平台继续保留在后续范围。
