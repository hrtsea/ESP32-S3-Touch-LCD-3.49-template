#include "synology_client.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_wifi_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "synology_client";

#define SYNO_HTTP_BUF_SIZE 2048

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    char username[32];
    char password[64];
    char sid[64];
    bool use_https;
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    char* http_buf;
} SynologyClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void) {
    return wifi_cfg_is_connected();
}

static bool http_get(SynologyClientData* priv, const char* url, char* buf, int buf_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(http);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(http);
        if (status == 200) {
            int total_read = 0;
            while (total_read < buf_size - 1) {
                int read_len = esp_http_client_read(http, buf + total_read, buf_size - 1 - total_read);
                if (read_len <= 0) break;
                total_read += read_len;
            }
            buf[total_read] = '\0';
            success = true;
        } else {
            ESP_LOGE(TAG, "HTTP error: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(http);
    return success;
}

static bool login(SynologyClientData* priv)
{
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skip login");
        return false;
    }

    char url[512];
    const char* proto = priv->use_https ? "https" : "http";
    snprintf(url, sizeof(url),
        "%s://%s:%d/webapi/auth.cgi?api=SYNO.API.Auth&version=7&method=login&account=%s&passwd=%s&format=sid",
        proto, priv->nas_ip, priv->nas_port, priv->username, priv->password);

    if (!http_get(priv, url, priv->http_buf, SYNO_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Login JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* success_item = cJSON_GetObjectItemCaseSensitive(doc, "success");
    if (cJSON_IsTrue(success_item)) {
        cJSON* data = cJSON_GetObjectItemCaseSensitive(doc, "data");
        cJSON* sid_item = cJSON_GetObjectItemCaseSensitive(data, "sid");
        if (cJSON_IsString(sid_item) && sid_item->valuestring) {
            strncpy(priv->sid, sid_item->valuestring, sizeof(priv->sid) - 1);
            success = true;
            ESP_LOGI(TAG, "Logged in successfully");
        }
    }

    cJSON_Delete(doc);
    if (!success) {
        ESP_LOGE(TAG, "Login failed");
    }
    return success;
}

static bool fetch_system(SynologyClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "http://%s:%d/webapi/entry.cgi?api=SYNO.Core.System.Utilization&version=1&method=get&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, SYNO_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "System JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* success_item = cJSON_GetObjectItemCaseSensitive(doc, "success");
    if (cJSON_IsTrue(success_item)) {
        cJSON* data = cJSON_GetObjectItemCaseSensitive(doc, "data");
        if (cJSON_IsObject(data)) {
            cJSON* cpu = cJSON_GetObjectItemCaseSensitive(data, "cpu");
            cJSON* mem = cJSON_GetObjectItemCaseSensitive(data, "memory");

            if (cJSON_IsObject(cpu)) {
                float user_load = 0, sys_load = 0;
                cJSON* item;
                item = cJSON_GetObjectItemCaseSensitive(cpu, "user_load");
                if (cJSON_IsNumber(item)) user_load = (float)item->valuedouble;
                item = cJSON_GetObjectItemCaseSensitive(cpu, "system_load");
                if (cJSON_IsNumber(item)) sys_load = (float)item->valuedouble;
                priv->data.system.cpu_pct = user_load + sys_load;
            }

            if (cJSON_IsObject(mem)) {
                cJSON* item;
                item = cJSON_GetObjectItemCaseSensitive(mem, "real_usage");
                if (cJSON_IsNumber(item)) priv->data.system.ram_pct = (float)item->valuedouble;
                item = cJSON_GetObjectItemCaseSensitive(mem, "memory_size");
                if (cJSON_IsNumber(item)) priv->data.system.ram_total_mb = (uint32_t)item->valuedouble;
                if (priv->data.system.ram_total_mb > 0) {
                    priv->data.system.ram_used_mb = (uint32_t)(priv->data.system.ram_total_mb * priv->data.system.ram_pct / 100.0f);
                }
            }

            success = true;
        }
    }

    cJSON_Delete(doc);
    return success;
}

static bool fetch_storage(SynologyClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[512];
    snprintf(url, sizeof(url),
        "http://%s:%d/webapi/entry.cgi?api=SYNO.Storage.CGI.Storage&version=1&method=get&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, SYNO_HTTP_BUF_SIZE)) {
        return true;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGW(TAG, "Storage JSON parse error");
        return true;
    }

    cJSON* success_item = cJSON_GetObjectItemCaseSensitive(doc, "success");
    if (cJSON_IsTrue(success_item)) {
        cJSON* data_obj = cJSON_GetObjectItemCaseSensitive(doc, "data");
        cJSON* disks_arr = cJSON_GetObjectItemCaseSensitive(data_obj, "disks");
        if (cJSON_IsArray(disks_arr)) {
            int count = cJSON_GetArraySize(disks_arr);
            if (count > MAX_DISKS) count = MAX_DISKS;
            priv->data.disk_count = (uint8_t)count;
            for (int i = 0; i < count; i++) {
                cJSON* disk = cJSON_GetArrayItem(disks_arr, i);
                if (!cJSON_IsObject(disk)) continue;

                cJSON* item;
                item = cJSON_GetObjectItemCaseSensitive(disk, "name");
                if (cJSON_IsString(item) && item->valuestring)
                    strncpy(priv->data.disks[i].name, item->valuestring, sizeof(priv->data.disks[i].name) - 1);
                item = cJSON_GetObjectItemCaseSensitive(disk, "temp");
                if (cJSON_IsNumber(item))
                    priv->data.disks[i].temp = (int16_t)item->valuedouble;
                const char* health = "normal";
                item = cJSON_GetObjectItemCaseSensitive(disk, "smart_status");
                if (cJSON_IsString(item) && item->valuestring) health = item->valuestring;
                priv->data.disks[i].health = (strcmp(health, "normal") == 0) ? HEALTH_OK : HEALTH_WARNING;
            }
        }
    }

    cJSON_Delete(doc);
    return true;
}

static bool syno_init(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)calloc(1, sizeof(SynologyClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = 5000;
    memcpy(priv->username, g_config.nas_user, sizeof(priv->username));
    priv->username[sizeof(priv->username) - 1] = '\0';
    memcpy(priv->password, g_config.nas_pass, sizeof(priv->password));
    priv->password[sizeof(priv->password) - 1] = '\0';
    priv->use_https = g_config.nas_https;
    priv->sid[0] = '\0';
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;

    priv->http_buf = (char*)malloc(SYNO_HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    memset(&priv->data, 0, sizeof(NasData));
    priv->data.system.temp_cpu = -1;
    priv->data.system.temp_sys = -1;

    ESP_LOGI(TAG, "Init: %s:%d", priv->nas_ip, priv->nas_port);
    return strlen(priv->nas_ip) > 0;
}

static bool syno_connect(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    if (!priv || strlen(priv->nas_ip) == 0) return false;

    ESP_LOGI(TAG, "Connecting...");
    bool success = login(priv);
    if (success) {
        ESP_LOGI(TAG, "Connected successfully");
        priv->data.is_online = true;
        fetch_system(priv);
        fetch_storage(priv);
    } else {
        ESP_LOGW(TAG, "Connection failed, will retry in poll()");
        priv->data.is_online = false;
        priv->consecutive_failures = 1;
    }
    return success;
}

static void syno_disconnect(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    if (!priv) return;
    ESP_LOGI(TAG, "Disconnecting...");
    priv->data.is_online = false;
    priv->sid[0] = '\0';
}

static bool syno_poll(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    if (!priv) return false;

    uint32_t now = get_millis();
    uint32_t poll_interval = g_config.poll_sec * 1000UL;

    if (priv->consecutive_failures > 0) {
        uint8_t capped = priv->consecutive_failures;
        if (capped > 3) capped = 3;
        uint32_t backoff = poll_interval * (1u << capped);
        if (backoff > 60000) backoff = 60000;
        poll_interval = backoff;
    }

    if (priv->last_poll_ms > 0 && (now - priv->last_poll_ms) < poll_interval) {
        return false;
    }

    if (!priv->data.is_online) {
        if (login(priv)) {
            ESP_LOGI(TAG, "Reconnected");
            priv->data.is_online = true;
            priv->consecutive_failures = 0;
        } else {
            priv->consecutive_failures++;
            ESP_LOGW(TAG, "Reconnect failed (failures: %d)", priv->consecutive_failures);
            priv->last_poll_ms = now;
            return false;
        }
    }

    if (!fetch_system(priv)) {
        priv->data.is_online = false;
        priv->consecutive_failures++;
        ESP_LOGW(TAG, "Poll failed (consecutive: %d)", priv->consecutive_failures);
        priv->last_poll_ms = now;
        return false;
    }

    if (priv->consecutive_failures > 0) {
        ESP_LOGI(TAG, "Connection recovered after %d failures", priv->consecutive_failures);
        priv->consecutive_failures = 0;
    }

    fetch_storage(priv);
    priv->data.is_online = true;
    priv->data.last_update_ms = now;
    priv->data.has_update = true;
    priv->last_poll_ms = now;
    return true;
}

static bool syno_is_connected(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    return priv && priv->data.is_online;
}

static const NasData* syno_get_data(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    return priv ? &priv->data : NULL;
}

static const char* syno_get_type_name(DataSource* self)
{
    (void)self;
    return "Synology DSM";
}

static const char* syno_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig syno_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NAS_SYNOLOGY);
}

static void syno_destroy(DataSource* self)
{
    SynologyClientData* priv = (SynologyClientData*)self->priv;
    if (priv) {
        if (priv->http_buf) free(priv->http_buf);
        free(priv);
    }
    free(self);
}

static const DataSourceVTable s_syno_vtable = {
    .init = syno_init,
    .connect = syno_connect,
    .disconnect = syno_disconnect,
    .poll = syno_poll,
    .is_connected = syno_is_connected,
    .get_data = syno_get_data,
    .get_type_name = syno_get_type_name,
    .get_conn_icon = syno_get_conn_icon,
    .get_config = syno_get_config,
    .destroy = syno_destroy,
};

DataSource* synology_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_syno_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}


