#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nas_data.h"

/* 前向声明：NasTypeEntry.create 字段需要 DataSource*，置于其定义之前 */
typedef struct DataSource DataSource;

#ifdef __cplusplus
extern "C" {
#endif

/* NAS 类型主表：id/展示名/枚举 + 出厂默认连接参数 + 能力开关。
 * 原 NasTypeConfig 配置视图已并入本结构体，get_config 直接返回其指针，
 * 不再单独拷贝构造（消除双结构 + 逐字段拷贝）。 */
typedef struct NasTypeEntry {
    const char* id;          /* 类型唯一标识字符串，用于配置/命令行/UI 持久化 */
    const char* display_name;/* UI 展示名，如 "Synology DSM" */
    NasType nas_type_enum;   /* 对应的协议枚举值，标识 NAS 类型 */
    bool implemented;        /* 是否已实现对应 client，false 表示占位未实现 */
    const char* default_ip;  /* 出厂默认 IP，首次配置占位 */
    uint32_t default_port;   /* 出厂默认端口，决定使用哪个协议端口 */
    const char* default_user;/* 出厂默认用户名，连接时预填 */
    bool need_password;      /* 该类型连接是否需要口令 */
    bool need_apiurl;        /* 是否需要填写 API 基址（如 HTTP API） */
    bool need_snmp;          /* 是否走 SNMP 采集（需要 community 等） */
    bool need_serial;        /* 是否走串口采集（如 Linux 串口设备） */
    DataSource* (*create)(void); /* 构造对应 client 实例；消除 ds_create_by_type 的 switch 与表的重复映射 */
} NasTypeEntry;

extern const NasTypeEntry NAS_TYPES[];
extern const int DATA_TYPE_COUNT;

typedef struct DataSourceVTable {
    bool (*init)(DataSource* self);
    bool (*connect)(DataSource* self);
    void (*disconnect)(DataSource* self);
    bool (*poll)(DataSource* self);
    bool (*is_connected)(DataSource* self);
    const NasData* (*get_data)(DataSource* self);
    const char* (*get_type_name)(DataSource* self);
    const char* (*get_conn_icon)(DataSource* self);
    const NasTypeEntry* (*get_config)(DataSource* self);
    void (*destroy)(DataSource* self);
} DataSourceVTable;

struct DataSource {
    const DataSourceVTable* vtable;
    NasData data;
    uint32_t last_poll_ms;
    uint8_t consecutive_failures;
    void* priv;
};

static inline bool ds_init(DataSource* self) {
    return self->vtable->init(self);
}
static inline bool ds_connect(DataSource* self) {
    return self->vtable->connect(self);
}
static inline void ds_disconnect(DataSource* self) {
    self->vtable->disconnect(self);
}
static inline bool ds_poll(DataSource* self) {
    return self->vtable->poll(self);
}
static inline bool ds_is_connected(DataSource* self) {
    return self->vtable->is_connected(self);
}
static inline const NasData* ds_get_data(DataSource* self) {
    return self->vtable->get_data(self);
}
static inline const char* ds_get_type_name(DataSource* self) {
    return self->vtable->get_type_name(self);
}
static inline const char* ds_get_conn_icon(DataSource* self) {
    return self->vtable->get_conn_icon(self);
}
static inline const NasTypeEntry* ds_get_config(DataSource* self) {
    return self->vtable->get_config(self);
}
static inline void ds_destroy(DataSource* self) {
    self->vtable->destroy(self);
}

void data_source_disconnect(void);
bool data_source_poll(void);
bool data_source_is_connected(void);
const NasData* data_source_get_data(void);
const char* data_source_get_type_name(void);
const char* data_source_get_conn_icon(void);
/* 统一数据源入口：把当前数据源类型设置为指定值（创建/切换/清空）。
 * 已是同类型则 no-op；不同类型则销毁旧的建新的；none/空则清空。
 * 连接自愈交给 client 的 poll()，此处不显式 connect。 */
bool data_source_set_type(const char* nas_type_id);
/* 外部批量操作锁：对"抓数+发布"等需整体持锁的序列使用（递归锁，可嵌套） */
void data_source_lock(void);
void data_source_unlock(void);

NasType nas_type_from_string(const char* nas_type_id);
const NasTypeEntry* nas_type_config_get_defaults(NasType type);

/* 协议默认端口与参数（原位于已废弃的 config.h，迁移至此） */
#define DEFAULT_HTTP_PORT      8099
#define DEFAULT_SYNOLOGY_PORT  5000
#define DEFAULT_QNAP_PORT      8080
#define DEFAULT_TRUENAS_PORT   80
#define DEFAULT_NETDATA_PORT   19999
#define DEFAULT_SNMP_PORT      161
#define DEFAULT_SERIAL_BAUD    115200
#define DEFAULT_POLL_SEC       5
#define MIN_POLL_SEC           1
#define MAX_POLL_SEC           30
#define DEFAULT_SATA_DISK_COUNT     6
#define DEFAULT_M2_DISK_COUNT       3

#ifdef __cplusplus
}
#endif
