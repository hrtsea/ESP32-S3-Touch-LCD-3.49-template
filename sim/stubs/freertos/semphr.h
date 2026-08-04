/* sim/stubs/freertos/semphr.h - FreeRTOS 信号量 stub */
#pragma once
#include "FreeRTOS.h"

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t init) { (void)max; (void)init; return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t to) { (void)h; (void)to; return pdTRUE; }
static inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t to) { (void)h; (void)to; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t h) { (void)h; return pdTRUE; }
static inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) { (void)h; return pdTRUE; }
static inline void vSemaphoreDelete(SemaphoreHandle_t h) { (void)h; }
static inline UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t h) { (void)h; return 1; }
