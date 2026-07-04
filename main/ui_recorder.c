#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "radio.h"
#include "recorder.h"
#include "i18n.h"
#include "ui_recorder.h"

static const char *TAG = "ui_recorder";

/* ---------------------- Recorder tile ---------------------- */

/* Recorder tile widgets. Single full-width view:
   - Top: status text ("Idle" / "Recording  Ns")
   - Middle: big round REC/STOP button
   - Below button: "Recordings ▶" button to open the list overlay
   - Bottom row: stereo VU bars  L |||||||----    ----|||||||| R
   The list of recordings is a separate full-tile overlay shown over
   the parent tile when the user taps "Recordings"; closes on a
   "Back" button. No split screen. */
lv_obj_t *g_rec_tile         = NULL;
lv_obj_t *g_rec_status       = NULL;
lv_obj_t *g_rec_btn_lbl      = NULL;
lv_obj_t *g_rec_vu_l         = NULL;   /* left bar (grows toward left) */
lv_obj_t *g_rec_vu_r         = NULL;   /* right bar (grows toward right) */
lv_obj_t *g_rec_list_overlay = NULL;
lv_obj_t *g_rec_list         = NULL;
lv_obj_t *g_rec_overlay_status = NULL;   /* "Playing ..." banner on the list overlay */
lv_timer_t *g_rec_poll       = NULL;
static int        g_rec_vu_l_smooth = 0;
static int        g_rec_vu_r_smooth = 0;

static void rec_play_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name || !*name) return;
    char path[128];
    snprintf(path, sizeof(path), "file://sdcard/recordings/%s", name);
    if (radio_is_playing()) radio_stop();
    radio_play(path);
}

