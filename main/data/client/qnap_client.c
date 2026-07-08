#include "qnap_client.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_wifi_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "qnap_client";

#define QNAP_HTTP_BUF_SIZE 4096
#define QNAP_XML_BUF_SIZE 128

typedef enum {
    QNAP_IDLE,
    QNAP_LOGIN,
    QNAP_FETCH_SYSINFO,
    QNAP_FETCH_STORAGE,
    QNAP_FETCH_DISKS,
    QNAP_FETCH_NETWORK,
    QNAP_DONE
} QnapState;

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    char username[32];
    char password[64];
    char sid[64];
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    QnapState state;
    char* http_buf;
    char xml_buf[QNAP_XML_BUF_SIZE];
} QnapClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void) {
    return wifi_cfg_is_connected();
}

static const char* extract_xml(const char* xml, const char* tag, int index, char* buf, int buf_size)
{
    char start_tag[64], end_tag[64];
    snprintf(start_tag, sizeof(start_tag), "<%s>", tag);
    snprintf(end_tag, sizeof(end_tag), "</%s>", tag);

    const char* p = xml;
    for (int i = 0; i <= index; i++) {
        p = strstr(p, start_tag);
        if (!p) return NULL;
        if (i < index) p += strlen(start_tag);
    }

    p += strlen(start_tag);
    const char* p2 = strstr(p, end_tag);
    if (!p2) return NULL;

    size_t len = (size_t)(p2 - p);
    if (len >= (size_t)buf_size) len = (size_t)(buf_size - 1);
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

static bool http_post(QnapClientData* priv, const char* url, const char* post_data, char* buf, int buf_size)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_http_client_set_header(http, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(http, post_data, strlen(post_data));

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
            ESP_LOGE(TAG, "HTTP POST error: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(http);
    return success;
}

static bool http_get(QnapClientData* priv, const char* url, char* buf, int buf_size)
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
            ESP_LOGE(TAG, "HTTP GET error: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(http);
    return success;
}

static bool qnap_login(QnapClientData* priv)
{
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skip login");
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/cgi-bin/authLogin.cgi",
        priv->nas_ip, priv->nas_port);

    char post_data[128];
    snprintf(post_data, sizeof(post_data), "user=%s&pwd=%s",
        priv->username, priv->password);

    if (!http_post(priv, url, post_data, priv->http_buf, QNAP_HTTP_BUF_SIZE)) {
        return false;
    }

    const char* s = extract_xml(priv->http_buf, "authSid", 0, priv->xml_buf, sizeof(priv->xml_buf));
    if (s && strlen(s) > 0) {
        memcpy(priv->sid, s, sizeof(priv->sid) - 1);
        priv->sid[sizeof(priv->sid) - 1] = '\0';
        return true;
    }

    return false;
}

static bool fetch_sysinfo(QnapClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/cgi-bin/management/manaRequest.cgi?subfunc=sysinfo&hd=no&multicpu=1&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, QNAP_HTTP_BUF_SIZE)) {
        return false;
    }

    const char* xml = priv->http_buf;
    char val_buf[QNAP_XML_BUF_SIZE];

    const char* cpu = extract_xml(xml, "cpuusage", 0, val_buf, sizeof(val_buf));
    if (cpu) priv->data.system.cpu_pct = atof(cpu);

    const char* mem = extract_xml(xml, "mem_perc", 0, val_buf, sizeof(val_buf));
    if (mem) priv->data.system.ram_pct = atof(mem);

    const char* temp = extract_xml(xml, "cpu_temp", 0, val_buf, sizeof(val_buf));
    if (temp) priv->data.system.temp_cpu = (int16_t)atoi(temp);

    const char* mem_total = extract_xml(xml, "mem_total", 0, val_buf, sizeof(val_buf));
    if (mem_total) priv->data.system.ram_total_mb = (uint32_t)atoi(mem_total);

    if (priv->data.system.ram_total_mb > 0 && priv->data.system.ram_pct > 0) {
        priv->data.system.ram_used_mb = (uint32_t)(priv->data.system.ram_total_mb * priv->data.system.ram_pct / 100.0f);
        priv->data.system.ram_free_mb = priv->data.system.ram_total_mb - priv->data.system.ram_used_mb;
    }

    ESP_LOGI(TAG, "SysInfo: CPU=%.1f%%, RAM=%.1f%%",
        priv->data.system.cpu_pct, priv->data.system.ram_pct);
    return true;
}

