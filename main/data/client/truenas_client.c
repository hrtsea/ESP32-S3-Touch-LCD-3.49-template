#include "truenas_client.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_wifi_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "truenas_client";

#define TRUENAS_HTTP_BUF_SIZE 2048

typedef enum {
    TRUENAS_IDLE,
    TRUENAS_FETCH_SYSTEM,
    TRUENAS_FETCH_POOLS,
    TRUENAS_FETCH_DISKS,
    TRUENAS_FETCH_INTERFACES,
    TRUENAS_DONE
} TrueNASState;

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    char api_key[128];
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    TrueNASState state;
    char* http_buf;
} TrueNASClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void) {
    return wifi_cfg_is_connected();
}

static bool http_get_with_auth(TrueNASClientData* priv, const char* url, char* buf, int buf_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", priv->api_key);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    esp_http_client_set_header(http, "Authorization", auth_header);

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

static bool fetch_system(TrueNASClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/v2.0/system/info",
        priv->nas_ip, priv->nas_port);

    if (!http_get_with_auth(priv, url, priv->http_buf, TRUENAS_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "System JSON parse error");
        return false;
    }

    bool success = false;

    cJSON* hostname = cJSON_GetObjectItem(doc, "hostname");
    if (cJSON_IsString(hostname)) {
        memcpy(priv->data.system.hostname, hostname->valuestring, sizeof(priv->data.system.hostname) - 1);
        priv->data.system.hostname[sizeof(priv->data.system.hostname) - 1] = '\0';
    }

    cJSON* uptime = cJSON_GetObjectItem(doc, "uptime_seconds");
    if (cJSON_IsNumber(uptime)) {
        priv->data.system.uptime_s = (uint32_t)uptime->valueint;
    }

    cJSON* loadavg = cJSON_GetObjectItem(doc, "loadavg");
    if (cJSON_IsNumber(loadavg)) {
        priv->data.system.cpu_pct = (float)loadavg->valuedouble;
    }

    uint64_t mem_total = 0, mem_used = 0;
    cJSON* physmem = cJSON_GetObjectItem(doc, "physmem");
    cJSON* buffers = cJSON_GetObjectItem(doc, "buffers");
    cJSON* cached = cJSON_GetObjectItem(doc, "cached");
    if (cJSON_IsNumber(physmem)) mem_total = (uint64_t)physmem->valuedouble;
    if (cJSON_IsNumber(buffers)) mem_used += (uint64_t)buffers->valuedouble;
    if (cJSON_IsNumber(cached)) mem_used += (uint64_t)cached->valuedouble;

    priv->data.system.ram_total_mb = (uint32_t)(mem_total / (1024 * 1024));
    priv->data.system.ram_used_mb = (uint32_t)(mem_used / (1024 * 1024));
    priv->data.system.ram_free_mb = priv->data.system.ram_total_mb - priv->data.system.ram_used_mb;

    if (priv->data.system.ram_total_mb > 0) {
        priv->data.system.ram_pct = (float)priv->data.system.ram_used_mb / priv->data.system.ram_total_mb * 100.0f;
    }

    success = true;
    ESP_LOGI(TAG, "System: %s, Up: %lu, RAM: %d/%d MB",
        priv->data.system.hostname, priv->data.system.uptime_s,
        priv->data.system.ram_used_mb, priv->data.system.ram_total_mb);

    cJSON_Delete(doc);
    return success;
}

static bool fetch_pools(TrueNASClientData* priv)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/v2.0/pool",
        priv->nas_ip, priv->nas_port);

    if (!http_get_with_auth(priv, url, priv->http_buf, TRUENAS_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Pools JSON parse error");
        return false;
    }

    bool success = false;
    priv->data.volume_count = 0;

    if (cJSON_IsArray(doc)) {
        cJSON* pool = NULL;
        cJSON_ArrayForEach(pool, doc) {
            if (priv->data.volume_count >= MAX_VOLUMES) break;

            cJSON* name = cJSON_GetObjectItem(pool, "name");
            if (cJSON_IsString(name)) {
                memcpy(priv->data.volumes[priv->data.volume_count].name, name->valuestring,
                    sizeof(priv->data.volumes[priv->data.volume_count].name) - 1);
                priv->data.volumes[priv->data.volume_count].name[sizeof(priv->data.volumes[priv->data.volume_count].name) - 1] = '\0';
            }

            cJSON* props = cJSON_GetObjectItem(pool, "properties");
            if (cJSON_IsObject(props)) {
                cJSON* allocated = cJSON_GetObjectItem(props, "allocated");
                cJSON* free_val = cJSON_GetObjectItem(props, "free");
                uint64_t alloc_val = 0, free_val_num = 0;
                if (cJSON_IsObject(allocated)) {
                    cJSON* val = cJSON_GetObjectItem(allocated, "value");
                    if (cJSON_IsNumber(val)) alloc_val = (uint64_t)val->valuedouble;
                }
                if (cJSON_IsObject(free_val)) {
                    cJSON* val = cJSON_GetObjectItem(free_val, "value");
                    if (cJSON_IsNumber(val)) free_val_num = (uint64_t)val->valuedouble;
                }
                uint64_t size = alloc_val + free_val_num;
                priv->data.volumes[priv->data.volume_count].total_gb = (uint32_t)(size / (1024ULL * 1024ULL * 1024ULL));
                priv->data.volumes[priv->data.volume_count].used_gb = (uint32_t)(alloc_val / (1024ULL * 1024ULL * 1024ULL));
                if (priv->data.volumes[priv->data.volume_count].total_gb > 0) {
                    priv->data.volumes[priv->data.volume_count].used_pct =
                        (uint8_t)((uint64_t)priv->data.volumes[priv->data.volume_count].used_gb * 100 / priv->data.volumes[priv->data.volume_count].total_gb);
                }
            }

            cJSON* status = cJSON_GetObjectItem(pool, "status");
            if (cJSON_IsString(status)) {
                memcpy(priv->data.volumes[priv->data.volume_count].status, status->valuestring,
                    sizeof(priv->data.volumes[priv->data.volume_count].status) - 1);
                priv->data.volumes[priv->data.volume_count].status[sizeof(priv->data.volumes[priv->data.volume_count].status) - 1] = '\0';
            }

            memcpy(priv->data.volumes[priv->data.volume_count].raid, "zfs", sizeof("zfs"));
            priv->data.volume_count++;
        }
        success = true;
    }

    ESP_LOGI(TAG, "Pools: %d", priv->data.volume_count);
    cJSON_Delete(doc);
    return success;
}

