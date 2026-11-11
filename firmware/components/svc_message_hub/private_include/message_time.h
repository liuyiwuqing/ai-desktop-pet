#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 仅支持消息信封使用的 ISO-8601 子集：YYYY-MM-DDTHH:MM:SS[.sss](Z|±HH:MM)。 */
bool pet_message_parse_iso8601(const char *text, uint32_t *epoch_s);

/* 输出 UTC 形式 YYYY-MM-DDTHH:MM:SSZ；容量不足或时间越界返回 false。 */
bool pet_message_format_iso8601(uint32_t epoch_s, char *out, size_t capacity);
