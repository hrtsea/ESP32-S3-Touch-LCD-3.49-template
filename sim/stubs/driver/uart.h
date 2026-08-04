/* sim/stubs/driver/uart.h - UART stub（仅 serial_client.c 使用，已 override） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_NUM_0 = 0,
    UART_NUM_1,
    UART_NUM_MAX,
} uart_port_t;

typedef enum {
    UART_DATA_5_BITS = 5,
    UART_DATA_6_BITS = 6,
    UART_DATA_7_BITS = 7,
    UART_DATA_8_BITS = 8,
} uart_data_bits_t;

typedef enum {
    UART_STOP_BITS_1 = 1,
    UART_STOP_BITS_2,
} uart_stop_bits_t;

#ifdef __cplusplus
}
#endif
