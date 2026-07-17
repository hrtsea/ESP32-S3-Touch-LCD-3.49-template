#include "config.h"
#include "app_info.h"
#include "event_bus.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"

AppConfig g_config;
ConfigBackup g_config_backup;

static void config_init_defaults(void)
{
    memset(&g_config, 0, sizeof(AppConfig));
    strlcpy(g_config.nas_type, "mock", sizeof(g_config.nas_type));
    g_config.nas_port = 0;
    g_config.poll_sec = DEFAULT_POLL_SEC;
    g_config.rotation_angle = 0;
    g_config.brightness = 128;
    g_config.autodim = true;
    g_config.timezone = 8;
    g_config.serial_baud = DEFAULT_SERIAL_BAUD;
    g_config.snmp_ver = 2;
    g_config.sata_disk_count = DEFAULT_SATA_DISK_COUNT;
    g_config.m2_disk_count = DEFAULT_M2_DISK_COUNT;
    g_config.auto_cycle_enabled = false;
    g_config.auto_cycle_interval_sec = 10;
    g_config.fan.enabled = FAN_ENABLED;
    g_config.fan.mode = FAN_MODE_MANUAL;
    g_config.fan.manual_pwm_pct = 50;
    g_config.fan.temp_source = TEMP_MAX_CPU_SYS;
    g_config.fan.hysteresis = FAN_DEFAULT_HYSTERESIS;
    g_config.fan.min_change_pct = FAN_DEFAULT_MIN_PWM;
    g_config.fan.min_pwm_pct = FAN_DEFAULT_MIN_PWM;
    g_config.fan.emergency_temp = FAN_DEFAULT_EMERGENCY;
    g_config.fan.ramp_time_ms = FAN_DEFAULT_RAMP_MS;
    g_config.fan.stall_detect_sec = FAN_DEFAULT_STALL_SEC;
    memcpy(g_config.fan.curve, DEFAULT_FAN_CURVE, sizeof(FanCurvePoint) * FAN_CURVE_POINTS);
}

