#include "ui_audio_test.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_codec_dev.h"

#include "ui_common.h"
#include "ui_main.h"
#include "i18n.h"
#include "radio.h"
#include "recorder.h"
#include "app_cfg.h"

static const char *TAG = "ui_audio_test";

extern const uint8_t music_pcm_start[] asm("_binary_canon_pcm_start");
extern const uint8_t music_pcm_end[]   asm("_binary_canon_pcm_end");

typedef enum {
    AUDIO_TEST_IDLE = 0,
    AUDIO_TEST_RECORDING,
    AUDIO_TEST_PLAYING,
    AUDIO_TEST_PLAYING_MUSIC,
} audio_test_state_t;

static lv_obj_t *g_status_lbl = NULL;
static lv_obj_t *g_status_en_lbl = NULL;
static lv_obj_t *g_btn_record = NULL;
static lv_obj_t *g_btn_play = NULL;
static lv_obj_t *g_btn_music = NULL;
static lv_obj_t *g_btn_stop = NULL;

static volatile audio_test_state_t g_state = AUDIO_TEST_IDLE;
static volatile bool g_stop_req = false;
static TaskHandle_t g_worker_task = NULL;
static uint8_t *g_rec_buffer = NULL;
static uint32_t g_rec_bytes = 0;

#define REC_BUFFER_SIZE  (192 * 1024)
#define REC_SAMPLE_RATE  24000
#define REC_BITS         16
#define REC_CHANNELS     2
#define MUSIC_SAMPLE_RATE  24000
#define MUSIC_BITS         16
#define MUSIC_CHANNELS     2

/* In TDM mode both TX (play) and RX (record) share the same bit/frame
   clock, so their sample rates must always match.  This helper switches
   both codec devices to the given rate, and restores both afterwards
   by re-reading the current radio rate. */
static void audio_test_set_rate(uint32_t rate, uint8_t bits, uint8_t ch)
{
    esp_codec_dev_handle_t play_dev =
        (esp_codec_dev_handle_t)radio_get_play_dev();
    esp_codec_dev_handle_t rec_dev  =
        (esp_codec_dev_handle_t)radio_get_record_dev();
    if (play_dev) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate     = rate,
            .bits_per_sample = bits,
            .channel         = ch,
        };
        esp_codec_dev_close(play_dev);
        esp_codec_dev_open(play_dev, &fs);
        esp_codec_dev_set_out_vol(play_dev, radio_get_volume());
    }
    if (rec_dev) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate     = rate,
            .bits_per_sample = bits,
            .channel         = ch,
        };
        esp_codec_dev_close(rec_dev);
        esp_codec_dev_open(rec_dev, &fs);
        esp_codec_dev_set_in_gain(rec_dev, 35.0f);
    }
}

static void audio_test_restore_rate(void)
{
    uint32_t rate = radio_get_sample_rate();
    esp_codec_dev_handle_t play_dev =
        (esp_codec_dev_handle_t)radio_get_play_dev();
    esp_codec_dev_handle_t rec_dev  =
        (esp_codec_dev_handle_t)radio_get_record_dev();
    if (play_dev) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate     = rate,
            .bits_per_sample = 16,
            .channel         = 2,
        };
        esp_codec_dev_close(play_dev);
        esp_codec_dev_open(play_dev, &fs);
        esp_codec_dev_set_out_vol(play_dev, radio_get_volume());
    }
    if (rec_dev) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate     = rate,
            .bits_per_sample = 16,
            .channel         = 2,
        };
        esp_codec_dev_close(rec_dev);
        esp_codec_dev_open(rec_dev, &fs);
        esp_codec_dev_set_in_gain(rec_dev, 42.0f);
    }
}

static void set_status(const char *cn, const char *en)
{
    if (g_status_lbl) lv_label_set_text(g_status_lbl, cn);
    if (g_status_en_lbl) lv_label_set_text(g_status_en_lbl, en);
}

