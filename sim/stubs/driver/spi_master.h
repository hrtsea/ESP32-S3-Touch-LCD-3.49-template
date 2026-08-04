/* sim/stubs/driver/spi_master.h - SPI 主机 stub（仅 disp_driver.c 引用，已 override） */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI1_HOST = 0,
    SPI2_HOST,
    SPI3_HOST,
    SPI_HOST_MAX,
} spi_host_device_t;

typedef struct spi_device_t *spi_device_handle_t;

typedef struct {
    int command_bits;
    int address_bits;
    int dummy_bits;
    int cs_ena_pretrans;
    int cs_ena_posttrans;
    int clock_speed_hz;
    int mode;
    int spics_io_num;
    int queue_size;
    void *pre_cb;
    void *post_cb;
} spi_device_interface_config_t;

typedef struct {
    void *tx_buffer;
    void *rx_buffer;
    size_t length;
    size_t rxlength;
    int cs;
    void *user;
} spi_transaction_t;

static inline int spi_bus_add_device(int host, const spi_device_interface_config_t *cfg,
                                      spi_device_handle_t *hdl) {
    (void)host; (void)cfg; if (hdl) *hdl = NULL; return 0;
}
static inline int spi_device_transmit(spi_device_handle_t h, spi_transaction_t *t) {
    (void)h; (void)t; return 0;
}

#ifdef __cplusplus
}
#endif
