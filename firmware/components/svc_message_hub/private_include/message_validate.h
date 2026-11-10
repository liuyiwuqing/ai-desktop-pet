#pragma once

#include <stdint.h>

#include "pet/message.h"

/* 信封字段校验；template 不在支持列表时由调用方回退 text_card，不算错误。 */
pet_message_error_t pet_message_validate(const pet_message_view_t *message, uint32_t now_epoch_s);
