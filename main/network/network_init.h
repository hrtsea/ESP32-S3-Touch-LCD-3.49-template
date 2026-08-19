/**
 * @file network_init.h
 * @brief 网络子系统初始化（WiFi 配置 / 桥接 / WebUI）
 *
 * 从 main.cpp 中拆出，归 network 层内聚；main 仅负责编排调用。
 */
#ifndef NETWORK_INIT_H
#define NETWORK_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

void network_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_INIT_H */
