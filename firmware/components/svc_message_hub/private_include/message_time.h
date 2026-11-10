#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 仅支持消息信封使用的 ISO-8601 子集：YYYY-MM-DDTHH:MM:SS[.sss](Z|±HH:MM)。 */
bool pet_message_parse_iso8601(const char *text, uint32_t *epoch_s);
