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

/* 由 data_source 组包连接参数后调用，client 不再依赖 app_cfg */
DataSource* api_client_create(const DataSourceParams* params);

#ifdef __cplusplus
}
#endif