static bool fetch_storage(QnapClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/cgi-bin/management/chartReq.cgi?chart_func=disk_usage&disk_select=all&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, QNAP_HTTP_BUF_SIZE)) {
        return false;
    }

    const char* xml = priv->http_buf;
    char val_buf[QNAP_XML_BUF_SIZE];

    priv->data.volume_count = 0;
    for (int i = 0; i < MAX_VOLUMES; i++) {
        const char* name = extract_xml(xml, "volume_name", i, val_buf, sizeof(val_buf));
        if (!name) break;

        memcpy(priv->data.volumes[i].name, name, sizeof(priv->data.volumes[i].name) - 1);
        priv->data.volumes[i].name[sizeof(priv->data.volumes[i].name) - 1] = '\0';

        const char* total = extract_xml(xml, "total_size", i, val_buf, sizeof(val_buf));
        if (total) priv->data.volumes[i].total_gb = (uint32_t)(atoi(total) / 1024);

        const char* used = extract_xml(xml, "used_size", i, val_buf, sizeof(val_buf));
        if (used) priv->data.volumes[i].used_gb = (uint32_t)(atoi(used) / 1024);

        if (priv->data.volumes[i].total_gb > 0) {
            priv->data.volumes[i].used_pct = (uint8_t)((uint32_t)priv->data.volumes[i].used_gb * 100 / priv->data.volumes[i].total_gb);
        }

        const char* raid = extract_xml(xml, "raid_type", i, val_buf, sizeof(val_buf));
        if (raid) {
            memcpy(priv->data.volumes[i].raid, raid, sizeof(priv->data.volumes[i].raid) - 1);
            priv->data.volumes[i].raid[sizeof(priv->data.volumes[i].raid) - 1] = '\0';
        }

        const char* status = extract_xml(xml, "volume_status", i, val_buf, sizeof(val_buf));
        if (status) {
            memcpy(priv->data.volumes[i].status, status, sizeof(priv->data.volumes[i].status) - 1);
            priv->data.volumes[i].status[sizeof(priv->data.volumes[i].status) - 1] = '\0';
        }

        priv->data.volume_count++;
    }

    ESP_LOGI(TAG, "Storage: %d volumes", priv->data.volume_count);
    return true;
}

static bool fetch_disks(QnapClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/cgi-bin/disk/qsmart.cgi?func=all_hd_data&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, QNAP_HTTP_BUF_SIZE)) {
        return false;
    }

    const char* xml = priv->http_buf;
    char val_buf[QNAP_XML_BUF_SIZE];

    priv->data.disk_count = 0;
    for (int i = 0; i < MAX_DISKS; i++) {
        const char* name = extract_xml(xml, "disk_name", i, val_buf, sizeof(val_buf));
        if (!name) break;

        memcpy(priv->data.disks[i].name, name, sizeof(priv->data.disks[i].name) - 1);
        priv->data.disks[i].name[sizeof(priv->data.disks[i].name) - 1] = '\0';

        const char* model = extract_xml(xml, "disk_model", i, val_buf, sizeof(val_buf));
        if (model) {
            memcpy(priv->data.disks[i].model_name, model, sizeof(priv->data.disks[i].model_name) - 1);
            priv->data.disks[i].model_name[sizeof(priv->data.disks[i].model_name) - 1] = '\0';
        }

        const char* temp = extract_xml(xml, "disk_temp", i, val_buf, sizeof(val_buf));
        if (temp) priv->data.disks[i].temp = (int16_t)atoi(temp);

        const char* health = extract_xml(xml, "disk_health", i, val_buf, sizeof(val_buf));
        if (health) {
            if (strstr(health, "Good") || strstr(health, "Normal")) {
                priv->data.disks[i].health = HEALTH_OK;
            } else if (strstr(health, "Warning")) {
                priv->data.disks[i].health = HEALTH_WARNING;
            } else {
                priv->data.disks[i].health = HEALTH_CRITICAL;
            }
        }

        const char* size = extract_xml(xml, "disk_size", i, val_buf, sizeof(val_buf));
        if (size) priv->data.disks[i].size_gb = (uint32_t)atoi(size);

        priv->data.disk_count++;
    }

    ESP_LOGI(TAG, "Disks: %d", priv->data.disk_count);
    return true;
}

