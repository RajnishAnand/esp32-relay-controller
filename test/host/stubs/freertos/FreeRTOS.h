#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms) / 10)
