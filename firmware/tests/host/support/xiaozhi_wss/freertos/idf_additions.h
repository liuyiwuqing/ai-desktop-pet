#pragma once
#include "freertos/queue.h"
QueueHandle_t xQueueCreateWithCaps(unsigned length, unsigned size, unsigned caps);
void vQueueDeleteWithCaps(QueueHandle_t queue);
