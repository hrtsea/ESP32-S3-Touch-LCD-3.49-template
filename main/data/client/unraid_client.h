#pragma once

#include "data_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unraid NAS 数据源客户端
 *
 * 通过 Unraid GraphQL API 获取监控数据：
 * - 端点: http://<ip>:<port>/graphql
 * - 认证: x-api-key 请求头（API Key 存储在 g_config.nas_pass 中）
 * - 单次 POST 查询获取 CPU/内存/磁盘/阵列/Docker/VM 等全部数据
 */
DataSource* unraid_client_create(const DataSourceParams* params);

#ifdef __cplusplus
}
#endif
