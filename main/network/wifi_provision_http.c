#include "wifi_provision_http.h"
#include "wifi_provision.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include "freertos/timers.h"
#include "freertos/task.h"
#include "webui.h"

static const char *TAG = "wifi_provision_http";

static httpd_handle_t s_server = NULL;

static char s_pending_ssid[33] = {0};
static char s_pending_pass[65] = {0};

static void s_connect_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "async connect: ssid=%s", s_pending_ssid);
    wifi_provision_on_config(s_pending_ssid, s_pending_pass[0] ? s_pending_pass : NULL);
    vTaskDelete(NULL);
}

static const char *s_html_page =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>NAS Monitor WiFi Setup</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1a1a2e;color:#fff;margin:0;padding:20px;}"
    ".container{max-width:400px;margin:0 auto;}"
    "h1{text-align:center;color:#40E0D0;margin-bottom:30px;}"
    ".form-group{margin-bottom:20px;}"
    "label{display:block;margin-bottom:8px;font-size:14px;}"
    "input{width:100%;padding:12px;border:none;border-radius:4px;background:#2d2d44;color:#fff;font-size:16px;box-sizing:border-box;}"
    "input:focus{outline:none;border:2px solid #40E0D0;}"
    "button{width:100%;padding:14px;background:#40E0D0;color:#000;border:none;border-radius:4px;font-size:16px;font-weight:bold;cursor:pointer;}"
    "button:active{background:#30c0b0;}"
    ".status{margin-top:20px;padding:12px;border-radius:4px;text-align:center;font-size:14px;}"
    ".success{background:#2ecc71;}"
    ".error{background:#e74c3c;}"
    "</style>"
    "</head><body>"
    "<div class=\"container\">"
    "<h1>WiFi Setup</h1>"
    "<form id=\"wifi-form\">"
    "<div class=\"form-group\">"
    "<label for=\"ssid\">WiFi SSID</label>"
    "<input type=\"text\" id=\"ssid\" name=\"ssid\" required placeholder=\"Enter WiFi name\">"
    "</div>"
    "<div class=\"form-group\">"
    "<label for=\"password\">Password (optional)</label>"
    "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Enter password\">"
    "</div>"
    "<button type=\"submit\">Connect</button>"
    "</form>"
    "<div id=\"status\" class=\"status\" style=\"display:none;\"></div>"
    "</div>"
    "<script>"
    "document.getElementById('wifi-form').addEventListener('submit',function(e){"
    "e.preventDefault();"
    "var ssid=document.getElementById('ssid').value;"
    "var password=document.getElementById('password').value;"
    "var statusEl=document.getElementById('status');"
    "statusEl.style.display='block';"
    "statusEl.className='status success';"
    "statusEl.textContent='Connecting to '+ssid+'...';"
    "fetch('/api/wifi-config',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:ssid,password:password})"
    "}).then(function(res){"
    "return res.json();"
    "}).then(function(data){"
    "if(data.success){"
    "statusEl.textContent='Connected! Device will restart...';"
    "}else{"
    "statusEl.className='status error';"
    "statusEl.textContent='Error: '+data.message;"
    "}"
    "}).catch(function(err){"
    "statusEl.className='status error';"
    "statusEl.textContent='Connection error';"
    "});"
    "});"
    "</script>"
    "</body></html>";

static esp_err_t s_http_handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_html_page, strlen(s_html_page));
    return ESP_OK;
}

static esp_err_t s_http_handler_config(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send(req, "{\"success\":false,\"message\":\"Empty request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    buf[len] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    char *ssid_start = strstr(buf, "\"ssid\":\"");
    char *pass_start = strstr(buf, "\"password\":\"");

    if (ssid_start) {
        ssid_start += 8;
        int i = 0;
        while (*ssid_start != '\"' && i < sizeof(ssid) - 1) {
            ssid[i++] = *ssid_start++;
        }
    }

    if (pass_start) {
        pass_start += 12;
        int i = 0;
        while (*pass_start != '\"' && i < sizeof(password) - 1) {
            password[i++] = *pass_start++;
        }
    }

    if (!ssid[0]) {
        httpd_resp_send(req, "{\"success\":false,\"message\":\"SSID is required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
    strncpy(s_pending_pass, password, sizeof(s_pending_pass) - 1);
    s_pending_pass[sizeof(s_pending_pass) - 1] = '\0';

    httpd_resp_send(req, "{\"success\":true,\"message\":\"Connecting...\"}", HTTPD_RESP_USE_STRLEN);

    xTaskCreate(s_connect_task, "prov_connect", 6144, NULL, 5, NULL);

    return ESP_OK;
}

static const httpd_uri_t s_uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = s_http_handler_root,
};

static const httpd_uri_t s_uri_config = {
    .uri = "/api/wifi-config",
    .method = HTTP_POST,
    .handler = s_http_handler_config,
};

void wifi_provision_http_init(void)
{
}

void wifi_provision_http_start(void)
{
    if (s_server) return;

    webui_stop();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_LOGI(TAG, "starting HTTP server on port %d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return;
    }

    httpd_register_uri_handler(s_server, &s_uri_root);
    httpd_register_uri_handler(s_server, &s_uri_config);

    ESP_LOGI(TAG, "HTTP server started");
}

void wifi_provision_http_stop(void)
{
    if (!s_server) return;

    ESP_LOGI(TAG, "stopping HTTP server");
    httpd_stop(s_server);
    s_server = NULL;
}
