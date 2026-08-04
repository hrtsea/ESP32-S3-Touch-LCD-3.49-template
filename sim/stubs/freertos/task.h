/* sim/stubs/freertos/task.h - FreeRTOS 任务 stub */
#pragma once
#include "FreeRTOS.h"

#define tskNO_AFFINITY   0x7FFFFFFF

static inline BaseType_t xTaskCreatePinnedToCore(TaskHookFunction_t pxTaskCode, const char *name,
    uint32_t stack, void *param, UBaseType_t prio, TaskHandle_t *handle, BaseType_t core) {
    (void)pxTaskCode; (void)name; (void)stack; (void)param; (void)prio; (void)core;
    if (handle) *handle = (TaskHandle_t)1;
    return pdFAIL;
}

static inline BaseType_t xTaskCreate(TaskHookFunction_t pxTaskCode, const char *name,
    uint32_t stack, void *param, UBaseType_t prio, TaskHandle_t *handle) {
    (void)pxTaskCode; (void)name; (void)stack; (void)param; (void)prio;
    if (handle) *handle = (TaskHandle_t)1;
    return pdFAIL;
}

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline void vTaskDelayUntil(TickType_t *prev, TickType_t inc) { (void)prev; (void)inc; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }
static inline TickType_t xTaskGetTickCount(void) { return 0; }
static inline TickType_t xTaskGetTickCountFromISR(void) { return 0; }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
static inline char *pcTaskGetName(TaskHandle_t h) { (void)h; return (char *)"sim"; }
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t h) { (void)h; return 1024; }
static inline void vTaskSuspendAll(void) {}
static inline BaseType_t xTaskResumeAll(void) { return pdTRUE; }