void config_load(void)
{
    config_init_defaults();

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        return;
    }

    size_t len;

    len = sizeof(g_config.ssid);
    nvs_get_str(h, NVS_WIFI_SSID, g_config.ssid, &len);

    len = sizeof(g_config.wifipass);
    nvs_get_str(h, NVS_WIFI_PASS, g_config.wifipass, &len);

    len = sizeof(g_config.nas_type);
    nvs_get_str(h, NVS_NAS_TYPE, g_config.nas_type, &len);
    if (strlen(g_config.nas_type) == 0) {
        strlcpy(g_config.nas_type, "mock", sizeof(g_config.nas_type));
    }

    len = sizeof(g_config.nas_ip);
    nvs_get_str(h, NVS_NAS_IP, g_config.nas_ip, &len);

    uint16_t port;
    if (nvs_get_u16(h, NVS_NAS_PORT, &port) == ESP_OK) {
        g_config.nas_port = port;
    }

    len = sizeof(g_config.nas_user);
    nvs_get_str(h, NVS_NAS_USER, g_config.nas_user, &len);

    len = sizeof(g_config.nas_pass);
    nvs_get_str(h, NVS_NAS_PASS, g_config.nas_pass, &len);

    uint8_t https;
    if (nvs_get_u8(h, NVS_NAS_HTTPS, &https) == ESP_OK) {
        g_config.nas_https = https != 0;
    }

    len = sizeof(g_config.snmp_comm);
    nvs_get_str(h, NVS_SNMP_COMM, g_config.snmp_comm, &len);

    if (nvs_get_u8(h, NVS_SNMP_VER, &g_config.snmp_ver) != ESP_OK) {
        g_config.snmp_ver = 2;
    }

    uint32_t baud;
    if (nvs_get_u32(h, NVS_SERIAL_BAUD, &baud) == ESP_OK) {
        g_config.serial_baud = baud;
    }

    if (nvs_get_u8(h, NVS_POLL_SEC, &g_config.poll_sec) != ESP_OK) {
        g_config.poll_sec = DEFAULT_POLL_SEC;
    }

    if (nvs_get_u8(h, NVS_ROTATION_ANGLE, &g_config.rotation_angle) != ESP_OK) {
        g_config.rotation_angle = 0;
    }

    if (nvs_get_u8(h, NVS_BRIGHTNESS, &g_config.brightness) != ESP_OK) {
        g_config.brightness = 128;
    }

    uint8_t autodim;
    if (nvs_get_u8(h, NVS_AUTODIM, &autodim) == ESP_OK) {
        g_config.autodim = autodim != 0;
    }

    int8_t tz;
    if (nvs_get_i8(h, NVS_TIMEZONE, &tz) == ESP_OK) {
        g_config.timezone = tz;
    }

    if (nvs_get_u8(h, NVS_SATA_DISK_COUNT, &g_config.sata_disk_count) != ESP_OK) {
        g_config.sata_disk_count = DEFAULT_SATA_DISK_COUNT;
    }

    if (nvs_get_u8(h, NVS_M2_DISK_COUNT, &g_config.m2_disk_count) != ESP_OK) {
        g_config.m2_disk_count = DEFAULT_M2_DISK_COUNT;
    }

    len = sizeof(g_config.weather_api_key);
    nvs_get_str(h, NVS_WEATHER_API_KEY, g_config.weather_api_key, &len);

    len = sizeof(g_config.weather_city);
    nvs_get_str(h, NVS_WEATHER_CITY, g_config.weather_city, &len);

    uint8_t auto_cycle_en;
    if (nvs_get_u8(h, NVS_AUTO_CYCLE_ENABLE, &auto_cycle_en) == ESP_OK) {
        g_config.auto_cycle_enabled = auto_cycle_en != 0;
    }

    if (nvs_get_u8(h, NVS_AUTO_CYCLE_INTERVAL, &g_config.auto_cycle_interval_sec) != ESP_OK) {
        g_config.auto_cycle_interval_sec = 10;
    }

    uint8_t fan_en;
    if (nvs_get_u8(h, NVS_FAN_ENABLE, &fan_en) == ESP_OK) {
        g_config.fan.enabled = fan_en != 0;
    }

    uint8_t fan_mode;
    if (nvs_get_u8(h, NVS_FAN_MODE, &fan_mode) == ESP_OK) {
        g_config.fan.mode = (FanMode)fan_mode;
    }

    if (nvs_get_u8(h, NVS_FAN_MANUAL, &g_config.fan.manual_pwm_pct) != ESP_OK) {
        g_config.fan.manual_pwm_pct = 50;
    }

    uint8_t fan_tsrc;
    if (nvs_get_u8(h, NVS_FAN_TSRC, &fan_tsrc) == ESP_OK) {
        g_config.fan.temp_source = (TempSource)fan_tsrc;
    }

    if (nvs_get_u8(h, NVS_FAN_HYST, &g_config.fan.hysteresis) != ESP_OK) {
        g_config.fan.hysteresis = FAN_DEFAULT_HYSTERESIS;
    }

    if (nvs_get_u8(h, NVS_FAN_MIN, &g_config.fan.min_pwm_pct) != ESP_OK) {
        g_config.fan.min_pwm_pct = FAN_DEFAULT_MIN_PWM;
    }

    int16_t fan_emerg;
    if (nvs_get_i16(h, NVS_FAN_EMERG, &fan_emerg) == ESP_OK) {
        g_config.fan.emergency_temp = fan_emerg;
    }

    uint16_t fan_ramp;
    if (nvs_get_u16(h, NVS_FAN_RAMP, &fan_ramp) == ESP_OK) {
        g_config.fan.ramp_time_ms = fan_ramp;
    }

    if (nvs_get_u8(h, NVS_FAN_STALL, &g_config.fan.stall_detect_sec) != ESP_OK) {
        g_config.fan.stall_detect_sec = FAN_DEFAULT_STALL_SEC;
    }

    size_t sz = sizeof(FanCurvePoint) * FAN_CURVE_POINTS;
    if (nvs_get_blob(h, NVS_FAN_CURVE, g_config.fan.curve, &sz) != ESP_OK ||
        sz != sizeof(FanCurvePoint) * FAN_CURVE_POINTS) {
        memcpy(g_config.fan.curve, DEFAULT_FAN_CURVE, sizeof(FanCurvePoint) * FAN_CURVE_POINTS);
    }

    nvs_close(h);
}

