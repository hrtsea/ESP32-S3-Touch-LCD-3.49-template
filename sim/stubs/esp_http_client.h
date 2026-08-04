/* sim/stubs/esp_http_client.h - HTTP 客户端 stub（仅客户端使用，被 override 跳过） */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_http_client *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int port;
    const char *host;
    const char *path;
    const char *username;
    const char *password;
    int timeout_ms;
    bool disable_auto_redirect;
} esp_http_client_config_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
} esp_http_client_method_t;

#ifdef __cplusplus
}
#endif
