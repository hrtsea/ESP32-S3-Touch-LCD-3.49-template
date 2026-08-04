/* sim/stubs/driver/i2c_master.h - I2C 主机 stub */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_NUM_0 = 0,
    I2C_NUM_1,
    I2C_NUM_MAX,
} i2c_port_t;

static inline int i2c_param_config(i2c_port_t port, void *cfg) { (void)port; (void)cfg; return 0; }
static inline int i2c_driver_install(i2c_port_t port, int mode, int slv_rx_buf, int slv_tx_buf, int flags) {
    (void)port; (void)mode; (void)slv_rx_buf; (void)slv_tx_buf; (void)flags; return 0;
}
static inline int i2c_master_write_to_device(i2c_port_t port, uint8_t addr,
                                              const uint8_t *buf, size_t len, int timeout_ms) {
    (void)port; (void)addr; (void)buf; (void)len; (void)timeout_ms; return 0;
}

#ifdef __cplusplus
}
#endif
