#include "unraid_client.h"
#include "app_cfg.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "unraid_client";

#define UNRAID_HTTP_BUF_SIZE 4096
#define UNRAID_URL_LEN       128

/* GraphQL 查询：一次性获取所有监控数据 */
static const char* s_graphql_query =
    "{\"query\":\"{ "
    "notifications { overview { unread { info warning alert } } } "
    "array { parityCheckStatus { status speed running errors progress duration paused } "
    "disks { temp name size status warning critical numReads numWrites } "
    "state capacity { kilobytes { free total used } } } "
    "info { cpu { cores manufacturer model packages { temp power totalPower } vendor threads speed brand speedmax } "
    "os { fqdn hostname kernel release uptime } } "
    "metrics { cpu { percentTotal } memory { available percentTotal total active } } "
    "docker { containers { names state } } "
    "vms { domains { name state } } "
    "}\"}";

typedef struct {
    char nas_ip[40];
    uint16_t nas_port;
    char api_key[65];
    bool use_https;
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    char* http_buf;
} UnraidClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool wifi_is_connected(void)
{
    return wifi_cfg_is_connected();
}

static void clear_data(NasData* data)
{
    memset(data, 0, sizeof(NasData));
    data->system.temp_cpu = -1;
    data->system.temp_sys = -1;
    for (int i = 0; i < MAX_DISKS; i++) data->disks[i].temp = -1;
}

/* ── GraphQL 字段解析辅助 ── */

static float json_get_float(cJSON* obj, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
}

static int json_get_int(cJSON* obj, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (int)item->valuedouble : 0;
}

static const char* json_get_str(cJSON* obj, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : NULL;
}

static long long json_get_ll(cJSON* obj, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) return (long long)item->valuedouble;
    /* Unraid 容量字段有时以字符串返回 */
    if (cJSON_IsString(item) && item->valuestring) return atoll(item->valuestring);
    return 0;
}

/* ── 解析 GraphQL 响应到 NasData ── */

static void parse_system_info(UnraidClientData* priv, cJSON* data_obj)
{
    /* OS 信息 */
    cJSON* info = cJSON_GetObjectItemCaseSensitive(data_obj, "info");
    if (!cJSON_IsObject(info)) return;

    cJSON* os = cJSON_GetObjectItemCaseSensitive(info, "os");
    if (cJSON_IsObject(os)) {
        const char* hostname = json_get_str(os, "hostname");
        if (hostname) strncpy(priv->data.system.hostname, hostname, sizeof(priv->data.system.hostname) - 1);
        priv->data.system.uptime_s = (uint32_t)json_get_int(os, "uptime");
    }

    /* CPU 信息 */
    cJSON* cpu_info = cJSON_GetObjectItemCaseSensitive(info, "cpu");
    if (cJSON_IsObject(cpu_info)) {
        priv->data.system.cpu_core_count = (uint8_t)json_get_int(cpu_info, "cores");
        const char* brand = json_get_str(cpu_info, "brand");
        if (brand) {
            strncpy(priv->data.system.model, brand, sizeof(priv->data.system.model) - 1);
        }
        /* CPU 温度取第一个 package */
        cJSON* packages = cJSON_GetObjectItemCaseSensitive(cpu_info, "packages");
        if (cJSON_IsArray(packages) && cJSON_GetArraySize(packages) > 0) {
            cJSON* pkg0 = cJSON_GetArrayItem(packages, 0);
            if (cJSON_IsObject(pkg0)) {
                cJSON* temp_arr = cJSON_GetObjectItemCaseSensitive(pkg0, "temp");
                if (cJSON_IsArray(temp_arr) && cJSON_GetArraySize(temp_arr) > 0) {
                    cJSON* t0 = cJSON_GetArrayItem(temp_arr, 0);
                    if (cJSON_IsNumber(t0)) priv->data.system.temp_cpu = (int16_t)t0->valuedouble;
                } else if (cJSON_IsNumber(temp_arr)) {
                    priv->data.system.temp_cpu = (int16_t)temp_arr->valuedouble;
                }
            }
        }
    }

    /* Metrics: CPU 使用率 + 内存 */
    cJSON* metrics = cJSON_GetObjectItemCaseSensitive(data_obj, "metrics");
    if (cJSON_IsObject(metrics)) {
        cJSON* cpu_m = cJSON_GetObjectItemCaseSensitive(metrics, "cpu");
        if (cJSON_IsObject(cpu_m)) {
            priv->data.system.cpu_pct = json_get_float(cpu_m, "percentTotal");
        }

        cJSON* mem_m = cJSON_GetObjectItemCaseSensitive(metrics, "memory");
        if (cJSON_IsObject(mem_m)) {
            priv->data.system.ram_pct = json_get_float(mem_m, "percentTotal");
            /* active 和 total 以字节为单位，转 MB */
            long long total_bytes = json_get_ll(mem_m, "total");
            long long active_bytes = json_get_ll(mem_m, "active");
            priv->data.system.ram_total_mb = (uint32_t)(total_bytes / (1024 * 1024));
            priv->data.system.ram_used_mb = (uint32_t)(active_bytes / (1024 * 1024));
            if (priv->data.system.ram_total_mb > 0) {
                priv->data.system.ram_free_mb = priv->data.system.ram_total_mb - priv->data.system.ram_used_mb;
                priv->data.system.ram_pct = (float)priv->data.system.ram_used_mb * 100.0f / priv->data.system.ram_total_mb;
            }
        }
    }
}

