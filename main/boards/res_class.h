/**
 * @file res_class.h
 * @brief 画布分辨率档位定义（board 抽象层）
 *
 * 布局参数按分辨率档位数据化（见 docs/15-多开发板适配重构计划.md 4.4），
 * UI 通过 res_class_get() 判定当前画布属于哪个档位，再由 layout_get()
 * 读取对应档位的布局参数表。
 *
 * 枚举项以"宽×高"分辨率命名，与 board_desc_t.canvas_w/h 一一对应；
 * 新板子若布局参数不同，追加一个枚举项并登记一条布局参数记录即可。
 */
#ifndef BOARDS_RES_CLASS_H
#define BOARDS_RES_CLASS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RES_640X172,   /* 超宽横屏 640×172（当前 3.49" 板，物理面板 172×640） */
    RES_800X480,   /* 横屏 800×480 */
    RES_480X320,   /* 横屏 480×320 */
    RES_240X320,   /* 竖屏 240×320 */
    RES_CLASS_MAX,
} res_class_t;

/** 返回当前板的布局档位（实现随板提供，见 board.c） */
res_class_t res_class_get(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARDS_RES_CLASS_H */
