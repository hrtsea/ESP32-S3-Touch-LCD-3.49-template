#include "serial_client.h"
#include "config.h"
#include "../../config/config.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "serial_client";

#define SERIAL_UART_NUM     UART_NUM_2
#define SERIAL_UART_RX_PIN  4
#define SERIAL_UART_TX_PIN  5
#define SERIAL_UART_BUF_SIZE (SERIAL_BUF_SIZE * 2)

typedef enum {
    SERIAL_IDLE,
    SERIAL_WAITING_FRAME,
    SERIAL_RECEIVING,
    SERIAL_PARSING,
    SERIAL_DONE
} SerialState;

typedef struct {
    uint32_t baud_rate;
    SerialState state;
    uint32_t last_frame_ms;
    char rx_buffer[SERIAL_BUF_SIZE];
    int rx_pos;
    NasData data;
    bool connected;
    bool uart_installed;
} SerialClientData;

static uint32_t get_millis(void)
{
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static bool parse_json(SerialClientData* priv, const char* json)
{
    cJSON* doc = cJSON_Parse(json);
    if (!doc) {
        ESP_LOGE(TAG, "JSON parse error");
        return false;
    }

    cJSON* sys = cJSON_GetObjectItem(doc, "system");
    if (cJSON_IsObject(sys)) {
        cJSON* hostname = cJSON_GetObjectItem(sys, "hostname");
        if (cJSON_IsString(hostname)) {
            memcpy(priv->data.system.hostname, hostname->valuestring, sizeof(priv->data.system.hostname) - 1);
            priv->data.system.hostname[sizeof(priv->data.system.hostname) - 1] = '\0';
        }
        cJSON* model = cJSON_GetObjectItem(sys, "model");
        if (cJSON_IsString(model)) {
            memcpy(priv->data.system.model, model->valuestring, sizeof(priv->data.system.model) - 1);
            priv->data.system.model[sizeof(priv->data.system.model) - 1] = '\0';
        }
        cJSON* uptime = cJSON_GetObjectItem(sys, "uptime_s");
        if (cJSON_IsNumber(uptime)) priv->data.system.uptime_s = (uint32_t)uptime->valueint;
        cJSON* cpu_pct = cJSON_GetObjectItem(sys, "cpu_pct");
        if (cJSON_IsNumber(cpu_pct)) priv->data.system.cpu_pct = (float)cpu_pct->valuedouble;
        cJSON* ram_pct = cJSON_GetObjectItem(sys, "ram_pct");
        if (cJSON_IsNumber(ram_pct)) priv->data.system.ram_pct = (float)ram_pct->valuedouble;
        cJSON* ram_total = cJSON_GetObjectItem(sys, "ram_total_mb");
        if (cJSON_IsNumber(ram_total)) priv->data.system.ram_total_mb = (uint32_t)ram_total->valueint;
        cJSON* ram_used = cJSON_GetObjectItem(sys, "ram_used_mb");
        if (cJSON_IsNumber(ram_used)) priv->data.system.ram_used_mb = (uint32_t)ram_used->valueint;
        cJSON* ram_free = cJSON_GetObjectItem(sys, "ram_free_mb");
        if (cJSON_IsNumber(ram_free)) priv->data.system.ram_free_mb = (uint32_t)ram_free->valueint;
        cJSON* ram_cached = cJSON_GetObjectItem(sys, "ram_cached_mb");
        if (cJSON_IsNumber(ram_cached)) priv->data.system.ram_cached_mb = (uint32_t)ram_cached->valueint;
        cJSON* swap_total = cJSON_GetObjectItem(sys, "swap_total_mb");
        if (cJSON_IsNumber(swap_total)) priv->data.system.swap_total_mb = (uint32_t)swap_total->valueint;
        cJSON* swap_used = cJSON_GetObjectItem(sys, "swap_used_mb");
        if (cJSON_IsNumber(swap_used)) priv->data.system.swap_used_mb = (uint32_t)swap_used->valueint;
        cJSON* temp_cpu = cJSON_GetObjectItem(sys, "temp_cpu");
        if (cJSON_IsNumber(temp_cpu)) priv->data.system.temp_cpu = (int16_t)temp_cpu->valueint;
        cJSON* temp_sys = cJSON_GetObjectItem(sys, "temp_sys");
        if (cJSON_IsNumber(temp_sys)) priv->data.system.temp_sys = (int16_t)temp_sys->valueint;

        cJSON* cpu_cores = cJSON_GetObjectItem(sys, "cpu_cores");
        if (cJSON_IsArray(cpu_cores)) {
            int count = cJSON_GetArraySize(cpu_cores);
            if (count > MAX_CPU_CORES) count = MAX_CPU_CORES;
            priv->data.system.cpu_core_count = (uint8_t)count;
            for (int i = 0; i < count; i++) {
                cJSON* core = cJSON_GetArrayItem(cpu_cores, i);
                if (cJSON_IsNumber(core)) {
                    priv->data.system.cpu_cores[i] = (float)core->valuedouble;
                }
            }
        }

        cJSON* load_avg = cJSON_GetObjectItem(sys, "load_avg");
        if (cJSON_IsArray(load_avg)) {
            for (int i = 0; i < 3 && i < cJSON_GetArraySize(load_avg); i++) {
                cJSON* load = cJSON_GetArrayItem(load_avg, i);
                if (cJSON_IsNumber(load)) {
                    priv->data.system.load_avg[i] = (float)load->valuedouble;
                }
            }
        }
    }

    priv->data.disk_count = 0;
    cJSON* disks = cJSON_GetObjectItem(doc, "disks");
    if (cJSON_IsArray(disks)) {
        int disk_count = cJSON_GetArraySize(disks);
        for (int i = 0; i < disk_count && i < MAX_DISKS; i++) {
            cJSON* d = cJSON_GetArrayItem(disks, i);
            if (!cJSON_IsObject(d)) continue;

            cJSON* name = cJSON_GetObjectItem(d, "name");
            if (cJSON_IsString(name)) {
                memcpy(priv->data.disks[i].name, name->valuestring, sizeof(priv->data.disks[i].name) - 1);
                priv->data.disks[i].name[sizeof(priv->data.disks[i].name) - 1] = '\0';
            }
            cJSON* device = cJSON_GetObjectItem(d, "device");
            if (cJSON_IsString(device)) {
                memcpy(priv->data.disks[i].device, device->valuestring, sizeof(priv->data.disks[i].device) - 1);
                priv->data.disks[i].device[sizeof(priv->data.disks[i].device) - 1] = '\0';
            }
            cJSON* model = cJSON_GetObjectItem(d, "model");
            if (cJSON_IsString(model)) {
                memcpy(priv->data.disks[i].model_name, model->valuestring, sizeof(priv->data.disks[i].model_name) - 1);
                priv->data.disks[i].model_name[sizeof(priv->data.disks[i].model_name) - 1] = '\0';
            }
            cJSON* mount = cJSON_GetObjectItem(d, "mount");
            if (cJSON_IsString(mount)) {
                memcpy(priv->data.disks[i].mount, mount->valuestring, sizeof(priv->data.disks[i].mount) - 1);
                priv->data.disks[i].mount[sizeof(priv->data.disks[i].mount) - 1] = '\0';
            }
            cJSON* disk_type = cJSON_GetObjectItem(d, "disk_type");
            if (cJSON_IsString(disk_type)) {
                memcpy(priv->data.disks[i].disk_type, disk_type->valuestring, sizeof(priv->data.disks[i].disk_type) - 1);
                priv->data.disks[i].disk_type[sizeof(priv->data.disks[i].disk_type) - 1] = '\0';
            }
            cJSON* temp = cJSON_GetObjectItem(d, "temp");
            if (cJSON_IsNumber(temp)) priv->data.disks[i].temp = (int16_t)temp->valueint;
            cJSON* size_gb = cJSON_GetObjectItem(d, "size_gb");
            if (cJSON_IsNumber(size_gb)) priv->data.disks[i].size_gb = (uint32_t)size_gb->valueint;
            cJSON* used_gb = cJSON_GetObjectItem(d, "used_gb");
            if (cJSON_IsNumber(used_gb)) priv->data.disks[i].used_gb = (uint32_t)used_gb->valueint;
            cJSON* used_pct = cJSON_GetObjectItem(d, "used_pct");
            if (cJSON_IsNumber(used_pct)) priv->data.disks[i].used_pct = (uint8_t)used_pct->valueint;

            cJSON* health = cJSON_GetObjectItem(d, "health");
            if (cJSON_IsString(health)) {
                if (strcmp(health->valuestring, "OK") == 0) {
                    priv->data.disks[i].health = HEALTH_OK;
                } else if (strcmp(health->valuestring, "WARNING") == 0) {
                    priv->data.disks[i].health = HEALTH_WARNING;
                } else {
                    priv->data.disks[i].health = HEALTH_CRITICAL;
                }
            }
            priv->data.disk_count++;
        }
    }

    priv->data.volume_count = 0;
    cJSON* volumes = cJSON_GetObjectItem(doc, "volumes");
    if (cJSON_IsArray(volumes)) {
        int vol_count = cJSON_GetArraySize(volumes);
        for (int i = 0; i < vol_count && i < MAX_VOLUMES; i++) {
            cJSON* v = cJSON_GetArrayItem(volumes, i);
            if (!cJSON_IsObject(v)) continue;

            cJSON* name = cJSON_GetObjectItem(v, "name");
            if (cJSON_IsString(name)) {
                memcpy(priv->data.volumes[i].name, name->valuestring, sizeof(priv->data.volumes[i].name) - 1);
                priv->data.volumes[i].name[sizeof(priv->data.volumes[i].name) - 1] = '\0';
            }
            cJSON* total_gb = cJSON_GetObjectItem(v, "total_gb");
            if (cJSON_IsNumber(total_gb)) priv->data.volumes[i].total_gb = (uint32_t)total_gb->valueint;
            cJSON* used_gb = cJSON_GetObjectItem(v, "used_gb");
            if (cJSON_IsNumber(used_gb)) priv->data.volumes[i].used_gb = (uint32_t)used_gb->valueint;
            cJSON* used_pct = cJSON_GetObjectItem(v, "used_pct");
            if (cJSON_IsNumber(used_pct)) priv->data.volumes[i].used_pct = (uint8_t)used_pct->valueint;
            cJSON* raid = cJSON_GetObjectItem(v, "raid");
            if (cJSON_IsString(raid)) {
                memcpy(priv->data.volumes[i].raid, raid->valuestring, sizeof(priv->data.volumes[i].raid) - 1);
                priv->data.volumes[i].raid[sizeof(priv->data.volumes[i].raid) - 1] = '\0';
            }
            cJSON* status = cJSON_GetObjectItem(v, "status");
            if (cJSON_IsString(status)) {
                memcpy(priv->data.volumes[i].status, status->valuestring, sizeof(priv->data.volumes[i].status) - 1);
                priv->data.volumes[i].status[sizeof(priv->data.volumes[i].status) - 1] = '\0';
            }
            priv->data.volume_count++;
        }
    }

    priv->data.service_count = 0;
    cJSON* services = cJSON_GetObjectItem(doc, "services");
    if (cJSON_IsArray(services)) {
        int svc_count = cJSON_GetArraySize(services);
        for (int i = 0; i < svc_count && i < MAX_SERVICES; i++) {
            cJSON* s = cJSON_GetArrayItem(services, i);
            if (!cJSON_IsObject(s)) continue;

            cJSON* name = cJSON_GetObjectItem(s, "name");
            if (cJSON_IsString(name)) {
                memcpy(priv->data.services[i].name, name->valuestring, sizeof(priv->data.services[i].name) - 1);
                priv->data.services[i].name[sizeof(priv->data.services[i].name) - 1] = '\0';
            }
            cJSON* running = cJSON_GetObjectItem(s, "running");
            if (cJSON_IsBool(running)) priv->data.services[i].running = cJSON_IsTrue(running);
            cJSON* is_docker = cJSON_GetObjectItem(s, "is_docker");
            if (cJSON_IsBool(is_docker)) priv->data.services[i].is_docker = cJSON_IsTrue(is_docker);
            priv->data.service_count++;
        }
    }

    cJSON* net = cJSON_GetObjectItem(doc, "network");
    if (cJSON_IsObject(net)) {
        cJSON* iface = cJSON_GetObjectItem(net, "interface");
        if (cJSON_IsString(iface)) {
            memcpy(priv->data.network.interface, iface->valuestring, sizeof(priv->data.network.interface) - 1);
            priv->data.network.interface[sizeof(priv->data.network.interface) - 1] = '\0';
        }
        cJSON* ip = cJSON_GetObjectItem(net, "ip");
        if (cJSON_IsString(ip)) {
            memcpy(priv->data.network.ip, ip->valuestring, sizeof(priv->data.network.ip) - 1);
            priv->data.network.ip[sizeof(priv->data.network.ip) - 1] = '\0';
        }
        cJSON* rx_bps = cJSON_GetObjectItem(net, "rx_bps");
        if (cJSON_IsNumber(rx_bps)) priv->data.network.rx_bps = (uint32_t)rx_bps->valueint;
        cJSON* tx_bps = cJSON_GetObjectItem(net, "tx_bps");
        if (cJSON_IsNumber(tx_bps)) priv->data.network.tx_bps = (uint32_t)tx_bps->valueint;
    }

    cJSON_Delete(doc);
    return true;
}

static void clear_data(SerialClientData* priv)
{
    memset(&priv->data, 0, sizeof(priv->data));
    priv->data.system.temp_cpu = -1;
    priv->data.system.temp_sys = -1;
    for (int i = 0; i < MAX_DISKS; i++) {
        priv->data.disks[i].temp = -1;
    }
}

static bool serial_init(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)calloc(1, sizeof(SerialClientData));
    if (!priv) return false;

    self->priv = priv;

    priv->baud_rate = g_config.serial_baud;
    if (priv->baud_rate == 0) priv->baud_rate = DEFAULT_SERIAL_BAUD;

    priv->state = SERIAL_IDLE;
    priv->last_frame_ms = 0;
    priv->rx_pos = 0;
    priv->connected = false;
    priv->uart_installed = false;
    clear_data(priv);

    ESP_LOGI(TAG, "Init: baud=%lu", priv->baud_rate);
    return true;
}

