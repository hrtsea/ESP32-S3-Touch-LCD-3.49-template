#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the HTTP server. Called after Wi-Fi STA gets an IP. Idempotent. */
esp_err_t webui_start(void);

/* Stop the server and free its resources. Safe to call when not running. */
void      webui_stop(void);

/* The main app calls this once it has a usable framebuffer pointer so
   /screen.bmp can sample it without reaching into LVGL internals. The
   framebuffer is RGB565 with the LV_COLOR_16_SWAP byte order this
   project uses. */
void      webui_set_framebuffer(const void *fb, int w, int h);

#ifdef __cplusplus
}
#endif