static void update_button_states(void)
{
    bool idle = (g_state == AUDIO_TEST_IDLE);
    bool recording = (g_state == AUDIO_TEST_RECORDING);
    bool playing = (g_state == AUDIO_TEST_PLAYING || g_state == AUDIO_TEST_PLAYING_MUSIC);

    if (g_btn_record) {
        lv_obj_set_style_bg_opa(g_btn_record, idle ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_clear_flag(g_btn_record, LV_OBJ_FLAG_CLICKABLE);
        if (idle) lv_obj_add_flag(g_btn_record, LV_OBJ_FLAG_CLICKABLE);
    }
    if (g_btn_play) {
        lv_obj_set_style_bg_opa(g_btn_play, idle && g_rec_bytes > 0 ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_clear_flag(g_btn_play, LV_OBJ_FLAG_CLICKABLE);
        if (idle && g_rec_bytes > 0) lv_obj_add_flag(g_btn_play, LV_OBJ_FLAG_CLICKABLE);
    }
    if (g_btn_music) {
        lv_obj_set_style_bg_opa(g_btn_music, idle ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_clear_flag(g_btn_music, LV_OBJ_FLAG_CLICKABLE);
        if (idle) lv_obj_add_flag(g_btn_music, LV_OBJ_FLAG_CLICKABLE);
    }
    if (g_btn_stop) {
        lv_obj_set_style_bg_opa(g_btn_stop, (recording || playing) ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_clear_flag(g_btn_stop, LV_OBJ_FLAG_CLICKABLE);
        if (recording || playing) lv_obj_add_flag(g_btn_stop, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void record_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_state != AUDIO_TEST_IDLE) return;
    if (!g_cfg.audio_enable) return;

    if (!g_rec_buffer) {
        g_rec_buffer = heap_caps_malloc(REC_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!g_rec_buffer) {
            ESP_LOGE(TAG, "no PSRAM for record buffer");
            return;
        }
    }

    g_state = AUDIO_TEST_RECORDING;
    g_rec_bytes = 0;
    set_status("正在录音", "Recording...");
    update_button_states();

    if (g_worker_task) xTaskNotifyGive(g_worker_task);
}

static void play_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_state != AUDIO_TEST_IDLE) return;
    if (g_rec_bytes == 0) return;
    if (!g_cfg.audio_enable) return;

    g_state = AUDIO_TEST_PLAYING;
    set_status("正在播放", "Playing...");
    update_button_states();

    if (g_worker_task) xTaskNotifyGive(g_worker_task);
}

static void music_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_state != AUDIO_TEST_IDLE) return;
    if (!g_cfg.audio_enable) return;

    g_state = AUDIO_TEST_PLAYING_MUSIC;
    set_status("正在播放音乐", "Play Music");
    update_button_states();

    if (g_worker_task) xTaskNotifyGive(g_worker_task);
}

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;
    if (g_state == AUDIO_TEST_IDLE) return;

    g_stop_req = true;
    if (g_state == AUDIO_TEST_RECORDING) {
        recorder_stop();
    }
}

