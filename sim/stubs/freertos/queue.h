/* sim/stubs/freertos/queue.h - FreeRTOS 队列 stub */
#pragma once
#include "FreeRTOS.h"

static inline QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t sz) { (void)len; (void)sz; return NULL; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t to) { (void)q; (void)item; (void)to; return pdFAIL; }
static inline BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *hpw) { (void)q; (void)item; (void)hpw; return pdFAIL; }
static inline BaseType_t xQueueSendToBack(QueueHandle_t q, const void *item, TickType_t to) { (void)q; (void)item; (void)to; return pdFAIL; }
static inline BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t to) { (void)q; (void)item; (void)to; return pdFAIL; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t to) { (void)q; (void)buf; (void)to; return pdFAIL; }
static inline BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *buf, BaseType_t *hpw) { (void)q; (void)buf; (void)hpw; return pdFAIL; }
static inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { (void)q; return 0; }
static inline UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { (void)q; return 0; }
static inline void vQueueDelete(QueueHandle_t q) { (void)q; }
static inline BaseType_t xQueueReset(QueueHandle_t q) { (void)q; return pdPASS; }
static inline QueueHandle_t xQueueCreateMutex(void) { return (QueueHandle_t)1; }
