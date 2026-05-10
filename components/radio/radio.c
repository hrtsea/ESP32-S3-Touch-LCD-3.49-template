/* radio.c -- internet-radio engine: HTTP/HTTPS stream -> esp_audio_simple_player
 * -> esp_codec_dev (ES8311 over I2C+I2S). Stage-1 minimum: bring up the codec,
 * play a URI, stop on demand. ICY metadata, station list, and player UI live
 * one layer up. */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"

#include "esp_audio_simple_player.h"

#include "i2c_bsp.h"   /* esp_i2c_bus_handle */
#include "radio.h"
#include "stations.h"

static const char *TAG = "radio";

/* Same wiring as audio_min/audio_min.c */
#define PIN_I2S_MCLK   GPIO_NUM_7
#define PIN_I2S_BCLK   GPIO_NUM_15
#define PIN_I2S_LRCK   GPIO_NUM_46
#define PIN_I2S_DOUT   GPIO_NUM_45
#define PIN_I2S_DIN    GPIO_NUM_6
/* esp_codec_dev's audio_codec_new_i2c_ctrl shifts the addr right by 1 before
   handing it to i2c_master_bus_add_device, so we pass the 8-bit form here.
   ES8311's 7-bit slave address is 0x18 -> 8-bit form 0x30. */
#define ES8311_I2C_ADDR 0x30

static i2s_chan_handle_t       s_i2s_tx     = NULL;
static i2s_chan_handle_t       s_i2s_rx     = NULL;   /* shared with recorder */
static esp_codec_dev_handle_t  s_codec      = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;     /* shared */
static const audio_codec_data_if_t *s_data_if_in = NULL;  /* shared */
static const audio_codec_if_t *s_codec_if   = NULL;       /* shared, single ES8311 driver instance */
static esp_asp_handle_t        s_player     = NULL;
static volatile esp_asp_state_t s_state     = ESP_ASP_STATE_NONE;
static int                     s_cur_idx    = -1;
static int                     s_volume     = 70;  /* 0..100 */
static uint32_t                s_cur_rate   = 44100;  /* last codec_dev_open sample rate */
static uint8_t                 s_cur_bits   = 16;
static uint8_t                 s_cur_ch     = 2;

static int radio_out_cb(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if (!s_codec || !data || data_size <= 0) return 0;
    int err = esp_codec_dev_write(s_codec, data, data_size);
    return err == ESP_CODEC_DEV_OK ? data_size : 0;
}

static void codec_vol_ramp(int from, int to, int step_ms);

static int radio_event_cb(esp_asp_event_pkt_t *pkt, void *ctx)
{
    (void)ctx;
    if (pkt->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = ESP_ASP_STATE_NONE;
        memcpy(&st, pkt->payload, pkt->payload_size);
        s_state = st;
        ESP_LOGI(TAG, "state -> %s", esp_audio_simple_player_state_to_str(st));
    } else if (pkt->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, pkt->payload, pkt->payload_size);
        ESP_LOGI(TAG, "music info: rate=%d ch=%d bits=%d",
                 info.sample_rate, info.channels, info.bits);
        if (s_codec) {
            uint32_t r = (uint32_t)info.sample_rate;
            uint8_t  b = (uint8_t)info.bits;
            uint8_t  c = (uint8_t)info.channels;
            /* Only retune the codec if the format actually changed. Most
               station-to-station switches are 44.1->44.1 or 48->48 stereo;
               skipping the close/open in that case eliminates the audible
               click that the I2S clock retune produces. */
            if (r != s_cur_rate || b != s_cur_bits || c != s_cur_ch) {
                esp_codec_dev_sample_info_t fs = {
                    .bits_per_sample = b,
                    .channel         = c,
                    .channel_mask    = 0,
                    .sample_rate     = r,
                };
                esp_codec_dev_close(s_codec);
                esp_codec_dev_open(s_codec, &fs);
                s_cur_rate = r;
                s_cur_bits = b;
                s_cur_ch   = c;
            }
            /* Fade volume back up now that audio is flowing. */
            codec_vol_ramp(0, s_volume, 5);
        }
    }
    return 0;
}