static void parse_array_disks(UnraidClientData* priv, cJSON* data_obj)
{
    cJSON* array_obj = cJSON_GetObjectItemCaseSensitive(data_obj, "array");
    if (!cJSON_IsObject(array_obj)) return;

    /* 阵列总容量 → disk_pct */
    cJSON* capacity = cJSON_GetObjectItemCaseSensitive(array_obj, "capacity");
    if (cJSON_IsObject(capacity)) {
        cJSON* kb = cJSON_GetObjectItemCaseSensitive(capacity, "kilobytes");
        if (cJSON_IsObject(kb)) {
            long long used_kb = json_get_ll(kb, "used");
            long long total_kb = json_get_ll(kb, "total");
            if (total_kb > 0) {
                priv->data.system.disk_pct = (float)used_kb * 100.0f / (float)total_kb;
            }
        }
    }

    /* 磁盘列表 */
    cJSON* disks_arr = cJSON_GetObjectItemCaseSensitive(array_obj, "disks");
    if (!cJSON_IsArray(disks_arr)) return;

    int count = cJSON_GetArraySize(disks_arr);
    if (count > MAX_DISKS) count = MAX_DISKS;
    priv->data.disk_count = 0;

    for (int i = 0; i < count; i++) {
        cJSON* d = cJSON_GetArrayItem(disks_arr, i);
        if (!cJSON_IsObject(d)) continue;

        const char* name = json_get_str(d, "name");
        if (name) strncpy(priv->data.disks[i].name, name, sizeof(priv->data.disks[i].name) - 1);

        const char* status = json_get_str(d, "status");
        /* 健康状态：DISK_OK=正常，其他为警告/严重 */
        if (status && strcmp(status, "DISK_OK") != 0) {
            priv->data.disks[i].health = HEALTH_WARNING;
        } else {
            priv->data.disks[i].health = HEALTH_OK;
        }

        cJSON* temp_item = cJSON_GetObjectItemCaseSensitive(d, "temp");
        if (cJSON_IsNumber(temp_item)) {
            priv->data.disks[i].temp = (int16_t)temp_item->valuedouble;
        } else {
            priv->data.disks[i].temp = -1;
        }

        /* 容量（size 以 KB 为单位的字符串或数字） */
        long long size_kb = json_get_ll(d, "size");
        if (size_kb > 0) {
            priv->data.disks[i].size_gb = (uint32_t)(size_kb / (1024 * 1024)); /* KB→GB */
        }

        priv->data.disks[i].online = true;
        priv->data.disks[i].slot_index = (uint8_t)i;
        priv->data.disk_count++;
    }
    priv->data.disk_slot_count = priv->data.disk_count;
}