static void audio_test_worker(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "audio_test_worker started");

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_stop_req = false;

        if (g_state == AUDIO_TEST_RECORDING) {
            ESP_LOGI(TAG, "start recording test");
            /* Stop radio playback to avoid echo through the mic. */
            bool was_playing = radio_is_playing();
            if (was_playing) {
                radio_stop();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            /* Switch both TX and RX to the test rate (TDM shared clock). */
            audio_test_set_rate(REC_SAMPLE_RATE, REC_BITS, REC_CHANNELS);
            uint32_t total = 0;
            uint32_t chunk = 4096;
            esp_codec_dev_handle_t rec_dev = (esp_codec_dev_handle_t)radio_get_record_dev();
            if (rec_dev) {
                while (g_state == AUDIO_TEST_RECORDING && !g_stop_req && total + chunk <= REC_BUFFER_SIZE) {
                    int rd = esp_codec_dev_read(rec_dev, g_rec_buffer + total, chunk);
                    if (rd > 0) total += rd;
                    else vTaskDelay(pdMS_TO_TICKS(5));
                }
            }
            g_rec_bytes = total;
            ESP_LOGI(TAG, "recorded %u bytes", total);
            /* Restore both codecs to the radio's current rate. */
            audio_test_restore_rate();

            if (lvgl_lock(50)) {
                g_state = AUDIO_TEST_IDLE;
                g_stop_req = false;
                set_status("录音完成", "Rec Done");
                update_button_states();
                lvgl_unlock();
            }

        } else if (g_state == AUDIO_TEST_PLAYING) {
            ESP_LOGI(TAG, "start playback test, %u bytes", g_rec_bytes);
            /* Stop the radio player pipeline before reconfiguring the codec. */
            bool was_playing = radio_is_playing();
            if (was_playing) {
                radio_stop();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            /* Switch both TX and RX to the test rate (TDM shared clock). */
            audio_test_set_rate(REC_SAMPLE_RATE, REC_BITS, REC_CHANNELS);
            uint32_t offset = 0;
            uint32_t chunk = 4096;
            esp_codec_dev_handle_t play_dev = (esp_codec_dev_handle_t)radio_get_play_dev();
            if (play_dev) {
                while (g_state == AUDIO_TEST_PLAYING && !g_stop_req && offset < g_rec_bytes) {
                    uint32_t sz = (offset + chunk > g_rec_bytes) ? (g_rec_bytes - offset) : chunk;
                    esp_codec_dev_write(play_dev, (void *)(g_rec_buffer + offset), sz);
                    offset += sz;
                }
            }
            /* Restore both codecs to the radio's current rate. */
            audio_test_restore_rate();
            ESP_LOGI(TAG, "playback done, %u bytes", offset);

            if (lvgl_lock(50)) {
                g_state = AUDIO_TEST_IDLE;
                g_stop_req = false;
                set_status("播放完成", "Play Done");
                update_button_states();
                lvgl_unlock();
            }

        } else if (g_state == AUDIO_TEST_PLAYING_MUSIC) {
            ESP_LOGI(TAG, "start music playback test");
            /* Stop the radio player pipeline first so it stops touching
               the codec; we then reconfigure the codec for our PCM and
               restore the radio default afterwards. This avoids the
               "esp_gmf_pipeline_stop: Got NULL Pointer" crash that
               happens when we yank the codec out from under the player. */
            bool was_playing = radio_is_playing();
            if (was_playing) {
                radio_stop();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            /* Switch both TX and RX to the test rate (TDM shared clock). */
            audio_test_set_rate(MUSIC_SAMPLE_RATE, MUSIC_BITS, MUSIC_CHANNELS);
            esp_codec_dev_handle_t play_dev = (esp_codec_dev_handle_t)radio_get_play_dev();
            if (play_dev) {
                esp_codec_dev_set_out_vol(play_dev, 90);
                size_t total = music_pcm_end - music_pcm_start;
                const uint8_t *data_ptr = music_pcm_start;
                size_t bytes_write = 0;
                uint32_t chunk = 1024;
                ESP_LOGI(TAG, "playing %u bytes of PCM", (unsigned)total);
                while (bytes_write < total && !g_stop_req) {
                    size_t sz = (bytes_write + chunk > total) ? (total - bytes_write) : chunk;
                    esp_codec_dev_write(play_dev, (void *)(data_ptr + bytes_write), sz);
                    bytes_write += sz;
                }
                ESP_LOGI(TAG, "music playback done, %u bytes", (unsigned)bytes_write);
            }
            /* Restore both codecs to the radio's current rate. */
            audio_test_restore_rate();

            if (lvgl_lock(50)) {
                g_state = AUDIO_TEST_IDLE;
                g_stop_req = false;
                set_status("播放完成", "Play Done");
                update_button_states();
                lvgl_unlock();
            }
        }
    }
}

void build_audio_test_tile(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Audio Test");
    lv_obj_set_style_text_color(title, lv_color_make(0x0f, 0x90, 0x8e), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    g_status_lbl = lv_label_create(parent);
    lv_label_set_text(g_status_lbl, "等待操作");
    lv_obj_set_style_text_color(g_status_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_status_lbl, i18n_font(), 0);
    lv_obj_set_style_text_align(g_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_status_lbl, LV_ALIGN_TOP_MID, 0, 34);

    g_status_en_lbl = lv_label_create(parent);
    lv_label_set_text(g_status_en_lbl, "Idle");
    lv_obj_set_style_text_color(g_status_en_lbl, lv_color_make(0xa0, 0xa0, 0xa0), 0);
    lv_obj_set_style_text_font(g_status_en_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(g_status_en_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_status_en_lbl, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *btn_row = lv_obj_create(parent);
    lv_obj_set_size(btn_row, disp_driver_get_canvas_w() - 32, 56);
    lv_obj_set_style_bg_opa(btn_row, 0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_btn_record = lv_btn_create(btn_row);
    lv_obj_set_size(g_btn_record, 72, 44);
    lv_obj_set_style_radius(g_btn_record, 8, 0);
    lv_obj_set_style_bg_color(g_btn_record, lv_color_make(0x0E, 0x6B, 0x03), 0);
    lv_obj_add_event_cb(g_btn_record, record_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rec_lbl = lv_label_create(g_btn_record);
    lv_label_set_text(rec_lbl, "REC");
    lv_obj_set_style_text_color(rec_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rec_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(rec_lbl);

    g_btn_play = lv_btn_create(btn_row);
    lv_obj_set_size(g_btn_play, 72, 44);
    lv_obj_set_style_radius(g_btn_play, 8, 0);
    lv_obj_set_style_bg_color(g_btn_play, lv_color_make(0x03, 0x26, 0x6B), 0);
    lv_obj_add_event_cb(g_btn_play, play_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_lbl = lv_label_create(g_btn_play);
    lv_label_set_text(play_lbl, "Play");
    lv_obj_set_style_text_color(play_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(play_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(play_lbl);

    g_btn_music = lv_btn_create(btn_row);
    lv_obj_set_size(g_btn_music, 72, 44);
    lv_obj_set_style_radius(g_btn_music, 8, 0);
    lv_obj_set_style_bg_color(g_btn_music, lv_color_make(0x6B, 0x03, 0x6B), 0);
    lv_obj_add_event_cb(g_btn_music, music_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *music_lbl = lv_label_create(g_btn_music);
    lv_label_set_text(music_lbl, "Music");
    lv_obj_set_style_text_color(music_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(music_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(music_lbl);

    g_btn_stop = lv_btn_create(btn_row);
    lv_obj_set_size(g_btn_stop, 72, 44);
    lv_obj_set_style_radius(g_btn_stop, 8, 0);
    lv_obj_set_style_bg_color(g_btn_stop, lv_color_make(0x8B, 0x00, 0x00), 0);
    lv_obj_add_event_cb(g_btn_stop, stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_lbl = lv_label_create(g_btn_stop);
    lv_label_set_text(stop_lbl, "Stop");
    lv_obj_set_style_text_color(stop_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_lbl);

    update_button_states();
}

void audio_test_ui_init(void)
{
    if (!g_worker_task) {
        xTaskCreatePinnedToCore(audio_test_worker, "audio_test_w", 4 * 1024,
                                NULL, 3, &g_worker_task, 1);
    }
    ESP_LOGI(TAG, "audio test ui initialized");
}