void config_save(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    nvs_set_str(h, NVS_WIFI_SSID, g_config.ssid);
    nvs_set_str(h, NVS_WIFI_PASS, g_config.wifipass);
    nvs_set_str(h, NVS_NAS_TYPE, g_config.nas_type);
    nvs_set_str(h, NVS_NAS_IP, g_config.nas_ip);
    nvs_set_u16(h, NVS_NAS_PORT, g_config.nas_port);
    nvs_set_str(h, NVS_NAS_USER, g_config.nas_user);
    nvs_set_str(h, NVS_NAS_PASS, g_config.nas_pass);
    nvs_set_u8(h, NVS_NAS_HTTPS, g_config.nas_https ? 1 : 0);
    nvs_set_str(h, NVS_SNMP_COMM, g_config.snmp_comm);
    nvs_set_u8(h, NVS_SNMP_VER, g_config.snmp_ver);
    nvs_set_u32(h, NVS_SERIAL_BAUD, g_config.serial_baud);
    nvs_set_u8(h, NVS_POLL_SEC, g_config.poll_sec);
    nvs_set_u8(h, NVS_ROTATION_ANGLE, g_config.rotation_angle);
    nvs_set_u8(h, NVS_BRIGHTNESS, g_config.brightness);
    nvs_set_u8(h, NVS_AUTODIM, g_config.autodim ? 1 : 0);
    nvs_set_i8(h, NVS_TIMEZONE, g_config.timezone);
    nvs_set_u8(h, NVS_SATA_DISK_COUNT, g_config.sata_disk_count);
    nvs_set_u8(h, NVS_M2_DISK_COUNT, g_config.m2_disk_count);
    nvs_set_str(h, NVS_WEATHER_API_KEY, g_config.weather_api_key);
    nvs_set_str(h, NVS_WEATHER_CITY, g_config.weather_city);
    nvs_set_u8(h, NVS_AUTO_CYCLE_ENABLE, g_config.auto_cycle_enabled ? 1 : 0);
    nvs_set_u8(h, NVS_AUTO_CYCLE_INTERVAL, g_config.auto_cycle_interval_sec);
    nvs_set_u8(h, NVS_FAN_ENABLE, g_config.fan.enabled ? 1 : 0);
    nvs_set_u8(h, NVS_FAN_MODE, (uint8_t)g_config.fan.mode);
    nvs_set_u8(h, NVS_FAN_MANUAL, g_config.fan.manual_pwm_pct);
    nvs_set_u8(h, NVS_FAN_TSRC, (uint8_t)g_config.fan.temp_source);
    nvs_set_u8(h, NVS_FAN_HYST, g_config.fan.hysteresis);
    nvs_set_u8(h, NVS_FAN_MIN, g_config.fan.min_pwm_pct);
    nvs_set_i16(h, NVS_FAN_EMERG, g_config.fan.emergency_temp);
    nvs_set_u16(h, NVS_FAN_RAMP, g_config.fan.ramp_time_ms);
    nvs_set_u8(h, NVS_FAN_STALL, g_config.fan.stall_detect_sec);
    nvs_set_blob(h, NVS_FAN_CURVE, g_config.fan.curve, sizeof(FanCurvePoint) * FAN_CURVE_POINTS);

    nvs_commit(h);
    nvs_close(h);
}

void config_save_wifi(const char* ssid, const char* pass)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    nvs_set_str(h, NVS_WIFI_SSID, ssid);
    if (pass && strlen(pass) > 0) {
        nvs_set_str(h, NVS_WIFI_PASS, pass);
    }

    nvs_commit(h);
    nvs_close(h);

    strlcpy(g_config.ssid, ssid, sizeof(g_config.ssid));
    if (pass && strlen(pass) > 0) {
        strlcpy(g_config.wifipass, pass, sizeof(g_config.wifipass));
    }
}

void config_save_nas(const char* type, const char* ip, uint16_t port,
                     const char* user, const char* pass, bool https)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    nvs_set_str(h, NVS_NAS_TYPE, type);
    nvs_set_str(h, NVS_NAS_IP, ip);
    nvs_set_u16(h, NVS_NAS_PORT, port);
    nvs_set_str(h, NVS_NAS_USER, user);
    nvs_set_str(h, NVS_NAS_PASS, pass);
    nvs_set_u8(h, NVS_NAS_HTTPS, https ? 1 : 0);

    nvs_commit(h);
    nvs_close(h);

    strlcpy(g_config.nas_type, type, sizeof(g_config.nas_type));
    strlcpy(g_config.nas_ip, ip, sizeof(g_config.nas_ip));
    g_config.nas_port = port;
    strlcpy(g_config.nas_user, user, sizeof(g_config.nas_user));
    strlcpy(g_config.nas_pass, pass, sizeof(g_config.nas_pass));
    g_config.nas_https = https;
}

