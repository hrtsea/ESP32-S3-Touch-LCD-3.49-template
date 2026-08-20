#pragma once

#include "data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

DataSource* snmp_client_create(const DataSourceParams* params);

#ifdef __cplusplus
}
#endif
