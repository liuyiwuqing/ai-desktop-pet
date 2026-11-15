#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char code[17], uri[256], token[513];
    bool waiting, has_challenge;
} pet_setup_activation_result_t;

/* 只取绑定状态与 WSS 凭据；忽略 firmware 字段，绝不更新本项目固件。 */
esp_err_t pet_setup_activation_parse(const char *json,size_t bytes,pet_setup_activation_result_t *out);
