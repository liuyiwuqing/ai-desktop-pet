#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
typedef void *QueueSetHandle_t;
typedef void *QueueSetMemberHandle_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *value, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *value, TickType_t timeout);
QueueSetHandle_t xQueueCreateSet(unsigned length);
int xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set);
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set, TickType_t timeout);
