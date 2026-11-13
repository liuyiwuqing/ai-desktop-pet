#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *value, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *value, TickType_t timeout);
int xQueueReset(QueueHandle_t queue);
