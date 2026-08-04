/* sim/stubs/esp_console.h - 控制台 stub（仅 cli.c 使用，已 override） */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    void *func;
    void *argtable;
} esp_console_cmd_t;

static inline int esp_console_register_help_command(void) { return 0; }
static inline int esp_console_cmd_register(const esp_console_cmd_t *cmd) { (void)cmd; return 0; }
static inline int esp_console_init(void *cfg) { (void)cfg; return 0; }

#ifdef __cplusplus
}
#endif
