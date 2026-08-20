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
#include "esp_log.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "data_source";

const NasTypeEntry NAS_TYPES[] = {
    /* create 字段绑定各 client 的无参构造入口；带参特例（linux_http/windows/fnos）
     * 由对应 client 提供的包装函数固化参数，避免本文件维护 enum→client 的 switch。 */
    {"synology",     "Synology DSM",   NAS_SYNOLOGY,    true, "192.168.1.100", 5000,   "admin", true,  true,  false, false, synology_client_create},
    {"qnap",         "QNAP QTS",       NAS_QNAP,        true, "192.168.1.100", 8080,   "admin", true,  true,  false, false, qnap_client_create},
    {"truenas",      "TrueNAS",        NAS_TRUENAS,     true, "192.168.1.100", 80,     "root",  true,  true,  false, false, truenas_client_create},
    /* FNOS 当前无独立 client，复用 mock client（固定 type/展示名/图标），由 mock_client_fnos_create 包装 */
    {"fnos",         "FNOS",           NAS_FNOS,        true, "192.168.1.100", 3000,   "",      false, true,  false, false, mock_client_fnos_create},
    {"unraid",       "Unraid",         NAS_UNRAID,      true, "192.168.1.100", 80,     "",      true,  false, false, false, unraid_client_create},
    {"netdata",      "Netdata",        NAS_NETDATA,     true, "192.168.1.100", 19999,  "",      false, true,  false, false, netdata_client_create},
    {"snmp",         "SNMP",           NAS_SNMP,        true, "192.168.1.100", 161,    "",      false, false, true,  false, snmp_client_create},
    {"linux_http",   "Linux (HTTP)",   NAS_LINUX_HTTP,  true, "192.168.1.100", 8099,   "",      false, false, false, false, api_client_linux_http_create},
    {"linux_serial", "Linux (Serial)", NAS_LINUX_SERIAL,true, "/dev/ttyUSB0",  115200, "",      false, false, false, true,  serial_client_create},
    {"windows",      "Windows",        NAS_WINDOWS,     true, "192.168.1.100", 0,      "admin", true,  false, false, false, api_client_windows_create},
    {"mock",         "Mock (测试)",    NAS_MOCK,        true, "",              0,      "",      false, false, false, false, mock_client_create},
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
    if (nas_type_id == NULL) return NAS_LINUX_HTTP;
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return NAS_TYPES[i].nas_type_enum;
        }
    }
    return NAS_LINUX_HTTP;
}

static const char* nas_type_to_string(NasType type)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (NAS_TYPES[i].nas_type_enum == type) {
            return NAS_TYPES[i].id;
        }
    }
    return "linux_http";
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

static DataSource* ds_create_by_type(NasType type)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (NAS_TYPES[i].nas_type_enum == type && NAS_TYPES[i].create != NULL) {
            return NAS_TYPES[i].create();
        }
    }
    return NULL;
}

static DataSource* data_source_create(const char* nas_type_id)
{
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(NAS_TYPES[i].id, nas_type_id) == 0) {
            return ds_create_by_type(NAS_TYPES[i].nas_type_enum);
        }
    }

    ESP_LOGW(TAG, "Unsupported type: %s, fallback to mock", nas_type_id);
    return mock_client_create();
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
