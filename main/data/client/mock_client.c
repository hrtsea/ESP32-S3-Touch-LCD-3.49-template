#include "mock_client.h"
#include "config.h"
#include "nas_data.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char* TAG = "mock_client";

typedef struct {
    NasType type;
    const char* type_name;
    const char* conn_icon;
} MockClientPriv;

static uint32_t s_counter = 0;

static void generate_mock_data(NasData* data)
{
    s_counter++;

    strncpy(data->system.hostname, "ZotLab-NAS-Mock", sizeof(data->system.hostname));
    strncpy(data->system.model, "Mock NAS DS920+", sizeof(data->system.model));
    data->system.uptime_s = s_counter * 100;
    data->system.temp_cpu = 42 + (s_counter % 10);
    data->system.temp_sys = 38 + (s_counter % 8);
    data->system.cpu_pct = 25.0f + (s_counter % 30);
    data->system.ram_total_mb = 8192;
    data->system.ram_used_mb = 3276 + (s_counter % 500);
    data->system.ram_pct = (data->system.ram_used_mb * 100.0f) / data->system.ram_total_mb;

    data->system.cpu_core_count = 4;
    for (int i = 0; i < 4; i++) {
        data->system.cpu_cores[i] = 20.0f + ((s_counter + i * 10) % 40);
    }

    data->system.load_avg[0] = 0.5f + (s_counter % 20) / 10.0f;
    data->system.load_avg[1] = 0.8f;
    data->system.load_avg[2] = 1.2f;

    data->system.ram_free_mb = data->system.ram_total_mb - data->system.ram_used_mb;
    data->system.ram_cached_mb = 512;
    data->system.swap_total_mb = 2048;
    data->system.swap_used_mb = 128;

    float simulated_temp = 45.0f + sinf(s_counter * 0.1f) * 15.0f;
    data->fan.ctrl_temp = (int16_t)simulated_temp;

    uint8_t simulated_pwm = 0;
    if (simulated_temp < 35) {
        simulated_pwm = 20;
    } else if (simulated_temp < 50) {
        simulated_pwm = 20 + (uint8_t)((simulated_temp - 35) * 2.0f);
    } else if (simulated_temp < 65) {
        simulated_pwm = 50 + (uint8_t)((simulated_temp - 50) * 2.0f);
    } else {
        simulated_pwm = 80 + (uint8_t)((simulated_temp - 65) * 1.33f);
    }
    if (simulated_pwm > 100) simulated_pwm = 100;

    data->fan.pwm_pct = simulated_pwm;
    data->fan.rpm = 800 + (uint32_t)(simulated_pwm * 22.0f);
    data->fan.rpm += (s_counter % 50) - 25;
    data->fan.pwm_pct += (s_counter % 3) - 1;
    data->fan.enabled = true;
    data->fan.stall_alarm = (s_counter % 500 == 0);

    data->disk_count = config_get_total_disk_slots();
    data->disk_slot_count = config_get_total_disk_slots();

    uint64_t total_size_gb = 0;
    uint64_t total_used_gb = 0;

    for (int i = 0; i < 3; i++) {
        snprintf(data->disks[i].name, sizeof(data->disks[i].name), "Disk %d", i + 1);
        snprintf(data->disks[i].model_name, sizeof(data->disks[i].model_name), "WD RED %dTB", 4 + i);
        snprintf(data->disks[i].device, sizeof(data->disks[i].device), "/dev/sd%c", 'a' + i);
        data->disks[i].size_gb = 4000 + (i * 1000);
        data->disks[i].used_gb = 2000 + (s_counter % 1000) + (i * 200);
        data->disks[i].used_pct = (uint8_t)((uint64_t)data->disks[i].used_gb * 100 / data->disks[i].size_gb);
        data->disks[i].temp = 35 + (s_counter % 8) + i;
        data->disks[i].health = HEALTH_OK;
        data->disks[i].read_kbps = 100000 + (s_counter % 50000) + (i * 10000);
        data->disks[i].write_kbps = 80000 + (s_counter % 40000) + (i * 8000);
        data->disks[i].online = true;
        data->disks[i].slot_index = i;
        total_size_gb += data->disks[i].size_gb;
        total_used_gb += data->disks[i].used_gb;
    }

    int empty_slot = 3;
    snprintf(data->disks[empty_slot].name, sizeof(data->disks[empty_slot].name), "Disk %d", empty_slot + 1);
    snprintf(data->disks[empty_slot].model_name, sizeof(data->disks[empty_slot].model_name), "Empty");
    snprintf(data->disks[empty_slot].device, sizeof(data->disks[empty_slot].device), "/dev/sd%c", 'a' + empty_slot);
    data->disks[empty_slot].size_gb = 0;
    data->disks[empty_slot].used_gb = 0;
    data->disks[empty_slot].used_pct = 0;
    data->disks[empty_slot].temp = 0;
    data->disks[empty_slot].health = HEALTH_UNKNOWN;
    data->disks[empty_slot].read_kbps = 0;
    data->disks[empty_slot].write_kbps = 0;
    data->disks[empty_slot].online = false;
    data->disks[empty_slot].slot_index = empty_slot;

    if (total_size_gb > 0) {
        data->system.disk_pct = (float)(total_used_gb * 100) / (float)total_size_gb;
    } else {
        data->system.disk_pct = 0;
    }

    data->volume_count = 2;
    for (int i = 0; i < 2; i++) {
        snprintf(data->volumes[i].name, sizeof(data->volumes[i].name), "Volume %d", i + 1);
        data->volumes[i].total_gb = 8000 + (i * 4000);
        data->volumes[i].used_gb = 4000 + (s_counter % 1500) + (i * 500);
        data->volumes[i].used_pct = (uint8_t)((uint64_t)data->volumes[i].used_gb * 100 / data->volumes[i].total_gb);
        strncpy(data->volumes[i].raid, "RAID5", sizeof(data->volumes[i].raid));
        strncpy(data->volumes[i].status, "normal", sizeof(data->volumes[i].status));
    }

    data->service_count = 8;
    const char* service_names[] = {"SMB", "NFS", "AFP", "FTP", "SSH", "Docker", "Web Station", "Surveillance"};
    for (int i = 0; i < 8; i++) {
        strncpy(data->services[i].name, service_names[i], sizeof(data->services[i].name));
        data->services[i].running = (i < 6);
        data->services[i].is_docker = (i == 5);
    }

    data->network.rx_bps = (10 + (s_counter % 90)) * 1000000;
    data->network.tx_bps = (5 + (s_counter % 45)) * 1000000;
    strncpy(data->network.ip, "192.168.1.100", sizeof(data->network.ip));
    strncpy(data->network.interface, "eth0", sizeof(data->network.interface));

    data->interface_count = 3;

    strncpy(data->interfaces[0].name, "eth0", sizeof(data->interfaces[0].name));
    strncpy(data->interfaces[0].ip, "192.168.1.100", sizeof(data->interfaces[0].ip));
    data->interfaces[0].rx_bps = (10 + (s_counter % 90)) * 125;
    data->interfaces[0].tx_bps = (5 + (s_counter % 45)) * 125;
    data->interfaces[0].active = true;

    strncpy(data->interfaces[1].name, "eth1", sizeof(data->interfaces[1].name));
    strncpy(data->interfaces[1].ip, "192.168.2.100", sizeof(data->interfaces[1].ip));
    data->interfaces[1].rx_bps = (s_counter % 5) * 125;
    data->interfaces[1].tx_bps = (s_counter % 3) * 125;
    data->interfaces[1].active = false;

    strncpy(data->interfaces[2].name, "wlan0", sizeof(data->interfaces[2].name));
    strncpy(data->interfaces[2].ip, "192.168.1.150", sizeof(data->interfaces[2].ip));
    data->interfaces[2].rx_bps = (2 + (s_counter % 20)) * 125;
    data->interfaces[2].tx_bps = (1 + (s_counter % 10)) * 125;
    data->interfaces[2].active = true;

    data->active_interface_idx = 0;

    data->fan.rpm = 1200 + (s_counter % 300);
    data->fan.pwm_pct = 40 + (s_counter % 20);
    data->fan.enabled = true;
    data->fan.stall_alarm = false;
    data->fan.ctrl_temp = data->system.temp_cpu;
}

