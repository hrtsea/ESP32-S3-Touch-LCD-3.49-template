#include "api_client.h"
#include "config.h"
#include "nas_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "api_client";

#define HTTP_BUF_SIZE 2048

typedef struct {
    NasType current_type;
    char nas_ip[40];
    uint16_t nas_port;
    ApiState state;
    uint32_t poll_interval_ms;
    esp_http_client_handle_t http;
    char* http_buf;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    NasData data;
} ApiClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    return err == ESP_OK;
}

static void clear_data(NasData* data)
{
    memset(data, 0, sizeof(NasData));
    data->system.temp_cpu = -1;
    data->system.temp_sys = -1;
    for (int i = 0; i < MAX_DISKS; i++) data->disks[i].temp = -1;
}

static bool parse_json(ApiClientData* priv, const char* json)
{
    cJSON* doc = cJSON_Parse(json);
    if (doc == NULL) {
        const char* err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error: %s", err_ptr ? err_ptr : "unknown");
        return false;
    }

    cJSON* sys = cJSON_GetObjectItemCaseSensitive(doc, "system");
    if (cJSON_IsObject(sys)) {
        cJSON* item;
        item = cJSON_GetObjectItemCaseSensitive(sys, "hostname");
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(priv->data.system.hostname, item->valuestring, sizeof(priv->data.system.hostname) - 1);
        } else {
            strcpy(priv->data.system.hostname, "unknown");
        }
        item = cJSON_GetObjectItemCaseSensitive(sys, "model");
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(priv->data.system.model, item->valuestring, sizeof(priv->data.system.model) - 1);
        } else {
            strcpy(priv->data.system.model, "Custom");
        }
        item = cJSON_GetObjectItemCaseSensitive(sys, "uptime_s");
        priv->data.system.uptime_s = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "cpu_pct");
        priv->data.system.cpu_pct = cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
        item = cJSON_GetObjectItemCaseSensitive(sys, "ram_pct");
        priv->data.system.ram_pct = cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
        item = cJSON_GetObjectItemCaseSensitive(sys, "ram_total_mb");
        priv->data.system.ram_total_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "ram_used_mb");
        priv->data.system.ram_used_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "ram_free_mb");
        priv->data.system.ram_free_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "ram_cached_mb");
        priv->data.system.ram_cached_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "swap_total_mb");
        priv->data.system.swap_total_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "swap_used_mb");
        priv->data.system.swap_used_mb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(sys, "temp_cpu");
        priv->data.system.temp_cpu = cJSON_IsNumber(item) ? (int16_t)item->valuedouble : -1;
        item = cJSON_GetObjectItemCaseSensitive(sys, "temp_sys");
        priv->data.system.temp_sys = cJSON_IsNumber(item) ? (int16_t)item->valuedouble : -1;

        cJSON* cores = cJSON_GetObjectItemCaseSensitive(sys, "cpu_cores");
        if (cJSON_IsArray(cores)) {
            int core_count = cJSON_GetArraySize(cores);
            if (core_count > MAX_CPU_CORES) core_count = MAX_CPU_CORES;
            priv->data.system.cpu_core_count = (uint8_t)core_count;
            for (uint8_t i = 0; i < core_count; i++) {
                cJSON* core = cJSON_GetArrayItem(cores, i);
                priv->data.system.cpu_cores[i] = cJSON_IsNumber(core) ? (float)core->valuedouble : 0.0f;
            }
        }

        cJSON* load = cJSON_GetObjectItemCaseSensitive(sys, "load_avg");
        if (cJSON_IsArray(load)) {
            int load_count = cJSON_GetArraySize(load);
            for (uint8_t i = 0; i < 3 && i < load_count; i++) {
                cJSON* ld = cJSON_GetArrayItem(load, i);
                priv->data.system.load_avg[i] = cJSON_IsNumber(ld) ? (float)ld->valuedouble : 0.0f;
            }
        }
    }

    priv->data.disk_count = 0;
    cJSON* disks = cJSON_GetObjectItemCaseSensitive(doc, "disks");
    if (cJSON_IsArray(disks)) {
        int disk_count = cJSON_GetArraySize(disks);
        if (disk_count > MAX_DISKS) disk_count = MAX_DISKS;
        for (int i = 0; i < disk_count; i++) {
            cJSON* d = cJSON_GetArrayItem(disks, i);
            if (!cJSON_IsObject(d)) continue;
            cJSON* item;

            item = cJSON_GetObjectItemCaseSensitive(d, "name");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.disks[i].name, item->valuestring, sizeof(priv->data.disks[i].name) - 1);
            item = cJSON_GetObjectItemCaseSensitive(d, "device");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.disks[i].device, item->valuestring, sizeof(priv->data.disks[i].device) - 1);
            item = cJSON_GetObjectItemCaseSensitive(d, "model");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.disks[i].model_name, item->valuestring, sizeof(priv->data.disks[i].model_name) - 1);
            item = cJSON_GetObjectItemCaseSensitive(d, "mount");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.disks[i].mount, item->valuestring, sizeof(priv->data.disks[i].mount) - 1);
            item = cJSON_GetObjectItemCaseSensitive(d, "disk_type");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.disks[i].disk_type, item->valuestring, sizeof(priv->data.disks[i].disk_type) - 1);
            else
                strcpy(priv->data.disks[i].disk_type, "HDD");

            item = cJSON_GetObjectItemCaseSensitive(d, "temp");
            priv->data.disks[i].temp = cJSON_IsNumber(item) ? (int16_t)item->valuedouble : -1;
            item = cJSON_GetObjectItemCaseSensitive(d, "size_gb");
            priv->data.disks[i].size_gb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
            item = cJSON_GetObjectItemCaseSensitive(d, "used_gb");
            priv->data.disks[i].used_gb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
            item = cJSON_GetObjectItemCaseSensitive(d, "used_pct");
            priv->data.disks[i].used_pct = cJSON_IsNumber(item) ? (uint8_t)item->valuedouble : 0;

            const char* health = "OK";
            item = cJSON_GetObjectItemCaseSensitive(d, "health");
            if (cJSON_IsString(item) && item->valuestring) health = item->valuestring;
            if (strcmp(health, "OK") == 0) priv->data.disks[i].health = HEALTH_OK;
            else if (strcmp(health, "WARNING") == 0) priv->data.disks[i].health = HEALTH_WARNING;
            else priv->data.disks[i].health = HEALTH_CRITICAL;

            priv->data.disk_count++;
        }
    }

    priv->data.volume_count = 0;
    cJSON* vols = cJSON_GetObjectItemCaseSensitive(doc, "volumes");
    if (cJSON_IsArray(vols)) {
        int vol_count = cJSON_GetArraySize(vols);
        if (vol_count > MAX_VOLUMES) vol_count = MAX_VOLUMES;
        for (int i = 0; i < vol_count; i++) {
            cJSON* v = cJSON_GetArrayItem(vols, i);
            if (!cJSON_IsObject(v)) continue;
            cJSON* item;

            item = cJSON_GetObjectItemCaseSensitive(v, "name");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.volumes[i].name, item->valuestring, sizeof(priv->data.volumes[i].name) - 1);
            item = cJSON_GetObjectItemCaseSensitive(v, "total_gb");
            priv->data.volumes[i].total_gb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
            item = cJSON_GetObjectItemCaseSensitive(v, "used_gb");
            priv->data.volumes[i].used_gb = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
            item = cJSON_GetObjectItemCaseSensitive(v, "used_pct");
            priv->data.volumes[i].used_pct = cJSON_IsNumber(item) ? (uint8_t)item->valuedouble : 0;
            item = cJSON_GetObjectItemCaseSensitive(v, "raid");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.volumes[i].raid, item->valuestring, sizeof(priv->data.volumes[i].raid) - 1);
            else
                strcpy(priv->data.volumes[i].raid, "none");
            item = cJSON_GetObjectItemCaseSensitive(v, "status");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.volumes[i].status, item->valuestring, sizeof(priv->data.volumes[i].status) - 1);
            else
                strcpy(priv->data.volumes[i].status, "normal");

            priv->data.volume_count++;
        }
    }

    priv->data.service_count = 0;
    cJSON* svcs = cJSON_GetObjectItemCaseSensitive(doc, "services");
    if (cJSON_IsArray(svcs)) {
        int svc_count = cJSON_GetArraySize(svcs);
        if (svc_count > MAX_SERVICES) svc_count = MAX_SERVICES;
        for (int i = 0; i < svc_count; i++) {
            cJSON* s = cJSON_GetArrayItem(svcs, i);
            if (!cJSON_IsObject(s)) continue;
            cJSON* item;

            item = cJSON_GetObjectItemCaseSensitive(s, "name");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(priv->data.services[i].name, item->valuestring, sizeof(priv->data.services[i].name) - 1);
            item = cJSON_GetObjectItemCaseSensitive(s, "running");
            priv->data.services[i].running = cJSON_IsTrue(item);
            item = cJSON_GetObjectItemCaseSensitive(s, "is_docker");
            priv->data.services[i].is_docker = cJSON_IsTrue(item);

            priv->data.service_count++;
        }
    }

    cJSON* net = cJSON_GetObjectItemCaseSensitive(doc, "network");
    if (cJSON_IsObject(net)) {
        cJSON* item;
        item = cJSON_GetObjectItemCaseSensitive(net, "interface");
        if (cJSON_IsString(item) && item->valuestring)
            strncpy(priv->data.network.interface, item->valuestring, sizeof(priv->data.network.interface) - 1);
        else
            strcpy(priv->data.network.interface, "eth0");
        item = cJSON_GetObjectItemCaseSensitive(net, "ip");
        if (cJSON_IsString(item) && item->valuestring)
            strncpy(priv->data.network.ip, item->valuestring, sizeof(priv->data.network.ip) - 1);
        else
            strcpy(priv->data.network.ip, "0.0.0.0");
        item = cJSON_GetObjectItemCaseSensitive(net, "rx_bps");
        priv->data.network.rx_bps = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
        item = cJSON_GetObjectItemCaseSensitive(net, "tx_bps");
        priv->data.network.tx_bps = cJSON_IsNumber(item) ? (uint32_t)item->valuedouble : 0;
    }

    cJSON_Delete(doc);
    priv->state = API_DONE;
    return true;
}

