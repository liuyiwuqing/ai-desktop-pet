# 冻结的历史快照

两个二进制均在 S5-F 改动前，用 S5-E 的真实编码器生成，之后不随新编码器重新生成。它们验证读取旧格式的能力，避免只测试新编码器与新解码器互相一致。

- `message-snapshot-v1.bin`：PMS1；一条 `msg_legacy1`，标题“旧版消息”，创建时间 1794362400，TTL 一天。
- `message-snapshot-v2.bin`：PMS2；同一收件箱，以及 `msg_oldread` 的 read 回执，时间 1794362401。

`test_message_store` 验证迁移与 v3 再编码；`test_message_service legacy-migration` 验证真实 store/service 恢复、旧消息本地已读、新投递接入，以及旧回执留档不误发。

`growth-v1.h`：按 v1 小端布局独立构造的 80 B 成长存档，固定初始数值、时间和 CRC；用于验证后续版本仍可读取，不使用当前编码器生成。

`growth-v2.h`：G5 改动前使用 G3/G4 的真实 CLI 与 v2 编码器生成的 84 B 成长存档和 16 B 提交记录；Unix 1800000020、EXP 74、金币 33、三项每日任务均已领取。用于验证 v3 读取保留已有数值/任务，且不推断历史成就；之后不随当前编码器重新生成。

同文件的 `growth_v2_milestones` 按 v2 磁盘契约从冻结旧档独立构造等级 5、亲密 20 的样本并重新计算 CRC；它是迁移门槛测试数据，不是实机或历史业务记录。
