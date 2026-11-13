#pragma once

#include "esp_err.h"
#include "svc_ai_agent/xiaozhi.h"

/* 零初始化后仅由一个 codec 拥有者使用；不得与 A4 音频任务共享裸句柄。 */
typedef struct {
    void *encoder, *decoder;
    bool ready, release_failed;
} pet_opus_t;

/* 固定为 16 kHz mono/60 ms 编码、24 kHz mono/60 ms 解码，无 AEC/FEC/DTX。 */
esp_err_t pet_opus_open(pet_opus_t *codec);
pet_xz_codec_t pet_opus_port(pet_opus_t *codec);
/* 清理失败后锁住实例直到重启；不重复释放结果不确定的外部句柄。 */
esp_err_t pet_opus_close(pet_opus_t *codec);