static bool fetch_all(ApiClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/all", priv->nas_ip, priv->nas_port);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    priv->http = esp_http_client_init(&config);
    if (priv->http == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        priv->data.is_online = false;
        return false;
    }

    esp_err_t err = esp_http_client_perform(priv->http);
    bool parsed = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(priv->http);
        if (status == 200) {
            int content_len = esp_http_client_get_content_length(priv->http);
            if (content_len <= 0 || content_len > HTTP_BUF_SIZE - 1) {
                content_len = HTTP_BUF_SIZE - 1;
            }

            int read_len = 0;
            int total_read = 0;
            while (total_read < content_len) {
                read_len = esp_http_client_read(priv->http,
                                                priv->http_buf + total_read,
                                                content_len - total_read);
                if (read_len <= 0) break;
                total_read += read_len;
            }
            priv->http_buf[total_read] = '\0';

            parsed = parse_json(priv, priv->http_buf);
            if (parsed) {
                priv->data.is_online = true;
                priv->data.last_update_ms = get_millis();
                priv->data.has_update = true;
            }
        } else {
            ESP_LOGE(TAG, "HTTP error: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(priv->http);
    priv->http = NULL;

    if (!parsed) {
        priv->data.is_online = false;
    }
    return parsed;
}

static bool api_init(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)calloc(1, sizeof(ApiClientData));
    if (!priv) return false;

    self->priv = priv;
    priv->current_type = NET_LINUX_HTTP;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = DEFAULT_HTTP_PORT;

    priv->poll_interval_ms = g_config.poll_sec * 1000UL;
    priv->state = API_IDLE;
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;

    priv->http_buf = (char*)malloc(HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    clear_data(&priv->data);

    ESP_LOGI(TAG, "Init: %s:%d", priv->nas_ip, priv->nas_port);
    return strlen(priv->nas_ip) > 0;
}

static bool api_connect(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (!priv || strlen(priv->nas_ip) == 0) return false;

    ESP_LOGI(TAG, "Connecting...");
    if (fetch_all(priv)) {
        ESP_LOGI(TAG, "Connected successfully: %s:%d", priv->nas_ip, priv->nas_port);
        priv->data.is_online = true;
        priv->state = API_IDLE;
        return true;
    } else {
        ESP_LOGW(TAG, "Connection failed, will retry in poll()");
        priv->data.is_online = false;
        priv->consecutive_failures = 1;
        priv->state = API_IDLE;
        return false;
    }
}

static void api_disconnect(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (!priv) return;

    ESP_LOGI(TAG, "Disconnecting...");
    if (priv->http) {
        esp_http_client_cleanup(priv->http);
        priv->http = NULL;
    }
    priv->data.is_online = false;
}

static bool api_poll(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (!priv) return false;

    uint32_t now = get_millis();

    uint32_t current_interval = priv->poll_interval_ms;
    if (priv->consecutive_failures > 0) {
        uint8_t capped = priv->consecutive_failures;
        if (capped > 3) capped = 3;
        uint32_t backoff = priv->poll_interval_ms * (1u << capped);
        if (backoff > 60000) backoff = 60000;
        current_interval = backoff;
    }

    if (priv->last_poll_ms > 0 && (now - priv->last_poll_ms) < current_interval) {
        return false;
    }

    if (!priv->data.is_online) {
        if (fetch_all(priv)) {
            ESP_LOGI(TAG, "Reconnected");
            priv->data.is_online = true;
            priv->consecutive_failures = 0;
        } else {
            priv->consecutive_failures++;
            ESP_LOGW(TAG, "Reconnect failed (failures: %d, next retry in %ums)",
                     priv->consecutive_failures, current_interval);
            priv->last_poll_ms = now;
            return false;
        }
    }

    switch (priv->state) {
        case API_IDLE:
            if (fetch_all(priv)) {
                if (priv->consecutive_failures > 0) {
                    ESP_LOGI(TAG, "Connection recovered after %d failures", priv->consecutive_failures);
                    priv->consecutive_failures = 0;
                }
                priv->state = API_REQUESTING;
            } else {
                priv->consecutive_failures++;
                ESP_LOGW(TAG, "Poll failed (consecutive: %d)", priv->consecutive_failures);
            }
            break;

        case API_REQUESTING:
            priv->state = API_DONE;
            break;

        case API_DONE:
            priv->last_poll_ms = now;
            priv->state = API_IDLE;
            return true;

        default:
            priv->state = API_IDLE;
            break;
    }

    return false;
}

static bool api_is_connected(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (!priv) return false;
    return priv->data.is_online && strlen(priv->nas_ip) > 0;
}

static const NasData* api_get_data(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (!priv) return NULL;
    return &priv->data;
}

static const char* api_get_type_name(DataSource* self)
{
    (void)self;
    return "Linux (HTTP)";
}

static const char* api_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig api_get_config(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    NasType type = priv ? priv->current_type : NET_LINUX_HTTP;
    return nas_type_config_get_defaults(type);
}

static void api_destroy(DataSource* self)
{
    ApiClientData* priv = (ApiClientData*)self->priv;
    if (priv) {
        if (priv->http) {
            esp_http_client_cleanup(priv->http);
        }
        if (priv->http_buf) {
            free(priv->http_buf);
        }
        free(priv);
    }
    free(self);
}

static const DataSourceVTable s_api_vtable = {
    .init = api_init,
    .connect = api_connect,
    .disconnect = api_disconnect,
    .poll = api_poll,
    .is_connected = api_is_connected,
    .get_data = api_get_data,
    .get_type_name = api_get_type_name,
    .get_conn_icon = api_get_conn_icon,
    .get_config = api_get_config,
    .destroy = api_destroy,
};

DataSource* api_client_create(NasType type)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_api_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}