static bool fetch_disks(TrueNASClientData* priv)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/v2.0/disk",
        priv->nas_ip, priv->nas_port);

    if (!http_get_with_auth(priv, url, priv->http_buf, TRUENAS_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Disks JSON parse error");
        return false;
    }

    bool success = false;
    priv->data.disk_count = 0;

    if (cJSON_IsArray(doc)) {
        cJSON* disk = NULL;
        cJSON_ArrayForEach(disk, doc) {
            if (priv->data.disk_count >= MAX_DISKS) break;

            cJSON* name = cJSON_GetObjectItem(disk, "name");
            if (cJSON_IsString(name)) {
                memcpy(priv->data.disks[priv->data.disk_count].name, name->valuestring,
                    sizeof(priv->data.disks[priv->data.disk_count].name) - 1);
                priv->data.disks[priv->data.disk_count].name[sizeof(priv->data.disks[priv->data.disk_count].name) - 1] = '\0';
            }

            cJSON* model = cJSON_GetObjectItem(disk, "model");
            if (cJSON_IsString(model)) {
                memcpy(priv->data.disks[priv->data.disk_count].model_name, model->valuestring,
                    sizeof(priv->data.disks[priv->data.disk_count].model_name) - 1);
                priv->data.disks[priv->data.disk_count].model_name[sizeof(priv->data.disks[priv->data.disk_count].model_name) - 1] = '\0';
            }

            cJSON* size = cJSON_GetObjectItem(disk, "size");
            if (cJSON_IsNumber(size)) {
                priv->data.disks[priv->data.disk_count].size_gb = (uint32_t)((uint64_t)size->valuedouble / (1024ULL * 1024ULL * 1024ULL));
            }

            cJSON* temp = cJSON_GetObjectItem(disk, "temperature");
            if (cJSON_IsNumber(temp) && temp->valueint > 0) {
                priv->data.disks[priv->data.disk_count].temp = (int16_t)temp->valueint;
            }

            priv->data.disks[priv->data.disk_count].health = HEALTH_OK;
            priv->data.disk_count++;
        }
        success = true;
    }

    ESP_LOGI(TAG, "Disks: %d", priv->data.disk_count);
    cJSON_Delete(doc);
    return success;
}

static bool fetch_interfaces(TrueNASClientData* priv)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/v2.0/interface",
        priv->nas_ip, priv->nas_port);

    if (!http_get_with_auth(priv, url, priv->http_buf, TRUENAS_HTTP_BUF_SIZE)) {
        return false;
    }

    cJSON* doc = cJSON_Parse(priv->http_buf);
    if (!doc) {
        ESP_LOGE(TAG, "Interfaces JSON parse error");
        return false;
    }

    bool success = false;

    cJSON* iface = NULL;
    cJSON_ArrayForEach(iface, doc) {
        cJSON* aliases = cJSON_GetObjectItem(iface, "aliases");
        if (!cJSON_IsArray(aliases)) continue;

        cJSON* addr = NULL;
        cJSON_ArrayForEach(addr, aliases) {
            cJSON* type = cJSON_GetObjectItem(addr, "type");
            if (!cJSON_IsString(type) || strcmp(type->valuestring, "INET") != 0) continue;

            cJSON* address = cJSON_GetObjectItem(addr, "address");
            if (cJSON_IsString(address)) {
                memcpy(priv->data.network.ip, address->valuestring, sizeof(priv->data.network.ip) - 1);
                priv->data.network.ip[sizeof(priv->data.network.ip) - 1] = '\0';

                cJSON* iface_name = cJSON_GetObjectItem(iface, "name");
                if (cJSON_IsString(iface_name)) {
                    memcpy(priv->data.network.interface, iface_name->valuestring,
                        sizeof(priv->data.network.interface) - 1);
                    priv->data.network.interface[sizeof(priv->data.network.interface) - 1] = '\0';
                }

                ESP_LOGI(TAG, "Interface: %s (%s)", priv->data.network.interface, priv->data.network.ip);
                success = true;
                break;
            }
        }
        if (success) break;
    }

    if (!success) success = true;

    cJSON_Delete(doc);
    return success;
}

