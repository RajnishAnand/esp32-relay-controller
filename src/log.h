#pragma once

#include <Arduino.h>

#ifndef LOG_ENABLED
#define LOG_ENABLED 1
#endif

#if LOG_ENABLED
#define LOGI(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#endif