static void parse_services(UnraidClientData* priv, cJSON* data_obj)
{
    priv->data.service_count = 0;

    /* Docker 容器 */
    cJSON* docker = cJSON_GetObjectItemCaseSensitive(data_obj, "docker");
    if (cJSON_IsObject(docker)) {
        cJSON* containers = cJSON_GetObjectItemCaseSensitive(docker, "containers");
        if (cJSON_IsArray(containers)) {
            int cnt = cJSON_GetArraySize(containers);
            for (int i = 0; i < cnt && priv->data.service_count < MAX_SERVICES; i++) {
                cJSON* c = cJSON_GetArrayItem(containers, i);
                if (!cJSON_IsObject(c)) continue;

                cJSON* names = cJSON_GetObjectItemCaseSensitive(c, "names");
                const char* name = NULL;
                if (cJSON_IsArray(names) && cJSON_GetArraySize(names) > 0) {
                    cJSON* n0 = cJSON_GetArrayItem(names, 0);
                    if (cJSON_IsString(n0) && n0->valuestring) name = n0->valuestring;
                } else if (cJSON_IsString(names) && names->valuestring) {
                    name = names->valuestring;
                }

                if (name) {
                    /* 去除前导 '/' */
                    if (name[0] == '/') name++;
                    strncpy(priv->data.services[priv->data.service_count].name,
                            name, sizeof(priv->data.services[0].name) - 1);
                }
                const char* state = json_get_str(c, "state");
                priv->data.services[priv->data.service_count].running = (state && strcmp(state, "running") == 0);
                priv->data.services[priv->data.service_count].is_docker = true;
                priv->data.service_count++;
            }
        }
    }

    /* VM 虚拟机 */
    cJSON* vms = cJSON_GetObjectItemCaseSensitive(data_obj, "vms");
    if (cJSON_IsObject(vms)) {
        cJSON* domains = cJSON_GetObjectItemCaseSensitive(vms, "domains");
        if (cJSON_IsArray(domains)) {
            int cnt = cJSON_GetArraySize(domains);
            for (int i = 0; i < cnt && priv->data.service_count < MAX_SERVICES; i++) {
                cJSON* vm = cJSON_GetArrayItem(domains, i);
                if (!cJSON_IsObject(vm)) continue;

                const char* name = json_get_str(vm, "name");
                if (name) {
                    strncpy(priv->data.services[priv->data.service_count].name,
                            name, sizeof(priv->data.services[0].name) - 1);
                }
                const char* state = json_get_str(vm, "state");
                priv->data.services[priv->data.service_count].running = (state && strcmp(state, "running") == 0);
                priv->data.services[priv->data.service_count].is_docker = false;
                priv->data.service_count++;
            }
        }
    }
}

static bool parse_graphql_response(UnraidClientData* priv, const char* json)
{
    cJSON* doc = cJSON_Parse(json);
    if (!doc) {
        ESP_LOGE(TAG, "JSON parse error");
        return false;
    }

    cJSON* data_obj = cJSON_GetObjectItemCaseSensitive(doc, "data");
    if (cJSON_IsNull(data_obj) || !cJSON_IsObject(data_obj)) {
        ESP_LOGE(TAG, "GraphQL returned null data — API Key invalid?");
        cJSON_Delete(doc);
        return false;
    }

    /* 清空旧数据但保留在线状态 */
    bool was_online = priv->data.is_online;
    clear_data(&priv->data);
    priv->data.is_online = was_online;

    parse_system_info(priv, data_obj);
    parse_array_disks(priv, data_obj);
    parse_services(priv, data_obj);

    cJSON_Delete(doc);
    return true;
}

/* ── HTTP POST (GraphQL) ── */

static bool graphql_fetch(UnraidClientData* priv)
{
    if (!wifi_is_connected()) {
        priv->data.is_online = false;
        return false;
    }

    char url[UNRAID_URL_LEN];
    const char* proto = priv->use_https ? "https" : "http";
    snprintf(url, sizeof(url), "%s://%s:%d/graphql", proto, priv->nas_ip, priv->nas_port);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    /* 设置请求头 */
    esp_http_client_set_header(http, "Content-Type", "application/json");
    if (priv->api_key[0]) {
        esp_http_client_set_header(http, "x-api-key", priv->api_key);
    }

    /* 设置 POST body */
    size_t body_len = strlen(s_graphql_query);
    esp_err_t err = esp_http_client_open(http, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http);
        return false;
    }

    int written = esp_http_client_write(http, s_graphql_query, body_len);
    if (written < 0) {
        ESP_LOGE(TAG, "HTTP write failed");
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        return false;
    }

    /* 读取响应 */
    int content_len = esp_http_client_fetch_headers(http);
    int total_read = 0;
    int read_len;

    if (content_len > 0 && content_len < UNRAID_HTTP_BUF_SIZE) {
        while (total_read < content_len) {
            read_len = esp_http_client_read(http, priv->http_buf + total_read, content_len - total_read);
            if (read_len <= 0) break;
            total_read += read_len;
        }
    } else {
        /* 不知道长度时读到 EOF 或缓冲区满 */
        while (total_read < UNRAID_HTTP_BUF_SIZE - 1) {
            read_len = esp_http_client_read(http, priv->http_buf + total_read, UNRAID_HTTP_BUF_SIZE - 1 - total_read);
            if (read_len <= 0) break;
            total_read += read_len;
        }
    }
    priv->http_buf[total_read] = '\0';

    int status = esp_http_client_get_status_code(http);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        return false;
    }

    if (total_read == 0) {
        ESP_LOGE(TAG, "Empty response");
        return false;
    }

    bool parsed = parse_graphql_response(priv, priv->http_buf);
    if (parsed) {
        priv->data.is_online = true;
        priv->data.last_update_ms = get_millis();
        priv->data.has_update = true;
    }
    return parsed;
}

