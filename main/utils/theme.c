#include "theme.h"
#include "app_cfg.h"

theme_palette_t theme_get(void)
{
    theme_palette_t p;
    switch (g_cfg.theme) {
    case 1:
        p.bg = lv_color_make(0xf0, 0xf0, 0xf4);
        p.text = lv_color_make(0x10, 0x10, 0x18);
        p.menu_surf = lv_color_make(0xe8, 0xe8, 0xee);
        p.menu_hdr = lv_color_make(0xc0, 0xc0, 0xcc);
        p.menu_btn = lv_color_make(0x90, 0x90, 0xa0);
        p.sunmap_water_n = lv_color_make(0xb0, 0xb8, 0xc8);
        p.sunmap_water_d = lv_color_make(0xe0, 0xe4, 0xf0);
        p.sunmap_land_n = lv_color_make(0x60, 0x70, 0x80);
        p.sunmap_land_d = lv_color_make(0x20, 0x30, 0x40);
        break;
    case 2:
        p.bg = lv_color_black();
        p.text = lv_color_make(0xff, 0xff, 0x00);
        p.menu_surf = lv_color_black();
        p.menu_hdr = lv_color_make(0xff, 0xff, 0x00);
        p.menu_btn = lv_color_white();
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x40, 0x40, 0x00);
        p.sunmap_land_n = lv_color_make(0x80, 0x80, 0x00);
        p.sunmap_land_d = lv_color_make(0xff, 0xff, 0x00);
        break;
    default:
        p.bg = lv_color_black();
        p.text = lv_color_white();
        p.menu_surf = lv_color_make(0x20, 0x20, 0x28);
        p.menu_hdr = lv_color_make(0x30, 0x30, 0x3c);
        p.menu_btn = lv_color_make(0x50, 0x50, 0x60);
        p.sunmap_water_n = lv_color_black();
        p.sunmap_water_d = lv_color_make(0x20, 0x20, 0x20);
        p.sunmap_land_n = lv_color_make(0x40, 0x40, 0x40);
        p.sunmap_land_d = lv_color_make(0x90, 0x90, 0x90);
        break;
    }
    return p;
}