static void rec_delete_cb(lv_event_t *e)
{
    if (menu_input_blocked()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    recorder_delete(name);
    recorder_refresh_list();
}

void recorder_refresh_list(void)
{
    if (!g_rec_list) return;
    lv_obj_clean(g_rec_list);
    static char names[16][64];
    int n = recorder_list(names, 16);
    if (n == 0) {
        lv_obj_t *l = lv_label_create(g_rec_list);
        lv_label_set_text(l, "(no recordings)");
        lv_obj_set_style_text_color(l, lv_color_make(0xa0, 0xa0, 0xa0), 0);
        lv_obj_set_style_text_font(l, i18n_font(), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        /* Tappable row: tap row body = play; tap trash = delete.
           Filename on the left (big), size/duration beneath (small),
           trash icon on the right. Whole row is itself the click
           target so the touch target is huge and we don't fight LVGL
           about which child eats the click. */
        lv_obj_t *row = lv_btn_create(g_rec_list);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_bg_color(row, lv_color_make(0x18, 0x40, 0x28), 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, rec_play_cb, LV_EVENT_CLICKED,
                            (void *)names[i]);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, names[i]);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *meta = lv_label_create(row);
        uint32_t bytes = 0, dur_ms = 0;
        recorder_file_info(names[i], &bytes, &dur_ms);
        char meta_buf[48];
        if (bytes >= 1024 * 1024) {
            snprintf(meta_buf, sizeof(meta_buf), "%.1f MB  %u.%01us",
                     bytes / (1024.0 * 1024.0),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        } else {
            snprintf(meta_buf, sizeof(meta_buf), "%u KB  %u.%01us",
                     (unsigned)(bytes / 1024),
                     (unsigned)(dur_ms / 1000),
                     (unsigned)((dur_ms / 100) % 10));
        }
        lv_label_set_text(meta, meta_buf);
        lv_obj_set_style_text_color(meta, lv_color_make(0xc0, 0xc0, 0xc0), 0);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
        lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        /* Trash button: anchored right, eats its own click event so the
           row's play handler doesn't also fire on delete. */
        lv_obj_t *del = lv_btn_create(row);
        lv_obj_set_size(del, 40, 32);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(del, lv_color_make(0x80, 0x40, 0x20), 0);
        lv_obj_t *dl = lv_label_create(del);
        lv_label_set_text(dl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(dl, lv_color_white(), 0);
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
        lv_obj_center(dl);
        lv_obj_add_event_cb(del, rec_delete_cb, LV_EVENT_CLICKED,
                            (void *)names[i]);
    }
}

static void rec_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "rec_btn tap: blocked=%d recording=%d",
             (int)menu_input_blocked(), (int)recorder_is_recording());
    if (menu_input_blocked()) return;
    if (recorder_is_recording()) {
        esp_err_t r = recorder_stop();
        ESP_LOGI(TAG, "rec_btn stop -> %s", esp_err_to_name(r));
        recorder_refresh_list();   /* no-op if overlay isn't open */
    } else {
        const char *path = NULL;
        esp_err_t r = recorder_start(&path);
        ESP_LOGI(TAG, "rec_btn start -> %s path=%s",
                 esp_err_to_name(r), path ? path : "(null)");
    }
}

static int peak_to_pct(uint16_t peak)
{
    if (peak == 0) return 0;
    int log2v = 0;
    uint32_t v = peak;
    while (v >>= 1) log2v++;          /* log2(32767) ~= 15 */
    int t = (log2v * 100) / 15;
    return t > 100 ? 100 : t;
}

static void rec_poll_cb(lv_timer_t *t)
{
    (void)t;
    /* Retry monitor start: at boot time the radio engine is warmed
       *after* show_main_ui, so build_recorder_tile's first call to
       recorder_monitor_start() fails (radio engine not yet up). The
       worker is what feeds peak data to the VU; without it the bars
       sit at zero. recorder_monitor_start is idempotent. */
    static bool s_monitor_running = false;
    if (!s_monitor_running) {
        if (recorder_monitor_start() == ESP_OK) s_monitor_running = true;
    }
    bool recording = recorder_is_recording();
    bool playing   = radio_is_playing();
    if (g_rec_btn_lbl) {
        lv_label_set_text(g_rec_btn_lbl, recording ? LV_SYMBOL_STOP : "REC");
    }
    if (g_rec_status) {
        if (recording) {
            lv_label_set_text_fmt(g_rec_status, LV_SYMBOL_AUDIO " REC  %us",
                                  recorder_elapsed_s());
            lv_obj_set_style_text_color(g_rec_status, lv_color_make(0xff, 0x40, 0x40), 0);
        } else if (playing) {
            /* Show "▶ Playing <basename>" so the user can tell that
               radio_play actually fired even if the speaker output
               sounds quiet. The VU bars below also flip to the
               decoder's output level so they reflect playback, not
               the mic. */
            const char *uri = radio_current_uri();
            const char *base = uri ? uri : "stream";
            const char *slash = NULL;
            if (uri) {
                for (const char *p = uri; *p; p++) if (*p == '/') slash = p;
                if (slash) base = slash + 1;
            }
            lv_label_set_text_fmt(g_rec_status, LV_SYMBOL_PLAY " Playing %s", base);
            lv_obj_set_style_text_color(g_rec_status, lv_color_make(0x40, 0xc0, 0xff), 0);
            if (g_rec_overlay_status) {
                lv_label_set_text_fmt(g_rec_overlay_status,
                                      LV_SYMBOL_PLAY " Playing %s", base);
                lv_obj_set_style_text_color(g_rec_overlay_status,
                                            lv_color_make(0x40, 0xc0, 0xff), 0);
            }
        } else {
            lv_label_set_text(g_rec_status, "Idle");
            lv_obj_set_style_text_color(g_rec_status, lv_color_white(), 0);
            if (g_rec_overlay_status) {
                lv_label_set_text(g_rec_overlay_status, "");
            }
        }
    }
    /* Stereo L/R VU. Source flips between mic input and decoder
       output: when playback is active, show the decoded PCM peak so
       the user can confirm the player is actually emitting samples
       (independent of speaker volume); otherwise show mic input from
       the recorder's monitor loop. Always read both peak buffers so
       neither accumulates stale residue across modes. */
    uint16_t mic_l = 0, mic_r = 0;
    uint16_t out_l = 0, out_r = 0;
    recorder_peak_lr(&mic_l, &mic_r);
    radio_out_peak(&out_l, &out_r);
    uint16_t pl = playing ? out_l : mic_l;
    uint16_t pr = playing ? out_r : mic_r;
    int tl = peak_to_pct(pl);
    int tr = peak_to_pct(pr);
    if (tl > g_rec_vu_l_smooth) g_rec_vu_l_smooth = tl;
    else g_rec_vu_l_smooth = (g_rec_vu_l_smooth * 7 + tl) / 8;
    if (tr > g_rec_vu_r_smooth) g_rec_vu_r_smooth = tr;
    else g_rec_vu_r_smooth = (g_rec_vu_r_smooth * 7 + tr) / 8;
    /* Visual hint: tint the indicator blue while playback is active,
       green during normal mic monitoring. */
    if (g_rec_vu_l) {
        lv_obj_set_style_bg_color(g_rec_vu_l,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(g_rec_vu_l, g_rec_vu_l_smooth, LV_ANIM_OFF);
    }
    if (g_rec_vu_r) {
        lv_obj_set_style_bg_color(g_rec_vu_r,
            playing ? lv_color_make(0x40, 0xa0, 0xff)
                    : lv_color_make(0x30, 0xc0, 0x40),
            LV_PART_INDICATOR);
        lv_bar_set_value(g_rec_vu_r, g_rec_vu_r_smooth, LV_ANIM_OFF);
    }
}

static void rec_list_close_cb(lv_event_t *e)
{
    (void)e;
    if (g_rec_list_overlay) {
        lv_obj_del(g_rec_list_overlay);
        g_rec_list_overlay = NULL;
        g_rec_list = NULL;
        g_rec_overlay_status = NULL;
    }
}

static void rec_list_open_cb(lv_event_t *e)
{
    (void)e;
    if (menu_input_blocked()) return;
    if (g_rec_list_overlay) return;
    /* Mount the overlay on the active screen's TOP LAYER so the
       tileview can't capture our taps as start-of-swipe gestures.
       Top layer is above the tileview in the input dispatch order. */
    g_rec_list_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_rec_list_overlay);
    lv_obj_set_size(g_rec_list_overlay, canvas_w, canvas_h);
    lv_obj_align(g_rec_list_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(g_rec_list_overlay, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(g_rec_list_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_rec_list_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(g_rec_list_overlay, 4, 0);

    lv_obj_t *back = lv_btn_create(g_rec_list_overlay);
    lv_obj_set_size(back, 56, 22);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, i18n_font(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, rec_list_close_cb, LV_EVENT_CLICKED, NULL);

    /* Now-playing banner pinned to the right of the Back button so the
       user can see "Playing X" even with the list overlay covering the
       main tile's status row. Updated by rec_poll_cb. */
    g_rec_overlay_status = lv_label_create(g_rec_list_overlay);
    lv_label_set_text(g_rec_overlay_status, "");
    lv_obj_set_style_text_font(g_rec_overlay_status, &lv_font_montserrat_16, 0);
    lv_obj_align(g_rec_overlay_status, LV_ALIGN_TOP_LEFT, 64, 2);
    lv_label_set_long_mode(g_rec_overlay_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(g_rec_overlay_status, canvas_w - 70);

    g_rec_list = lv_obj_create(g_rec_list_overlay);
    lv_obj_remove_style_all(g_rec_list);
    /* Fill the overlay below the back bar (back is 22 px + 4 px pad). */
    lv_obj_set_size(g_rec_list, lv_pct(100), lv_pct(85));
    lv_obj_align(g_rec_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_layout(g_rec_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_rec_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_rec_list, 2, 0);
    lv_obj_set_style_pad_all(g_rec_list, 4, 0);
    lv_obj_set_style_bg_opa(g_rec_list, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(g_rec_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_rec_list, LV_SCROLLBAR_MODE_AUTO);
    recorder_refresh_list();
}

void build_recorder_tile(lv_obj_t *parent)
{
    g_rec_tile = parent;
    lv_obj_set_style_bg_color(parent, lv_color_make(0x10, 0x10, 0x18), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 4, 0);

    /* Tile is 172 px wide (rotated) x 640 px tall in our LVGL coord
       system. Layout from top: status, REC button, recordings button,
       VU at the very bottom. */

    /* The display is rotated landscape: 640 wide x 172 tall. We lay
       the recorder tile out left -> right:
         [ status + Recordings button ]   [ big REC ]   [ L/R VU ]
       Status text is on the left.
       REC button is in the center.
       L/R VU stacked on the right. */

    /* Left column: status + recordings button. */
    g_rec_status = lv_label_create(parent);
    lv_label_set_text(g_rec_status, "Idle");
    lv_obj_set_style_text_color(g_rec_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_rec_status, i18n_font(), 0);
    lv_obj_align(g_rec_status, LV_ALIGN_LEFT_MID, 12, -32);

    lv_obj_t *list_btn = lv_btn_create(parent);
    lv_obj_set_size(list_btn, 150, 36);
    lv_obj_align(list_btn, LV_ALIGN_LEFT_MID, 12, 24);
    lv_obj_set_style_bg_color(list_btn, lv_color_make(0x40, 0x40, 0x60), 0);
    lv_obj_t *lbl = lv_label_create(list_btn);
    lv_label_set_text(lbl, "Recordings " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, i18n_font(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(list_btn, rec_list_open_cb, LV_EVENT_CLICKED, NULL);

    /* Center: big round REC button. Tile is 172 px tall so the button
       can be ~120 px without clipping; height is the constraint. */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 130, 130);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(btn, 65, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xc0, 0x30, 0x30), 0);
    lv_obj_add_event_cb(btn, rec_btn_cb, LV_EVENT_CLICKED, NULL);
    g_rec_btn_lbl = lv_label_create(btn);
    lv_label_set_text(g_rec_btn_lbl, "REC");
    lv_obj_set_style_text_color(g_rec_btn_lbl, lv_color_white(), 0);
    /* User asked for 2x bigger than the default i18n font (~14 px).
       Only Montserrat 12/14/16/48 are compiled in this build, so 48 is
       the closest "much bigger". */
    lv_obj_set_style_text_font(g_rec_btn_lbl, &lv_font_montserrat_48, 0);
    lv_obj_center(g_rec_btn_lbl);

    /* Right side: stereo L/R VU stacked vertically.
         L: ████████░░░░ (top bar)
         R: ████████░░░░ (bottom bar)
       Bars are normal LTR-fill. Width 130 px, fits comfortably in the
       remaining ~150 px on the right. */
    lv_obj_t *l_lbl = lv_label_create(parent);
    lv_label_set_text(l_lbl, "L");
    lv_obj_set_style_text_color(l_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(l_lbl, i18n_font(), 0);
    lv_obj_align(l_lbl, LV_ALIGN_RIGHT_MID, -150, -22);

    g_rec_vu_l = lv_bar_create(parent);
    lv_obj_set_size(g_rec_vu_l, 130, 14);
    lv_obj_align(g_rec_vu_l, LV_ALIGN_RIGHT_MID, -8, -22);
    lv_bar_set_range(g_rec_vu_l, 0, 100);
    lv_obj_set_style_bg_color(g_rec_vu_l, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_bg_color(g_rec_vu_l, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_rec_vu_l, 3, 0);
    lv_obj_set_style_radius(g_rec_vu_l, 3, LV_PART_INDICATOR);

    lv_obj_t *r_lbl = lv_label_create(parent);
    lv_label_set_text(r_lbl, "R");
    lv_obj_set_style_text_color(r_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(r_lbl, i18n_font(), 0);
    lv_obj_align(r_lbl, LV_ALIGN_RIGHT_MID, -150, 22);

    g_rec_vu_r = lv_bar_create(parent);
    lv_obj_set_size(g_rec_vu_r, 130, 14);
    lv_obj_align(g_rec_vu_r, LV_ALIGN_RIGHT_MID, -8, 22);
    lv_bar_set_range(g_rec_vu_r, 0, 100);
    lv_obj_set_style_bg_color(g_rec_vu_r, lv_color_make(0x30, 0x30, 0x30), 0);
    lv_obj_set_style_bg_color(g_rec_vu_r, lv_color_make(0x30, 0xc0, 0x40), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_rec_vu_r, 3, 0);
    lv_obj_set_style_radius(g_rec_vu_r, 3, LV_PART_INDICATOR);

    if (!g_rec_poll) {
        g_rec_poll = lv_timer_create(rec_poll_cb, 100, NULL);
    }
    /* Live VU regardless of recording state. */
    if (recorder_monitor_start() != ESP_OK) {
        ESP_LOGW(TAG, "recorder monitor start failed");
    }
}
