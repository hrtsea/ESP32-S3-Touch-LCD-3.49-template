#pragma once

#include "data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    API_IDLE,
    API_REQUESTING,
    API_PARSING,
    API_DONE
} ApiState;

typedef struct {
    NasType current_type;
    char nas_ip[40];
    uint16_t nas_port;
    ApiState state;
    uint32_t poll_interval_ms;
    NasData data;
} ApiClientPriv;

DataSource* api_client_create(NasType type);

#ifdef __cplusplus
}
#endif
