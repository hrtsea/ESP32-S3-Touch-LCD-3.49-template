/* sim/stubs/freertos/FreeRTOS.h - FreeRTOS 类型 stub */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef uint32_t TickType_t;
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;

#define portMAX_DELAY    0xFFFFFFFFU
#define pdTRUE           1
#define pdFALSE          0
#define pdPASS           1
#define pdFAIL           0

#define pdMS_TO_TICKS(ms)  ((TickType_t)((ms) / portTICK_PERIOD_MS))
#define portTICK_PERIOD_MS  1

#define tskIDLE_PRIORITY   0
#define configMAX_PRIORITIES 25
#define portPRIVILEGE_BIT   0

typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TimerHandle_t;
typedef void *EventGroupHandle_t;

typedef TickType_t (*TaskHookFunction_t)(void *);

static inline void *pvPortMalloc(size_t sz) { return malloc(sz); }
static inline void vPortFree(void *p) { free(p); }
static inline void *pvPortCalloc(size_t n, size_t sz) { return calloc(n, sz); }
static inline void *pvPortRealloc(void *p, size_t sz) { return realloc(p, sz); }