/* ── VTable 实现 ── */

static bool unraid_init(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)calloc(1, sizeof(UnraidClientData));
    if (!priv) return false;

    self->priv = priv;

    memcpy(priv->nas_ip, app_cfg_get_nas_ip(), sizeof(priv->nas_ip));
    priv->nas_ip[sizeof(priv->nas_ip) - 1] = '\0';
    priv->nas_port = app_cfg_get_nas_port();
    if (priv->nas_port == 0) priv->nas_port = 80;

    /* API Key 存储在 nas_pass 字段 */
    memcpy(priv->api_key, app_cfg_get_nas_pass(), sizeof(priv->api_key));
    priv->api_key[sizeof(priv->api_key) - 1] = '\0';

    priv->use_https = app_cfg_get_nas_https();
    priv->last_poll_ms = 0;
    priv->consecutive_failures = 0;

    priv->http_buf = (char*)malloc(UNRAID_HTTP_BUF_SIZE);
    if (!priv->http_buf) {
        free(priv);
        self->priv = NULL;
        return false;
    }

    clear_data(&priv->data);

    ESP_LOGI(TAG, "Init: %s:%d (api_key=%s)", priv->nas_ip, priv->nas_port,
             priv->api_key[0] ? "set" : "EMPTY");
    return strlen(priv->nas_ip) > 0;
}

static bool unraid_connect(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    if (!priv || strlen(priv->nas_ip) == 0) return false;

    ESP_LOGI(TAG, "Connecting to %s:%d ...", priv->nas_ip, priv->nas_port);
    if (graphql_fetch(priv)) {
        ESP_LOGI(TAG, "Connected successfully");
        priv->data.is_online = true;
        return true;
    }
    ESP_LOGW(TAG, "Connection failed, will retry in poll()");
    priv->data.is_online = false;
    priv->consecutive_failures = 1;
    return false;
}

static void unraid_disconnect(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    if (!priv) return;
    ESP_LOGI(TAG, "Disconnecting...");
    priv->data.is_online = false;
}

static bool unraid_poll(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    if (!priv) return false;

    uint32_t now = get_millis();
    uint32_t poll_interval = app_cfg_get_poll_sec() * 1000UL;

    /* 失败时指数退避 */
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

    if (!graphql_fetch(priv)) {
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

    priv->data.is_online = true;
    priv->last_poll_ms = now;
    return true;
}

static bool unraid_is_connected(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    return priv && priv->data.is_online;
}

static const NasData* unraid_get_data(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    return priv ? &priv->data : NULL;
}

static const char* unraid_get_type_name(DataSource* self)
{
    (void)self;
    return "Unraid";
}

static const char* unraid_get_conn_icon(DataSource* self)
{
    (void)self;
    return "wifi";
}

static NasTypeConfig unraid_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NAS_UNRAID);
}

static void unraid_destroy(DataSource* self)
{
    UnraidClientData* priv = (UnraidClientData*)self->priv;
    if (priv) {
        if (priv->http_buf) free(priv->http_buf);
        free(priv);
    }
    free(self);
}

static const DataSourceVTable s_unraid_vtable = {
    .init = unraid_init,
    .connect = unraid_connect,
    .disconnect = unraid_disconnect,
    .poll = unraid_poll,
    .is_connected = unraid_is_connected,
    .get_data = unraid_get_data,
    .get_type_name = unraid_get_type_name,
    .get_conn_icon = unraid_get_conn_icon,
    .get_config = unraid_get_config,
    .destroy = unraid_destroy,
};

DataSource* unraid_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_unraid_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}
