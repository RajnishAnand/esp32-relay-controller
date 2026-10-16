#pragma once
#include <stdint.h>
#include "esp_ota_ops.h"
typedef void* TaskHandle_t;
esp_err_t esp_task_wdt_init(uint32_t timeout_s, bool panic);
esp_err_t esp_task_wdt_add(TaskHandle_t task);
esp_err_t esp_task_wdt_reset(void);
