#pragma once

#include "data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

DataSource* netdata_client_create(const DataSourceParams* params);

#ifdef __cplusplus
}
#endif
