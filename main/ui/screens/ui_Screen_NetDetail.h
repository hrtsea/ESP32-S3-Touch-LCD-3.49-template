#ifndef UI_SCREEN_NETDETAIL_H
#define UI_SCREEN_NETDETAIL_H

#include <lvgl.h>
#include "../config/app_info.h"
#include "../../data/nas_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_obj_t *ui_Screen_NetDetail;

void ui_Screen_NetDetail_screen_init(void);
void ui_Screen_NetDetail_screen_destroy(void);

void netdetail_screen_update_time(const char *time_str);
void netdetail_screen_update_network(uint32_t tx_bps, uint32_t rx_bps);
void netdetail_screen_update_ip(const char *ip_str);
void netdetail_screen_update_wifi(bool connected);

/* 更新网口详情卡片（接口信息数组及其数量） */
void netdetail_screen_update_interfaces(const NasInterfaceInfo *interfaces, int count);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_NETDETAIL_H */
