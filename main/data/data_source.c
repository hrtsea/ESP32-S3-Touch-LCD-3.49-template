#include "data_source.h"
#include "client/mock_client.h"
#include "client/api_client.h"
#include "client/synology_client.h"
#include "client/netdata_client.h"
#include "client/truenas_client.h"
#include "client/qnap_client.h"
#include "client/serial_client.h"
#include "client/snmp_client.h"
#include "client/unraid_client.h"
#include "config/app_cfg.h"
#include "esp_log.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "data_source";

const NasTypeEntry NAS_TYPES[] = {
    /* create 字段绑定各 client 的带参构造入口 (const DataSourceParams*)；
     * 由 data_source 组包（NAS_TYPES 出厂默认 + g_cfg 运行时覆盖）后传入，client 不再依赖 app_cfg。 */
    {"synology",     "Synology DSM",   NAS_SYNOLOGY,    true, "192.168.1.100", 5000,   "admin", true,  true,  false, false, synology_client_create,     false, NULL,    0,      10},
    {"qnap",         "QNAP QTS",       NAS_QNAP,        true, "192.168.1.100", 8080,   "admin", true,  true,  false, false, qnap_client_create,         false, NULL,    0,      10},
    {"truenas",      "TrueNAS",        NAS_TRUENAS,     true, "192.168.1.100", 80,     "root",  true,  true,  false, false, truenas_client_create,      false, NULL,    0,      10},
    /* FNOS 当前无独立 client，复用 mock client（固定 type/展示名/图标） */
    {"fnos",         "FNOS",           NAS_FNOS,        true, "192.168.1.100", 3000,   "",      false, true,  false, false, mock_client_create,         false, NULL,    0,      10},
    {"unraid",       "Unraid",         NAS_UNRAID,      true, "192.168.1.100", 80,     "",      true,  false, false, false, unraid_client_create,        false, NULL,    0,      10},
    {"netdata",      "Netdata",        NAS_NETDATA,     true, "192.168.1.100", 19999,  "",      false, true,  false, false, netdata_client_create,      false, NULL,    0,      10},
    {"snmp",         "SNMP",           NAS_SNMP,        true, "192.168.1.100", 161,    "",      false, false, true,  false, snmp_client_create,         false, "public", 0,      10},
    {"linux_http",   "Linux (HTTP)",   NAS_LINUX_HTTP,  true, "192.168.1.100", 8099,   "",      false, false, false, false, api_client_create,          false, NULL,    0,      10},
    {"linux_serial", "Linux (Serial)", NAS_LINUX_SERIAL,true, "/dev/ttyUSB0",  115200, "",      false, false, false, true,  serial_client_create,      false, NULL,    115200, 10},
    {"windows",      "Windows",        NAS_WINDOWS,     true, "192.168.1.100", 0,      "admin", true,  false, false, false, api_client_create,          false, NULL,    0,      10},
    {"mock",         "Mock (测试)",    NAS_MOCK,        true, "",              0,      "",      false, false, false, false, mock_client_create,         false, NULL,    0,      10},
};

const int DATA_TYPE_COUNT = sizeof(NAS_TYPES) / sizeof(NAS_TYPES[0]);

const NasTypeEntry* nas_type_config_get_defaults(NasType type)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (NAS_TYPES[i].nas_type_enum == type) {
            return &NAS_TYPES[i];
        }
    }
    return NULL;
}

static const char* get_display_type_name(const char* nas_type_id)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return NAS_TYPES[i].display_name;
        }
    }
    return "Unknown";
}

NasType nas_type_from_string(const char* nas_type_id)
{
    if (nas_type_id == NULL) return NAS_TYPE_ENUM_COUNT;
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return NAS_TYPES[i].nas_type_enum;
        }
    }
    return NAS_TYPE_ENUM_COUNT;
}

static DataSource* g_data_source = NULL;