void config_save_display(uint8_t rotation_angle, uint8_t brightness, bool autodim)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    nvs_set_u8(h, NVS_ROTATION_ANGLE, rotation_angle);
    nvs_set_u8(h, NVS_BRIGHTNESS, brightness);
    nvs_set_u8(h, NVS_AUTODIM, autodim ? 1 : 0);

    nvs_commit(h);
    nvs_close(h);

    g_config.rotation_angle = rotation_angle;
    g_config.brightness = brightness;
    g_config.autodim = autodim;
}

void config_save_fan(const FanConfig* fan)
{
    if (!fan) return;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    nvs_set_u8(h, NVS_FAN_ENABLE, fan->enabled ? 1 : 0);
    nvs_set_u8(h, NVS_FAN_MODE, (uint8_t)fan->mode);
    nvs_set_u8(h, NVS_FAN_MANUAL, fan->manual_pwm_pct);
    nvs_set_u8(h, NVS_FAN_TSRC, (uint8_t)fan->temp_source);
    nvs_set_u8(h, NVS_FAN_HYST, fan->hysteresis);
    nvs_set_u8(h, NVS_FAN_MIN, fan->min_pwm_pct);
    nvs_set_i16(h, NVS_FAN_EMERG, fan->emergency_temp);
    nvs_set_u16(h, NVS_FAN_RAMP, fan->ramp_time_ms);
    nvs_set_u8(h, NVS_FAN_STALL, fan->stall_detect_sec);
    nvs_set_blob(h, NVS_FAN_CURVE, fan->curve, sizeof(FanCurvePoint) * FAN_CURVE_POINTS);

    nvs_commit(h);
    nvs_close(h);

    memcpy(&g_config.fan, fan, sizeof(FanConfig));
}

void config_save_disk_config(uint8_t sata_count, uint8_t m2_count)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        return;
    }

    if (sata_count > MAX_DISKS) sata_count = MAX_DISKS;
    if (m2_count > MAX_DISKS) m2_count = MAX_DISKS;
    if (sata_count + m2_count > MAX_DISKS) {
        m2_count = MAX_DISKS - sata_count;
    }

    nvs_set_u8(h, NVS_SATA_DISK_COUNT, sata_count);
    nvs_set_u8(h, NVS_M2_DISK_COUNT, m2_count);

    nvs_commit(h);
    nvs_close(h);

    g_config.sata_disk_count = sata_count;
    g_config.m2_disk_count = m2_count;

    event_bus_publish(EVENT_DISK_CONFIG_CHANGED, NULL, 0);
}

void config_reset(void)
{
    nvs_flash_erase();
    nvs_flash_init();
    config_init_defaults();
}

void config_save_backup(void)
{
    g_config_backup.nas_port = g_config.nas_port;
    g_config_backup.nas_https = g_config.nas_https;
    g_config_backup.snmp_ver = g_config.snmp_ver;
    g_config_backup.serial_baud = g_config.serial_baud;
    strlcpy(g_config_backup.nas_type, g_config.nas_type, sizeof(g_config_backup.nas_type));
    strlcpy(g_config_backup.nas_ip, g_config.nas_ip, sizeof(g_config_backup.nas_ip));
    strlcpy(g_config_backup.nas_user, g_config.nas_user, sizeof(g_config_backup.nas_user));
    strlcpy(g_config_backup.nas_pass, g_config.nas_pass, sizeof(g_config_backup.nas_pass));
    strlcpy(g_config_backup.snmp_comm, g_config.snmp_comm, sizeof(g_config_backup.snmp_comm));
}

void config_restore_backup(void)
{
    g_config.nas_port = g_config_backup.nas_port;
    g_config.nas_https = g_config_backup.nas_https;
    g_config.snmp_ver = g_config_backup.snmp_ver;
    g_config.serial_baud = g_config_backup.serial_baud;
    strlcpy(g_config.nas_type, g_config_backup.nas_type, sizeof(g_config.nas_type));
    strlcpy(g_config.nas_ip, g_config_backup.nas_ip, sizeof(g_config.nas_ip));
    strlcpy(g_config.nas_user, g_config_backup.nas_user, sizeof(g_config.nas_user));
    strlcpy(g_config.nas_pass, g_config_backup.nas_pass, sizeof(g_config.nas_pass));
    strlcpy(g_config.snmp_comm, g_config_backup.snmp_comm, sizeof(g_config.snmp_comm));
    config_save();
}