static bool truenas_init(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)calloc(1, sizeof(TrueNASClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = 80;

    memcpy(priv->api_key, g_config.nas_pass, sizeof(priv->api_key));
    priv->api_key[sizeof(priv->api_key) - 1] = '\0';

    priv->state = TRUENAS_IDLE;
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;
    memset(&priv->data, 0, sizeof(priv->data));

    priv->http_buf = (char*)malloc(TRUENAS_HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Init: %s:%d", priv->nas_ip, priv->nas_port);
    return strlen(priv->nas_ip) > 0 && strlen(priv->api_key) > 0;
}

static bool truenas_connect(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)self->priv;
    ESP_LOGI(TAG, "Connecting...");

    if (fetch_system(priv)) {
        ESP_LOGI(TAG, "Connected successfully");
        priv->data.is_online = true;
        priv->state = TRUENAS_FETCH_POOLS;
        return true;
    } else {
        ESP_LOGW(TAG, "Connection failed, will retry in poll()");
        priv->data.is_online = false;
        priv->consecutive_failures = 1;
        priv->state = TRUENAS_IDLE;
        return false;
    }
}

static void truenas_disconnect(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)self->priv;
    if (priv) {
        priv->data.is_online = false;
        priv->state = TRUENAS_IDLE;
    }
    ESP_LOGI(TAG, "Disconnected");
}

static bool truenas_poll(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)self->priv;
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
        if (fetch_system(priv)) {
            ESP_LOGI(TAG, "Reconnected");
            priv->data.is_online = true;
            priv->consecutive_failures = 0;
            priv->state = TRUENAS_FETCH_POOLS;
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
        case TRUENAS_IDLE:
            priv->state = TRUENAS_FETCH_SYSTEM;

        case TRUENAS_FETCH_SYSTEM:
            success = fetch_system(priv);
            if (success) {
                priv->state = TRUENAS_FETCH_POOLS;
            } else {
                priv->data.is_online = false;
                priv->consecutive_failures++;
            }
            break;

        case TRUENAS_FETCH_POOLS:
            success = fetch_pools(priv);
            if (success) {
                priv->state = TRUENAS_FETCH_DISKS;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case TRUENAS_FETCH_DISKS:
            success = fetch_disks(priv);
            if (success) {
                priv->state = TRUENAS_FETCH_INTERFACES;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case TRUENAS_FETCH_INTERFACES:
            success = fetch_interfaces(priv);
            if (success) {
                if (priv->consecutive_failures > 0) {
                    ESP_LOGI(TAG, "Connection recovered after %d failures", priv->consecutive_failures);
                    priv->consecutive_failures = 0;
                }
                priv->data.is_online = true;
                priv->data.last_update_ms = now;
                priv->data.has_update = true;
                priv->state = TRUENAS_DONE;
                ESP_LOGI(TAG, "Poll complete");
            } else {
                priv->consecutive_failures++;
            }
            break;

        case TRUENAS_DONE:
            priv->state = TRUENAS_FETCH_SYSTEM;
            priv->last_poll_ms = now;
            return false;
    }

    priv->last_poll_ms = now;
    return priv->data.has_update;
}

static bool truenas_is_connected(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)self->priv;
    return priv->data.is_online;
}

static const NasData* truenas_get_data(DataSource* self)
{
    TrueNASClientData* priv = (TrueNASClientData*)self->priv;
    return &priv->data;
}

static const char* truenas_get_type_name(DataSource* self)
{
    (void)self;
    return "TrueNAS";
}

static const char* truenas_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig truenas_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NAS_TRUENAS);
}

static void truenas_destroy(DataSource* self)
{
    if (self && self->priv) {
        TrueNASClientData* priv = (TrueNASClientData*)self->priv;
        if (priv->http_buf) {
            free(priv->http_buf);
        }
        free(priv);
        self->priv = NULL;
    }
    if (self) free(self);
}

static const DataSourceVTable s_truenas_vtable = {
    .init = truenas_init,
    .connect = truenas_connect,
    .disconnect = truenas_disconnect,
    .poll = truenas_poll,
    .is_connected = truenas_is_connected,
    .get_data = truenas_get_data,
    .get_type_name = truenas_get_type_name,
    .get_conn_icon = truenas_get_conn_icon,
    .get_config = truenas_get_config,
    .destroy = truenas_destroy,
};

DataSource* truenas_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_truenas_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}


