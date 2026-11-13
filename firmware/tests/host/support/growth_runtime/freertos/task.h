#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *context,
                unsigned priority, TaskHandle_t *task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(uint32_t ticks);
uint32_t ulTaskNotifyTake(int clear, uint32_t timeout);
int xTaskNotifyGive(TaskHandle_t task);