static bool fetch_network(QnapClientData* priv)
{
    char url[256];
    snprintf(url, sizeof(url),
        "http://%s:%d/cgi-bin/management/chartReq.cgi?chart_func=QSM40bandwidth&_sid=%s",
        priv->nas_ip, priv->nas_port, priv->sid);

    if (!http_get(priv, url, priv->http_buf, QNAP_HTTP_BUF_SIZE)) {
        return false;
    }

    const char* xml = priv->http_buf;
    char val_buf[QNAP_XML_BUF_SIZE];

    const char* rx = extract_xml(xml, "net_rx", 0, val_buf, sizeof(val_buf));
    if (rx) priv->data.network.rx_bps = (uint32_t)(atof(rx) * 1000 / 8);

    const char* tx = extract_xml(xml, "net_tx", 0, val_buf, sizeof(val_buf));
    if (tx) priv->data.network.tx_bps = (uint32_t)(atof(tx) * 1000 / 8);

    const char* iface = extract_xml(xml, "interface", 0, val_buf, sizeof(val_buf));
    if (iface) {
        memcpy(priv->data.network.interface, iface, sizeof(priv->data.network.interface) - 1);
        priv->data.network.interface[sizeof(priv->data.network.interface) - 1] = '\0';
    }

    const char* ip = extract_xml(xml, "ip", 0, val_buf, sizeof(val_buf));
    if (ip) {
        memcpy(priv->data.network.ip, ip, sizeof(priv->data.network.ip) - 1);
        priv->data.network.ip[sizeof(priv->data.network.ip) - 1] = '\0';
    }

    ESP_LOGI(TAG, "Network: RX=%u, TX=%u",
        priv->data.network.rx_bps, priv->data.network.tx_bps);
    return true;
}

static bool qnap_init(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)calloc(1, sizeof(QnapClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, g_config.nas_ip, sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = g_config.nas_port;
    if (priv->nas_port == 0) priv->nas_port = 8080;

    memcpy(priv->username, g_config.nas_user, sizeof(priv->username));
    priv->username[sizeof(priv->username) - 1] = '\0';
    memcpy(priv->password, g_config.nas_pass, sizeof(priv->password));
    priv->password[sizeof(priv->password) - 1] = '\0';

    priv->sid[0] = '\0';
    priv->state = QNAP_IDLE;
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;
    memset(&priv->data, 0, sizeof(priv->data));

    priv->http_buf = (char*)malloc(QNAP_HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Init: %s:%d (user=%s)", priv->nas_ip, priv->nas_port, priv->username);
    return strlen(priv->nas_ip) > 0;
}

static bool qnap_connect(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)self->priv;
    ESP_LOGI(TAG, "Connecting...");
    priv->state = QNAP_LOGIN;

    bool success = qnap_login(priv);
    if (success) {
        ESP_LOGI(TAG, "Connected successfully");
        priv->data.is_online = true;
    } else {
        ESP_LOGW(TAG, "Connection failed, will retry in poll()");
        priv->data.is_online = false;
        priv->consecutive_failures = 1;
        priv->state = QNAP_IDLE;
    }
    return success;
}

static void qnap_disconnect(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)self->priv;
    if (priv) {
        priv->data.is_online = false;
        priv->state = QNAP_IDLE;
        priv->sid[0] = '\0';
    }
    ESP_LOGI(TAG, "Disconnected");
}

