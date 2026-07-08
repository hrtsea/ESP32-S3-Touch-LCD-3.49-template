#include "netdata_client.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_wifi_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "netdata_client";

#define NETDATA_HTTP_BUF_SIZE 1024

typedef enum {
    NETDATA_IDLE,
    NETDATA_FETCH_CPU,
    NETDATA_FETCH_RAM,
    NETDATA_FETCH_NETWORK,
    NETDATA_FETCH_STORAGE,
    NETDATA_DONE
} NetdataState;

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    NetdataState state;
    char* http_buf;
} NetdataClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void) {
    return wifi_cfg_is_connected();
}

static bool http_get(NetdataClientData* priv, const char* url, char* buf, int buf_size)
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

static bool fetch_cpu(NetdataClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/api/v1/data?chart=system.cpu&points=1&after=-1&format=json",
        priv->nas_ip, priv->nas_port);

    if (!http_get(priv, url, priv->http_buf, NETDATA_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "CPU JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* data_arr = cJSON_GetObjectItem(doc, "data");
    if (cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
        cJSON* first = cJSON_GetArrayItem(data_arr, 0);
        cJSON* dims = cJSON_GetObjectItem(first, "dimensions");
        if (dims) {
            cJSON* idle = cJSON_GetObjectItem(dims, "idle");
            if (cJSON_IsNumber(idle)) {
                float idle_val = (float)idle->valuedouble;
                priv->data.system.cpu_pct = 100.0f - idle_val;
                ESP_LOGI(TAG, "CPU: %.1f%%", priv->data.system.cpu_pct);
                success = true;
            }
        }
    }

    cJSON_Delete(doc);
    return success;
}

static bool fetch_ram(NetdataClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/api/v1/data?chart=system.ram&points=1&after=-1&format=json",
        priv->nas_ip, priv->nas_port);

    if (!http_get(priv, url, priv->http_buf, NETDATA_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "RAM JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* data_arr = cJSON_GetObjectItem(doc, "data");
    if (cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
        cJSON* first = cJSON_GetArrayItem(data_arr, 0);
        cJSON* dims = cJSON_GetObjectItem(first, "dimensions");
        if (dims) {
            uint64_t used = 0, total = 0, cached = 0;
            cJSON* used_json = cJSON_GetObjectItem(dims, "used");
            cJSON* total_json = cJSON_GetObjectItem(dims, "total");
            cJSON* cached_json = cJSON_GetObjectItem(dims, "cached");
            if (cJSON_IsNumber(used_json)) used = (uint64_t)used_json->valuedouble;
            if (cJSON_IsNumber(total_json)) total = (uint64_t)total_json->valuedouble;
            if (cJSON_IsNumber(cached_json)) cached = (uint64_t)cached_json->valuedouble;

            priv->data.system.ram_used_mb = (uint32_t)(used / (1024 * 1024));
            priv->data.system.ram_total_mb = (uint32_t)(total / (1024 * 1024));
            priv->data.system.ram_cached_mb = (uint32_t)(cached / (1024 * 1024));
            priv->data.system.ram_free_mb = priv->data.system.ram_total_mb - priv->data.system.ram_used_mb;

            if (priv->data.system.ram_total_mb > 0) {
                priv->data.system.ram_pct = (float)priv->data.system.ram_used_mb / priv->data.system.ram_total_mb * 100.0f;
            }

            ESP_LOGI(TAG, "RAM: %d/%d MB (%.1f%%)",
                priv->data.system.ram_used_mb, priv->data.system.ram_total_mb, priv->data.system.ram_pct);
            success = true;
        }
    }

    cJSON_Delete(doc);
    return success;
}

static bool fetch_network(NetdataClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/api/v1/data?chart=system.net&points=1&after=-1&format=json",
        priv->nas_ip, priv->nas_port);

    if (!http_get(priv, url, priv->http_buf, NETDATA_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Network JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* data_arr = cJSON_GetObjectItem(doc, "data");
    if (cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
        cJSON* first = cJSON_GetArrayItem(data_arr, 0);
        cJSON* dims = cJSON_GetObjectItem(first, "dimensions");
        if (cJSON_IsArray(dims) && cJSON_GetArraySize(dims) >= 2) {
            float rx_kbps = (float)cJSON_GetArrayItem(dims, 0)->valuedouble;
            float tx_kbps = (float)cJSON_GetArrayItem(dims, 1)->valuedouble;

            priv->data.network.rx_bps = (uint32_t)(rx_kbps * 1000.0f / 8.0f);
            priv->data.network.tx_bps = (uint32_t)(tx_kbps * 1000.0f / 8.0f);

            memcpy(priv->data.network.interface, "eth0", sizeof("eth0"));
            ESP_LOGI(TAG, "Net: RX=%.1f KB/s, TX=%.1f KB/s",
                priv->data.network.rx_bps / 1024.0f, priv->data.network.tx_bps / 1024.0f);
            success = true;
        }
    }

    cJSON_Delete(doc);
    return success;
}

static bool fetch_storage(NetdataClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/api/v1/data?chart=disk_space._&points=1&after=-1&format=json",
        priv->nas_ip, priv->nas_port);

    if (!http_get(priv, url, priv->http_buf, NETDATA_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Storage JSON parse error");
        return false;
    }

    bool success = false;
    cJSON* data_arr = cJSON_GetObjectItem(doc, "data");
    if (cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
        cJSON* first = cJSON_GetArrayItem(data_arr, 0);
        cJSON* dims = cJSON_GetObjectItem(first, "dimensions");
        if (dims) {
            uint64_t avail = 0, used = 0;
            cJSON* avail_json = cJSON_GetObjectItem(dims, "avail");
            cJSON* used_json = cJSON_GetObjectItem(dims, "used");
            if (cJSON_IsNumber(avail_json)) avail = (uint64_t)avail_json->valuedouble;
            if (cJSON_IsNumber(used_json)) used = (uint64_t)used_json->valuedouble;

            uint64_t total = avail + used;
            if (total > 0) {
                priv->data.volumes[0].total_gb = (uint32_t)(total / (1024ULL * 1024ULL * 1024ULL));
                priv->data.volumes[0].used_gb = (uint32_t)(used / (1024ULL * 1024ULL * 1024ULL));
                priv->data.volumes[0].used_pct = (uint8_t)((used * 100) / total);
                memcpy(priv->data.volumes[0].name, "/", sizeof("/"));
                memcpy(priv->data.volumes[0].raid, "none", sizeof("none"));
                memcpy(priv->data.volumes[0].status, "normal", sizeof("normal"));
                priv->data.volume_count = 1;

                ESP_LOGI(TAG, "Disk: %d/%d GB (%d%%)",
                    priv->data.volumes[0].used_gb, priv->data.volumes[0].total_gb,
                    priv->data.volumes[0].used_pct);
                success = true;
            }
        }
    }

    cJSON_Delete(doc);
    return success;
}

static bool netdata_init(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)calloc(1, sizeof(NetdataClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = 19999;

    priv->state = NETDATA_IDLE;
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;
    memset(&priv->data, 0, sizeof(priv->data));

    priv->http_buf = (char*)malloc(NETDATA_HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Init: %s:%d", priv->nas_ip, priv->nas_port);
    return strlen(priv->nas_ip) > 0;
}

static bool netdata_connect(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)self->priv;
    ESP_LOGI(TAG, "Connecting...");

    if (fetch_cpu(priv)) {
        ESP_LOGI(TAG, "Connected successfully");
        priv->data.is_online = true;
        priv->state = NETDATA_IDLE;
        return true;
    } else {
        ESP_LOGW(TAG, "Connection failed, will retry in poll()");
        priv->data.is_online = false;
        priv->consecutive_failures = 1;
        priv->state = NETDATA_IDLE;
        return false;
    }
}

static void netdata_disconnect(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)self->priv;
    if (priv) {
        priv->data.is_online = false;
        priv->state = NETDATA_IDLE;
    }
    ESP_LOGI(TAG, "Disconnected");
}

static bool netdata_poll(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)self->priv;
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
        if (fetch_cpu(priv)) {
            ESP_LOGI(TAG, "Reconnected");
            priv->data.is_online = true;
            priv->consecutive_failures = 0;
        } else {
            priv->consecutive_failures++;
            ESP_LOGW(TAG, "Reconnect failed (failures: %d, next retry in %ds)",
                priv->consecutive_failures, poll_interval / 1000);
            priv->last_poll_ms = now;
            return false;
        }
    }

    bool success = false;

    switch (priv->state) {
        case NETDATA_IDLE:
            priv->state = NETDATA_FETCH_CPU;

        case NETDATA_FETCH_CPU:
            success = fetch_cpu(priv);
            if (success) {
                priv->state = NETDATA_FETCH_RAM;
            } else {
                priv->data.is_online = false;
                priv->consecutive_failures++;
            }
            break;

        case NETDATA_FETCH_RAM:
            success = fetch_ram(priv);
            if (success) {
                priv->state = NETDATA_FETCH_NETWORK;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case NETDATA_FETCH_NETWORK:
            success = fetch_network(priv);
            if (success) {
                priv->state = NETDATA_FETCH_STORAGE;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case NETDATA_FETCH_STORAGE:
            success = fetch_storage(priv);
            if (success) {
                if (priv->consecutive_failures > 0) {
                    ESP_LOGI(TAG, "Connection recovered after %d failures", priv->consecutive_failures);
                    priv->consecutive_failures = 0;
                }
                priv->data.is_online = true;
                priv->data.last_update_ms = now;
                priv->data.has_update = true;
                priv->state = NETDATA_DONE;
                ESP_LOGI(TAG, "Poll complete");
            } else {
                priv->consecutive_failures++;
            }
            break;

        case NETDATA_DONE:
            priv->state = NETDATA_FETCH_CPU;
            priv->last_poll_ms = now;
            return false;
    }

    priv->last_poll_ms = now;
    return priv->data.has_update;
}

static bool netdata_is_connected(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)self->priv;
    return priv->data.is_online;
}

static const NasData* netdata_get_data(DataSource* self)
{
    NetdataClientData* priv = (NetdataClientData*)self->priv;
    return &priv->data;
}

static const char* netdata_get_type_name(DataSource* self)
{
    (void)self;
    return "Netdata";
}

static const char* netdata_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig netdata_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NET_NETDATA);
}

static void netdata_destroy(DataSource* self)
{
    if (self && self->priv) {
        NetdataClientData* priv = (NetdataClientData*)self->priv;
        if (priv->http_buf) {
            free(priv->http_buf);
        }
        free(priv);
        self->priv = NULL;
    }
    if (self) free(self);
}

static const DataSourceVTable s_netdata_vtable = {
    .init = netdata_init,
    .connect = netdata_connect,
    .disconnect = netdata_disconnect,
    .poll = netdata_poll,
    .is_connected = netdata_is_connected,
    .get_data = netdata_get_data,
    .get_type_name = netdata_get_type_name,
    .get_conn_icon = netdata_get_conn_icon,
    .get_config = netdata_get_config,
    .destroy = netdata_destroy,
};

DataSource* netdata_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_netdata_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}