static bool serial_connect(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)self->priv;

    uart_config_t uart_config = {
        .baud_rate = (int)priv->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(SERIAL_UART_NUM, SERIAL_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        return false;
    }
    priv->uart_installed = true;

    err = uart_param_config(SERIAL_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(SERIAL_UART_NUM, SERIAL_UART_TX_PIN, SERIAL_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        return false;
    }

    priv->connected = true;
    priv->state = SERIAL_WAITING_FRAME;
    ESP_LOGI(TAG, "Connected: UART2 RX=%d TX=%d @ %lu",
        SERIAL_UART_RX_PIN, SERIAL_UART_TX_PIN, priv->baud_rate);
    return true;
}

static void serial_disconnect(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)self->priv;
    if (priv && priv->uart_installed) {
        uart_driver_delete(SERIAL_UART_NUM);
        priv->uart_installed = false;
    }
    priv->connected = false;
    priv->state = SERIAL_IDLE;
    ESP_LOGI(TAG, "Disconnected");
}

static bool serial_poll(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)self->priv;
    if (!priv->connected) return false;

    uint32_t now = get_millis();

    if (priv->last_frame_ms > 0 && (now - priv->last_frame_ms) > SERIAL_TIMEOUT_MS) {
        if (priv->data.is_online) {
            priv->data.is_online = false;
            ESP_LOGW(TAG, "Timeout - no data received");
        }
    }

    uint8_t buf[SERIAL_BUF_SIZE];
    int len = uart_read_bytes(SERIAL_UART_NUM, buf, sizeof(buf) - 1, 0);
    if (len > 0) {
        for (int i = 0; i < len; i++) {
            uint8_t c = buf[i];

            if (c == SERIAL_STX) {
                priv->rx_pos = 0;
                priv->state = SERIAL_RECEIVING;
            } else if (c == SERIAL_ETX && priv->state == SERIAL_RECEIVING) {
                priv->rx_buffer[priv->rx_pos] = '\0';
                priv->state = SERIAL_PARSING;
                if (parse_json(priv, priv->rx_buffer)) {
                    priv->data.is_online = true;
                    priv->data.last_update_ms = now;
                    priv->data.has_update = true;
                    priv->last_frame_ms = now;
                }
                priv->state = SERIAL_WAITING_FRAME;
                priv->rx_pos = 0;
                break;
            } else if (priv->state == SERIAL_RECEIVING) {
                if (priv->rx_pos < SERIAL_BUF_SIZE - 1) {
                    priv->rx_buffer[priv->rx_pos++] = (char)c;
                }
            }
        }
    }

    return priv->data.has_update;
}

