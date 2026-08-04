/* sim/stubs/esp_http_server.h - HTTP 服务器 stub */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明 HTTP 服务器句柄（esp_wifi_config.h 使用 httpd_handle_t） */
typedef struct httpd_req_t *httpd_req_t_handle_t;
typedef void *httpd_handle_t;
typedef struct httpd_req_s {
    void *aux;
} httpd_req_t;

typedef struct {
    const char *uri;
    int method;
    void (*handler)(httpd_req_t *r);
    void *user_ctx;
} httpd_uri_t;

typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_DELETE,
    HTTP_PUT,
} httpd_method_t;

/* stub: 无 HTTP 服务器 */
static inline int httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri) {
    (void)h; (void)uri; return -1;
}
static inline int httpd_query_key_value(const char *buf, const char *key, char *val, size_t len) {
    (void)buf; (void)key; (void)val; (void)len; return -1;
}
static inline int httpd_req_get_url_query_len(httpd_req_t *r) { (void)r; return 0; }
static inline int httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len) { (void)r; (void)buf; (void)len; return -1; }
static inline int httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len) { (void)r; (void)buf; (void)buf_len; return 0; }
static inline int httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len) { (void)r; (void)buf; (void)buf_len; return 0; }
static inline int httpd_resp_set_status(httpd_req_t *r, const char *status) { (void)r; (void)status; return 0; }
static inline int httpd_resp_set_type(httpd_req_t *r, const char *type) { (void)r; (void)type; return 0; }
static inline int httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value) { (void)r; (void)field; (void)value; return 0; }

#ifdef __cplusplus
}
#endif
