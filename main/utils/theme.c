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
        p.ok = lv_color_make(0x00, 0xc8, 0x00);
        p.warn = lv_color_make(0xff, 0xc8, 0x00);
        p.danger = lv_color_make(0xff, 0x30, 0x30);
        p.info = lv_color_make(0x00, 0xc8, 0xff);
        p.dim = lv_color_make(0x80, 0x80, 0x80);
        p.accent = lv_color_hex(0x2BA589);
        p.inactive = lv_color_make(0xcc, 0xcc, 0xcc);
        p.text_dim = lv_color_make(0x60, 0x60, 0x68);
        p.card_bg = lv_color_make(0xe4, 0xe4, 0xea);
        p.panel = lv_color_make(0xd8, 0xd8, 0xde);
        p.grid = lv_color_make(0xb8, 0xb8, 0xc0);
        p.highlight = lv_color_make(0xcc, 0x88, 0x00);
        p.up_accent = lv_color_make(0xd0, 0x90, 0x00);
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
        p.ok = lv_color_make(0x00, 0xff, 0x00);
        p.warn = lv_color_make(0xff, 0xff, 0x00);
        p.danger = lv_color_make(0xff, 0x00, 0x00);
        p.info = lv_color_make(0x00, 0xff, 0xff);
        p.dim = lv_color_make(0x80, 0x80, 0x80);
        p.accent = lv_color_hex(0xFFD700);
        p.inactive = lv_color_make(0x40, 0x40, 0x00);
        p.text_dim = lv_color_make(0x80, 0x80, 0x00);
        p.card_bg = lv_color_make(0x20, 0x20, 0x00);
        p.panel = lv_color_make(0x10, 0x10, 0x00);
        p.grid = lv_color_make(0x60, 0x60, 0x00);
        p.highlight = lv_color_make(0xff, 0xff, 0x00);
        p.up_accent = lv_color_make(0xff, 0xff, 0x00);
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
        p.ok = lv_color_make(0x00, 0xe0, 0x00);
        p.warn = lv_color_make(0xff, 0xe0, 0x00);
        p.danger = lv_color_make(0xf8, 0x00, 0x00);
        p.info = lv_color_make(0x00, 0xe0, 0xff);
        p.dim = lv_color_make(0x42, 0x08, 0x00);
        p.accent = lv_color_hex(0x40E0D0);
        p.inactive = lv_color_make(0x33, 0x33, 0x33);
        p.text_dim = lv_color_make(0xa0, 0xa0, 0xa0);
        p.card_bg = lv_color_make(0x1a, 0x1a, 0x1a);
        p.panel = lv_color_make(0x10, 0x10, 0x10);
        p.grid = lv_color_make(0x30, 0x30, 0x30);
        p.highlight = lv_color_make(0xff, 0xff, 0x00);
        p.up_accent = lv_color_make(0xe8, 0xca, 0x5a);
        break;
    }
    return p;
}