static bool mock_init(DataSource* self)
{
    ESP_LOGI(TAG, "Init: Mock Data Source");
    generate_mock_data(&self->data);
    return true;
}

static bool mock_connect(DataSource* self)
{
    (void)self;
    ESP_LOGI(TAG, "Connected (simulated)");
    return true;
}

static void mock_disconnect(DataSource* self)
{
    (void)self;
    ESP_LOGI(TAG, "Disconnected (simulated)");
}

static bool mock_poll(DataSource* self)
{
    uint32_t now = (uint32_t)(esp_log_timestamp() / 1000);
    uint32_t poll_interval = g_config.poll_sec * 1000UL;

    if (self->last_poll_ms > 0 && (now - self->last_poll_ms) < poll_interval) {
        return false;
    }

    generate_mock_data(&self->data);
    self->data.is_online = true;
    self->data.last_update_ms = now;
    self->data.has_update = true;
    self->last_poll_ms = now;
    return true;
}

static bool mock_is_connected(DataSource* self)
{
    (void)self;
    return true;
}

static const NasData* mock_get_data(DataSource* self)
{
    return &self->data;
}

static const char* mock_get_type_name(DataSource* self)
{
    MockClientPriv* priv = (MockClientPriv*)self->priv;
    if (priv && priv->type_name) return priv->type_name;
    return "Mock Data Source";
}

static const char* mock_get_conn_icon(DataSource* self)
{
    MockClientPriv* priv = (MockClientPriv*)self->priv;
    if (priv && priv->conn_icon) return priv->conn_icon;
    return "wifi";
}

static NasTypeConfig mock_get_config(DataSource* self)
{
    MockClientPriv* priv = (MockClientPriv*)self->priv;
    if (priv) return nas_type_config_get_defaults(priv->type);
    return nas_type_config_get_defaults(NAS_MOCK);
}

static void mock_destroy(DataSource* self)
{
    if (self) {
        if (self->priv) {
            free(self->priv);
        }
        free(self);
    }
}

static const DataSourceVTable s_mock_vtable = {
    .init = mock_init,
    .connect = mock_connect,
    .disconnect = mock_disconnect,
    .poll = mock_poll,
    .is_connected = mock_is_connected,
    .get_data = mock_get_data,
    .get_type_name = mock_get_type_name,
    .get_conn_icon = mock_get_conn_icon,
    .get_config = mock_get_config,
    .destroy = mock_destroy,
};

DataSource* mock_client_create(void)
{
    return mock_client_create_with_type(NAS_MOCK, "Mock Data Source", "wifi");
}

DataSource* mock_client_create_with_type(NasType type, const char* type_name, const char* conn_icon)
{
    DataSource* self = (DataSource*)calloc(1, sizeof(DataSource));
    if (!self) return NULL;

    MockClientPriv* priv = (MockClientPriv*)calloc(1, sizeof(MockClientPriv));
    if (!priv) {
        free(self);
        return NULL;
    }
    priv->type = type;
    priv->type_name = type_name;
    priv->conn_icon = conn_icon;

    self->vtable = &s_mock_vtable;
    self->priv = priv;
    self->last_poll_ms = 0;
    self->consecutive_failures = 0;
    return self;
}
