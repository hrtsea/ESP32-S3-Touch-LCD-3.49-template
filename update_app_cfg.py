import re

with open('f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/config/app_cfg.c', 'r', encoding='utf-8') as f:
    content = f.read()

content = content.replace('#include "event_bus.h"', '#include "event_bus.h"\n#include "esp_wifi_config.h"\n#include "esp_bus.h"')

old_func = '''void app_cfg_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    /* 暂存凭证，连接成功后再落地到 NVS（避免错误密码被保存） */
    app_cfg_wifi_pending_set(ssid, pass ? pass : "");
    if (s_callbacks.on_wifi_connect) {
        s_callbacks.on_wifi_connect(ssid, pass ? pass : ""); /* 兼容旧回调 */
    }
}'''

new_func = '''void app_cfg_wifi_connect_save(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;
    wifi_cfg_connect(ssid, pass ? pass : "");
    app_cfg_set_last_ssid(ssid);
}'''

content = content.replace(old_func, new_func)

with open('f:/nasmonitor/ESP32-S3-Touch-LCD-3.49-template/main/config/app_cfg.c', 'w', encoding='utf-8') as f:
    f.write(content)

print('app_cfg.c updated successfully')
