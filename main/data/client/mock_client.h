#pragma once

#include "data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

DataSource* mock_client_create(void);
DataSource* mock_client_create_with_type(NasType type, const char* type_name, const char* conn_icon);

#ifdef __cplusplus
}
#endif