/* ---------------- 数据源访问互斥锁（递归互斥量） ----------------
 * 保护所有公开 API 对 g_data_source 及其 client 实例的访问，
 * 使多调用方（事件循环任务 poll、UI 线程 switch）串行化操作数据源。
 *
 * 递归特性：事件循环任务经 data_source_lock() 对"抓数+发布"整体持锁，
 * 期间内部调用的 poll/get_data 等 API 再次持锁（递归重入）不会死锁；
 * get_data 返回的指针在外部锁释放前不会被其它线程的 switch 销毁。
 *
 * 懒初始化：首次调用时创建，此时通常处于启动单线程阶段，无创建竞争。 */
static SemaphoreHandle_t s_ds_mutex = NULL;

static void ds_lock(void)
{
    if (s_ds_mutex == NULL) {
        s_ds_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_ds_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create data source mutex");
            return; /* 极端内存不足：降级为无锁，不阻塞调用方 */
        }
    }
    xSemaphoreTakeRecursive(s_ds_mutex, portMAX_DELAY);
}

static void ds_unlock(void)
{
    if (s_ds_mutex != NULL) {
        xSemaphoreGiveRecursive(s_ds_mutex);
    }
}

static DataSource* ds_create_by_entry(const NasTypeEntry* e)
{
    if (e == NULL || e->create == NULL) return NULL;

    /* 组包：NAS_TYPES 出厂默认 作为基础，g_cfg 运行时值 非0/非空 时覆盖。
     * 这就是原来散落在各 client init 里的 "app_cfg_get_* 回退 defaults" 逻辑，
     * 现统一上提到 data_source，client 不再接触 app_cfg。 */
    DataSourceParams p;
    memset(&p, 0, sizeof(p));
    p.entry          = e;
    strncpy(p.nas_ip,   e->default_ip,   sizeof(p.nas_ip)   - 1);
    p.nas_port        = e->default_port;
    strncpy(p.nas_user, e->default_user, sizeof(p.nas_user) - 1);
    p.use_https        = e->default_https;
    p.serial_baud      = e->default_serial_baud;
    p.poll_sec         = e->default_poll_sec;

    const char* ip   = app_cfg_get_nas_ip();
    int         port = app_cfg_get_nas_port();
    const char* user = app_cfg_get_nas_user();
    const char* pass = app_cfg_get_nas_pass();
    int         https= app_cfg_get_nas_https();
    const char* comm = app_cfg_get_snmp_comm();
    int         baud = app_cfg_get_serial_baud();
    int         poll = app_cfg_get_poll_sec();
    int         sata = app_cfg_get_sata_disk_count();
    int         m2   = app_cfg_get_m2_disk_count();
    int         snmp_ver = app_cfg_get_snmp_ver();

    if (ip   && ip[0])   strncpy(p.nas_ip,   ip,   sizeof(p.nas_ip)   - 1);
    if (port > 0)        p.nas_port        = (uint16_t)port;
    if (user && user[0]) strncpy(p.nas_user, user, sizeof(p.nas_user) - 1);
    if (pass && pass[0]) strncpy(p.nas_pass, pass, sizeof(p.nas_pass) - 1);
    if (https)           p.use_https        = true;
    if (comm && comm[0]) strncpy(p.snmp_comm, comm, sizeof(p.snmp_comm) - 1);
    if (baud > 0)        p.serial_baud      = (uint32_t)baud;
    if (snmp_ver > 0)    p.snmp_ver         = (uint8_t)snmp_ver;
    if (poll > 0)        p.poll_sec         = (uint8_t)poll;
    if (sata > 0)        p.sata_disk_count  = (uint8_t)sata;
    if (m2   > 0)        p.m2_disk_count    = (uint8_t)m2;

    DataSource* ds = e->create(&p);
    if (ds) ds->poll_interval_ms = (uint32_t)p.poll_sec * 1000u;
    return ds;
}