static bool qnap_poll(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)self->priv;
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
        if (qnap_login(priv)) {
            ESP_LOGI(TAG, "Reconnected");
            priv->data.is_online = true;
            priv->consecutive_failures = 0;
            priv->state = QNAP_FETCH_SYSINFO;
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
        case QNAP_IDLE:
            priv->state = QNAP_FETCH_SYSINFO;

        case QNAP_LOGIN:
            if (!qnap_login(priv)) {
                priv->data.is_online = false;
                priv->consecutive_failures++;
                ESP_LOGW(TAG, "Login failed (consecutive: %d)", priv->consecutive_failures);
                priv->last_poll_ms = now;
                return false;
            }
            ESP_LOGI(TAG, "Logged in");
            priv->state = QNAP_FETCH_SYSINFO;

        case QNAP_FETCH_SYSINFO:
            success = fetch_sysinfo(priv);
            if (success) {
                priv->state = QNAP_FETCH_STORAGE;
            } else {
                priv->data.is_online = false;
                priv->consecutive_failures++;
            }
            break;

        case QNAP_FETCH_STORAGE:
            success = fetch_storage(priv);
            if (success) {
                priv->state = QNAP_FETCH_DISKS;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case QNAP_FETCH_DISKS:
            success = fetch_disks(priv);
            if (success) {
                priv->state = QNAP_FETCH_NETWORK;
            } else {
                priv->consecutive_failures++;
            }
            break;

        case QNAP_FETCH_NETWORK:
            success = fetch_network(priv);
            if (success) {
                if (priv->consecutive_failures > 0) {
                    ESP_LOGI(TAG, "Connection recovered after %d failures", priv->consecutive_failures);
                    priv->consecutive_failures = 0;
                }
                priv->data.is_online = true;
                priv->data.last_update_ms = now;
                priv->data.has_update = true;
                priv->state = QNAP_DONE;
                ESP_LOGI(TAG, "Poll complete");
            } else {
                priv->consecutive_failures++;
            }
            break;

        case QNAP_DONE:
            priv->state = QNAP_FETCH_SYSINFO;
            priv->last_poll_ms = now;
            return false;
    }

    priv->last_poll_ms = now;
    return priv->data.has_update;
}

static bool qnap_is_connected(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)self->priv;
    return priv->data.is_online;
}

static const NasData* qnap_get_data(DataSource* self)
{
    QnapClientData* priv = (QnapClientData*)self->priv;
    return &priv->data;
}

static const char* qnap_get_type_name(DataSource* self)
{
    (void)self;
    return "QNAP QTS";
}

static const char* qnap_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig qnap_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NAS_QNAP);
}

static void qnap_destroy(DataSource* self)
{
    if (self && self->priv) {
        QnapClientData* priv = (QnapClientData*)self->priv;
        if (priv->http_buf) {
            free(priv->http_buf);
        }
        free(priv);
        self->priv = NULL;
    }
    if (self) free(self);
}

static const DataSourceVTable s_qnap_vtable = {
    .init = qnap_init,
    .connect = qnap_connect,
    .disconnect = qnap_disconnect,
    .poll = qnap_poll,
    .is_connected = qnap_is_connected,
    .get_data = qnap_get_data,
    .get_type_name = qnap_get_type_name,
    .get_conn_icon = qnap_get_conn_icon,
    .get_config = qnap_get_config,
    .destroy = qnap_destroy,
};

DataSource* qnap_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_qnap_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}


