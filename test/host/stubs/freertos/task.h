#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackBytes, void* arg, unsigned priority,
                                   TaskHandle_t* handle, int core);
void vTaskDelete(TaskHandle_t handle);
void vTaskDelay(TickType_t ticks);