static bool serial_is_connected(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)self->priv;
    return priv->connected && priv->data.is_online;
}

static const NasData* serial_get_data(DataSource* self)
{
    SerialClientData* priv = (SerialClientData*)self->priv;
    return &priv->data;
}

static const char* serial_get_type_name(DataSource* self)
{
    (void)self;
    return "Linux (Serial)";
}

static const char* serial_get_conn_icon(DataSource* self)
{
    (void)self;
    return "usb";
}

static NasTypeConfig serial_get_config(DataSource* self)
{
    (void)self;
    return nas_type_config_get_defaults(NET_LINUX_SERIAL);
}

static void serial_destroy(DataSource* self)
{
    if (self && self->priv) {
        SerialClientData* priv = (SerialClientData*)self->priv;
        if (priv->uart_installed) {
            uart_driver_delete(SERIAL_UART_NUM);
        }
        free(priv);
        self->priv = NULL;
    }
    if (self) free(self);
}

static const DataSourceVTable s_serial_vtable = {
    .init = serial_init,
    .connect = serial_connect,
    .disconnect = serial_disconnect,
    .poll = serial_poll,
    .is_connected = serial_is_connected,
    .get_data = serial_get_data,
    .get_type_name = serial_get_type_name,
    .get_conn_icon = serial_get_conn_icon,
    .get_config = serial_get_config,
    .destroy = serial_destroy,
};

DataSource* serial_client_create(void)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;
    self->vtable = &s_serial_vtable;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}
