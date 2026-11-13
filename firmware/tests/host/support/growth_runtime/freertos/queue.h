#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueOverwrite(QueueHandle_t queue, const void *value);
int xQueueReceive(QueueHandle_t queue, void *value, uint32_t timeout);
