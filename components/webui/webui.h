#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the HTTP server. Called after Wi-Fi STA gets an IP. Idempotent. */
esp_err_t webui_start(void);

/* Start using an existing httpd handle (shared with esp_wifi_config).
   Registers webui routes on the given server without creating a new one. */
esp_err_t webui_start_with_httpd(httpd_handle_t srv);

/* Stop the server and free its resources. Safe to call when not running. */
void      webui_stop(void);

/* The main app calls this once it has a usable framebuffer pointer so
   /screen.bmp can sample it without reaching into LVGL internals. The
   framebuffer is RGB565 with the LV_COLOR_16_SWAP byte order this
   project uses. */
void      webui_set_framebuffer(const void *fb, int w, int h);

/* Enable/disable HTTP Basic Auth for configuration endpoints.
   Pass NULL username to disable auth (default). */
void      webui_set_auth(const char *username, const char *password);

/* Check if a request has valid Basic Auth credentials.
   Returns true if auth is disabled or credentials are valid. */
bool      webui_check_auth(httpd_req_t *req);

/* Pre-request hook compatible with esp_wifi_config's wifi_cfg_http_hook_t.
   Can be passed directly to wifi_cfg_config_t.http.pre_request_hook. */
esp_err_t webui_auth_pre_request_hook(httpd_req_t *req, void *ctx);

#ifdef __cplusplus
}
#endif