static DataSource* data_source_create(const char* nas_type_id)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return ds_create_by_entry(&NAS_TYPES[i]);
        }
    }

    /* 安全网：出厂默认配置损坏/未知类型时回退 mock，保证设备始终有数据源可运行 */
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, "mock") == 0) {
            ESP_LOGW(TAG, "Unsupported type: %s, fallback to mock", nas_type_id);
            return ds_create_by_entry(&NAS_TYPES[i]);
        }
    }
    return NULL;
}

static bool ds_create_and_init(const char* nas_type_id)
{
    g_data_source = data_source_create(nas_type_id);
    if (g_data_source == NULL) {
        ESP_LOGE(TAG, "Failed to create data source for type: %s", nas_type_id);
        return false;
    }

    if (!ds_init(g_data_source)) {
        ESP_LOGE(TAG, "Failed to init data source");
        ds_destroy(g_data_source);
        g_data_source = NULL;
        return false;
    }

    return true;
}

/* 统一数据源入口：创建/切换/清空都走这里，消除 init 与 switch 双入口。
 * - 已是同类型：no-op 返回 true
 * - 不同类型或当前为 NULL：销毁旧的、建新的、init（连接自愈交给 poll，不显式 connect）
 * - none/空：销毁当前置 NULL 返回 true
 * 连接失败非致命：对象已建好，poll 周期会自动重连。 */
bool data_source_set_type(const char* nas_type_id)
{
    ds_lock();

    bool is_clear = (nas_type_id == NULL || strlen(nas_type_id) == 0 ||
                     strcmp(nas_type_id, "none") == 0);

    if (!is_clear && g_data_source != NULL &&
        strcmp(ds_get_type_name(g_data_source), nas_type_id) == 0) {
        ESP_LOGI(TAG, "Data source already %s, no-op", nas_type_id);
        ds_unlock();
        return true;
    }

    if (g_data_source != NULL) {
        ESP_LOGI(TAG, "Tearing down existing data source (%s)",
                 ds_get_type_name(g_data_source));
        ds_disconnect(g_data_source);
        ds_destroy(g_data_source);
        g_data_source = NULL;
    }

    if (is_clear) {
        ESP_LOGI(TAG, "Data source cleared (no type specified)");
        ds_unlock();
        return true;
    }

    ESP_LOGI(TAG, "Creating data source for type: %s", nas_type_id);
    if (!ds_create_and_init(nas_type_id)) {
        ds_unlock();
        return false;
    }
    /* 不在此 connect：连接自愈内聚在各 client 的 poll() 中 */

    ds_unlock();
    return true;
}

void data_source_disconnect(void)
{
    ds_lock();
    if (g_data_source != NULL) ds_disconnect(g_data_source);
    ds_unlock();
}

bool data_source_poll(void)
{
    bool result = false;
    ds_lock();
    if (g_data_source != NULL) result = ds_poll(g_data_source);
    ds_unlock();
    return result;
}

bool data_source_is_connected(void)
{
    bool result = false;
    ds_lock();
    if (g_data_source != NULL) result = ds_is_connected(g_data_source);
    ds_unlock();
    return result;
}

const NasData* data_source_get_data(void)
{
    const NasData* result = NULL;
    ds_lock();
    if (g_data_source != NULL) result = ds_get_data(g_data_source);
    ds_unlock();
    return result;
}

const char* data_source_get_type_name(void)
{
    const char* result = "None";
    ds_lock();
    if (g_data_source != NULL) result = ds_get_type_name(g_data_source);
    ds_unlock();
    return result;
}

const char* data_source_get_conn_icon(void)
{
    const char* result = "none";
    ds_lock();
    if (g_data_source != NULL) result = ds_get_conn_icon(g_data_source);
    ds_unlock();
    return result;
}

/* 外部批量操作锁：事件循环任务在"抓数+发布"序列外整体持锁，
 * 保证 get_data 返回的指针在其间不被其它线程（如 UI 设置页）
 * 的 data_source_switch 销毁。与内部 ds_lock 为同一递归锁，嵌套调用安全。 */
void data_source_lock(void)
{
    ds_lock();
}

void data_source_unlock(void)
{
    ds_unlock();
}
