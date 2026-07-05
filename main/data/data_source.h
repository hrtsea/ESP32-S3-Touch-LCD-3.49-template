#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nas_data.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NasTypeConfig {
    NasType type;
    const char* default_ip;
    uint32_t default_port;
    const char* default_user;
    bool need_password;
    bool need_apiurl;
    bool need_snmp;
    bool need_serial;
} NasTypeConfig;

typedef struct NasTypeEntry {
    const char* id;
    const char* display_name;
    NasType nas_type_enum;
    bool implemented;
} NasTypeEntry;

extern const NasTypeEntry NAS_TYPES[];
extern const int DATA_TYPE_COUNT;

typedef struct DataSource DataSource;

typedef struct DataSourceVTable {
    bool (*init)(DataSource* self);
    bool (*connect)(DataSource* self);
    void (*disconnect)(DataSource* self);
    bool (*poll)(DataSource* self);
    bool (*is_connected)(DataSource* self);
    const NasData* (*get_data)(DataSource* self);
    const char* (*get_type_name)(DataSource* self);
    const char* (*get_conn_icon)(DataSource* self);
    NasTypeConfig (*get_config)(DataSource* self);
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
static inline NasTypeConfig ds_get_config(DataSource* self) {
    return self->vtable->get_config(self);
}
static inline void ds_destroy(DataSource* self) {
    self->vtable->destroy(self);
}

DataSource* data_source_create(const char* nas_type_id);
bool data_source_init(const char* nas_type_id);
bool data_source_connect(void);
void data_source_disconnect(void);
bool data_source_poll(void);
bool data_source_is_connected(void);
const NasData* data_source_get_data(void);
const char* data_source_get_type_name(void);
const char* data_source_get_conn_icon(void);
bool data_source_switch(const char* nas_type_id);

float data_source_get_rx_speed_mbps(void);
float data_source_get_tx_speed_mbps(void);

const char* get_display_type_name(const char* nas_type_id);
NasType nas_type_from_string(const char* nas_type_id);
const char* nas_type_to_string(NasType type);
NasTypeConfig nas_type_config_get_defaults(NasType type);
NasTypeConfig nas_type_config_get_defaults_by_id(const char* type_id);

#ifdef __cplusplus
}
#endif