esp_err_t radio_init(void)
{
    if (s_player) {
        ESP_LOGI(TAG, "init: already up");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "init step 1/8: i2s_new_channel (TX+RX duplex)");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    /* Allocate TX *and* RX in the same call. Doing this here once means
       the recorder can reuse s_i2s_rx without fighting the I2S driver
       for "controller occupied" errors, and both directions share the
       same BCLK/LRCK pair. */
    esp_err_t er = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(er)); return er; }

    ESP_LOGI(TAG, "init step 2/8: i2s_channel_init_std_mode @ 44100");
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    er = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_init_std tx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_init_std rx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_enable(s_i2s_tx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_enable tx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_enable(s_i2s_rx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_enable rx: %s", esp_err_to_name(er)); return er; }

    ESP_LOGI(TAG, "init step 3/8: audio_codec_new_i2c_ctrl bus=%p addr=0x%02x",
             esp_i2c_bus_handle, ES8311_I2C_ADDR);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr    = ES8311_I2C_ADDR,
        .bus_handle = esp_i2c_bus_handle,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_ctrl_if) { ESP_LOGE(TAG, "i2c ctrl create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 4/8: audio_codec_new_i2s_data (TX) + (RX)");
    audio_codec_i2s_cfg_t i2s_cfg_tx = {
        .port      = I2S_NUM_0,
        .tx_handle = s_i2s_tx,
        .rx_handle = NULL,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg_tx);
    if (!data_if) { ESP_LOGE(TAG, "i2s data create (tx) failed"); return ESP_FAIL; }
    audio_codec_i2s_cfg_t i2s_cfg_rx = {
        .port      = I2S_NUM_0,
        .tx_handle = NULL,
        .rx_handle = s_i2s_rx,
    };
    s_data_if_in = audio_codec_new_i2s_data(&i2s_cfg_rx);
    if (!s_data_if_in) { ESP_LOGE(TAG, "i2s data create (rx) failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 5/8: es8311_codec_new (DAC, recorder will reuse for ADC)");
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if   = s_ctrl_if,
        .gpio_if   = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk  = true,
    };
    s_codec_if = es8311_codec_new(&es_cfg);
    if (!s_codec_if) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }
    const audio_codec_if_t *codec_if = s_codec_if;

    ESP_LOGI(TAG, "init step 6/8: esp_codec_dev_new");
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) { ESP_LOGE(TAG, "codec_dev_new failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 7/8: esp_codec_dev_open");
    esp_codec_dev_set_out_vol(s_codec, s_volume);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = 44100,
    };
    int rc = esp_codec_dev_open(s_codec, &fs);
    if (rc != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "codec_dev_open: rc=%d", rc); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 8/8: esp_audio_simple_player_new");
    esp_asp_cfg_t cfg = {
        .out.cb       = radio_out_cb,
        .out.user_ctx = NULL,
        .task_prio    = 5,
        .task_stack   = 8 * 1024,
        .task_stack_in_ext = true,
        .task_core    = 1,
    };
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &s_player);
    if (err != ESP_OK || !s_player) {
        ESP_LOGE(TAG, "player_new: err=%d", (int)err);
        return ESP_FAIL;
    }
    esp_audio_simple_player_set_event(s_player, radio_event_cb, NULL);
    ESP_LOGI(TAG, "radio engine ready");
    return ESP_OK;
}

/* Push N samples of digital silence into the I2S TX so any partial frame
   left in the DMA ring is overwritten before we mute or change rate. */
static void codec_drain_silence(void)
{
    if (!s_codec) return;
    static int16_t zero[256] = {0};  /* 64 stereo frames */
    for (int i = 0; i < 8; i++) {
        esp_codec_dev_write(s_codec, zero, sizeof(zero));
    }
}

/* Soft volume ramp: walk from `from` to `to` in `step_ms` ms steps. This
   masks the DAC zipper-noise that you get from a hard 0 -> 70 jump. */
static void codec_vol_ramp(int from, int to, int step_ms)
{
    if (!s_codec) return;
    if (from == to) {
        esp_codec_dev_set_out_vol(s_codec, to);
        return;
    }
    int dir = (to > from) ? 1 : -1;
    for (int v = from; v != to; v += dir) {
        esp_codec_dev_set_out_vol(s_codec, v);
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
    esp_codec_dev_set_out_vol(s_codec, to);
}

esp_err_t radio_play(const char *uri)
{
    if (!s_player) return ESP_ERR_INVALID_STATE;
    if (!uri || !*uri) return ESP_ERR_INVALID_ARG;
    /* Fade out the current stream (if any) before tearing down the
       pipeline. radio_event_cb fades back up when the new stream's
       MUSIC_INFO arrives so the user hears a clean cross-fade rather
       than a "zzz" tail + click + ramp. The codec stays open, the I2S
       channel stays running -- no hardware re-init around the transition. */
    if (s_codec) {
        codec_vol_ramp(s_volume, 0, 5);  /* ~5*70 = 350 ms fade-out */
    }
    esp_audio_simple_player_stop(s_player);
    codec_drain_silence();
    ESP_LOGI(TAG, "play: %s", uri);
    esp_gmf_err_t err = esp_audio_simple_player_run(s_player, uri, NULL);
    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t radio_stop(void)
{
    if (!s_player) return ESP_ERR_INVALID_STATE;
    /* Fade out, then stop the decoder pipeline. The codec stays open and
       at volume 0; next radio_play() resumes from there and ramps up
       when audio starts flowing. No mute toggle, no codec close. */
    if (s_codec) {
        codec_vol_ramp(s_volume, 0, 5);
    }
    esp_audio_simple_player_stop(s_player);
    codec_drain_silence();
    return ESP_OK;
}

bool radio_is_playing(void)
{
    return s_state == ESP_ASP_STATE_RUNNING;
}

int radio_station_count(void) { return RADIO_STATION_COUNT; }

const char *radio_station_name(int idx) {
    if (idx < 0 || idx >= RADIO_STATION_COUNT) return "";
    return k_stations[idx].name;
}
const char *radio_station_genre(int idx) {
    if (idx < 0 || idx >= RADIO_STATION_COUNT) return "";
    return k_stations[idx].genre;
}
const char *radio_station_url(int idx) {
    if (idx < 0 || idx >= RADIO_STATION_COUNT) return "";
    return k_stations[idx].url;
}

esp_err_t radio_play_index(int idx)
{
    if (idx < 0 || idx >= RADIO_STATION_COUNT) return ESP_ERR_INVALID_ARG;
    s_cur_idx = idx;
    return radio_play(k_stations[idx].url);
}

int radio_current_index(void) { return s_cur_idx; }

void radio_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    /* Only push the new volume to the codec if we're actively playing.
       When stopped the codec sits at 0 (faded out by radio_stop); the
       MUSIC_INFO handler ramps to s_volume next time playback starts. */
    if (s_codec && s_state == ESP_ASP_STATE_RUNNING) {
        esp_codec_dev_set_out_vol(s_codec, v);
    }
}

int radio_get_volume(void) { return s_volume; }

void *radio_get_i2s_rx_handle(void)     { return (void *)s_i2s_rx; }
void *radio_get_codec_ctrl_if(void)     { return (void *)s_ctrl_if; }
void *radio_get_codec_data_if_in(void)  { return (void *)s_data_if_in; }
void *radio_get_codec_if(void)          { return (void *)s_codec_if; }
