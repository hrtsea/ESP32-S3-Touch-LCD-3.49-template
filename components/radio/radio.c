/* radio.c -- internet-radio engine: HTTP/HTTPS stream -> esp_audio_simple_player
 * -> esp_codec_dev (ES8311 + ES7210 over I2C+I2S TDM). Reworked to match the
 * reference 08_Audio_Test pattern that's known to record AND play correctly
 * on this board:
 *
 *   - Single duplex i2s_new_channel call (TX + RX)
 *   - I2S TDM mode, 32-bit slot, STEREO, 4 total slots, 16 kHz base
 *   - One shared audio_codec_data_if_t with BOTH tx_handle and rx_handle
 *   - ES8311 codec_if (DAC) -> play_dev with shared data_if
 *   - ES7210 codec_if (ADC) -> record_dev with same shared data_if
 *
 * Recorder reuses radio's record_dev directly via radio_get_record_dev().
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s_tdm.h"
#include "driver/i2c_master.h"

#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#include "es7210_adc.h"

#include "esp_audio_simple_player.h"

#include "i2c_bsp.h"   /* esp_i2c_bus_handle */
#include "radio.h"
#include "stations.h"

static const char *TAG = "radio";

/* From board_cfg.txt: S3_LCD_3_49. */
#define PIN_I2S_MCLK   GPIO_NUM_7
#define PIN_I2S_BCLK   GPIO_NUM_15
#define PIN_I2S_LRCK   GPIO_NUM_46
#define PIN_I2S_DOUT   GPIO_NUM_45
#define PIN_I2S_DIN    GPIO_NUM_6
#define ES8311_I2C_ADDR 0x30           /* 8-bit form of 7-bit 0x18 */
#define ES7210_I2C_ADDR 0x80           /* 8-bit form of 7-bit 0x40 */

/* TDM base sample rate. The ES7210/ES8311 share BCLK/LRCK, so all
   playback/record traffic is sample-rate-converted to/from this. */
#define I2S_TDM_RATE   16000

static i2s_chan_handle_t       s_i2s_tx     = NULL;
static i2s_chan_handle_t       s_i2s_rx     = NULL;
static const audio_codec_data_if_t *s_data_if = NULL;     /* shared TX+RX */
static const audio_codec_ctrl_if_t *s_ctrl_if_out = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if_in  = NULL;
static const audio_codec_gpio_if_t *s_gpio_if = NULL;
static const audio_codec_if_t *s_out_codec_if = NULL;
static const audio_codec_if_t *s_in_codec_if  = NULL;
static esp_codec_dev_handle_t  s_play_dev   = NULL;
static esp_codec_dev_handle_t  s_record_dev = NULL;

static esp_asp_handle_t        s_player     = NULL;
static volatile esp_asp_state_t s_state     = ESP_ASP_STATE_NONE;
static int                     s_cur_idx    = -1;
static char                    s_cur_uri[256] = {0};
static int                     s_volume     = 70;  /* 0..100 */
static uint32_t                s_cur_rate   = 0;
static uint8_t                 s_cur_bits   = 0;
static uint8_t                 s_cur_ch     = 0;
/* Output VU: peak abs sample seen in the most recent decoded chunk.
   Read-and-reset by radio_out_peak() so the UI can drive a playback
   bar. Resets to 0 when nothing is being decoded. */
static volatile uint16_t       s_out_peak_l = 0;
static volatile uint16_t       s_out_peak_r = 0;
/* Digital gain applied to decoded samples in radio_out_cb. Internet
   streams are already loudness-normalized; user-recorded WAVs have
   peaks at ~2% FS so we boost local files by ~8x. Set in radio_play
   based on the URI scheme. */
static int                     s_out_gain_q8 = 256;   /* Q8.8: 256 = 1.0x */

static int radio_out_cb(uint8_t *data, int data_size, void *ctx)
{
    (void)ctx;
    if (!s_play_dev || !data || data_size <= 0) return 0;
    /* Apply digital gain in-place (set per-URI by radio_play). Clip
       on saturation so loud transients don't wrap. Then peak-track
       the post-gain values so the output VU reflects what's actually
       hitting the speaker. */
    if (s_cur_bits == 16) {
        int16_t *p = (int16_t *)data;
        int n_samples = data_size / 2;
        int gain = s_out_gain_q8;
        if (gain != 256) {
            for (int i = 0; i < n_samples; i++) {
                int v = ((int)p[i] * gain) >> 8;
                if (v > 32767) v = 32767;
                else if (v < -32768) v = -32768;
                p[i] = (int16_t)v;
            }
        }
        uint16_t pl = 0, pr = 0;
        if (s_cur_ch == 2) {
            int frames = n_samples / 2;
            for (int i = 0; i < frames; i++) {
                int l = p[2 * i];      if (l < 0) l = -l;
                int r = p[2 * i + 1];  if (r < 0) r = -r;
                if (l > pl) pl = (uint16_t)l;
                if (r > pr) pr = (uint16_t)r;
            }
        } else {
            for (int i = 0; i < n_samples; i++) {
                int v = p[i]; if (v < 0) v = -v;
                if (v > pl) pl = (uint16_t)v;
            }
            pr = pl;
        }
        if (pl > s_out_peak_l) s_out_peak_l = pl;
        if (pr > s_out_peak_r) s_out_peak_r = pr;
    }
    int err = esp_codec_dev_write(s_play_dev, data, data_size);
    return err == ESP_CODEC_DEV_OK ? data_size : 0;
}

void radio_out_peak(uint16_t *out_l, uint16_t *out_r)
{
    if (out_l) *out_l = s_out_peak_l;
    if (out_r) *out_r = s_out_peak_r;
    s_out_peak_l = 0; s_out_peak_r = 0;
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
        if (s_play_dev) {
            uint32_t r = (uint32_t)info.sample_rate;
            uint8_t  b = (uint8_t)info.bits;
            uint8_t  c = (uint8_t)info.channels;
            if (r != s_cur_rate || b != s_cur_bits || c != s_cur_ch) {
                esp_codec_dev_sample_info_t fs = {
                    .bits_per_sample = b,
                    .channel         = c,
                    .channel_mask    = 0,
                    .sample_rate     = r,
                };
                /* In TDM mode both TX and RX share the same I2S bit/frame
                   clock, so we must re-open both devices at the new rate
                   to keep them in sync and avoid "sample_rate conflict"
                   errors from the I2S driver. */
                esp_codec_dev_close(s_play_dev);
                esp_codec_dev_open(s_play_dev, &fs);
                if (s_record_dev) {
                    esp_codec_dev_sample_info_t rec_fs = {
                        .bits_per_sample = 16,
                        .channel         = 2,
                        .sample_rate     = r,
                    };
                    esp_codec_dev_close(s_record_dev);
                    esp_codec_dev_open(s_record_dev, &rec_fs);
                    esp_codec_dev_set_in_gain(s_record_dev, 42.0f);
                }
                s_cur_rate = r;
                s_cur_bits = b;
                s_cur_ch   = c;
            }
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

    ESP_LOGI(TAG, "init step 1/9: i2s_new_channel (TX+RX duplex)");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.id = I2S_NUM_AUTO;
    chan_cfg.auto_clear = true;
    /* TDM with 4 slots * 32 bits * stereo at 16 kHz produces big DMA
       descriptors. The reference example runs before LVGL when there's
       still plenty of internal RAM; we run after, with internal heap
       fragmented. Use moderate sizing: enough to absorb jitter from
       LVGL/WiFi interrupts without exhausting internal RAM.
       4 desc × 96 frames = ~6KB per channel (TX+RX = ~12KB total).
       Up from the original 3×64 for better glitch resistance. */
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = 96;
    esp_err_t er = i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(er)); return er; }

    ESP_LOGI(TAG, "init step 2/9: i2s_channel_init_tdm_mode @ %dHz / 32-bit / 4 slots", I2S_TDM_RATE);
    /* TDM mode is what the reference uses to share one I2S frame
       between the ES8311 DAC slots and the ES7210 ADC slots.
       4 slots × 32 bits needs MCLK multiple of 384 (not default 256). */
    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg  = I2S_TDM_CLK_DEFAULT_CONFIG(I2S_TDM_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slot_mask),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
        },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
    tdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    er = i2s_channel_init_tdm_mode(s_i2s_tx, &tdm_cfg);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_init_tdm tx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_init_tdm_mode(s_i2s_rx, &tdm_cfg);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_init_tdm rx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_enable(s_i2s_tx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_enable tx: %s", esp_err_to_name(er)); return er; }
    er = i2s_channel_enable(s_i2s_rx);
    if (er != ESP_OK) { ESP_LOGE(TAG, "i2s_enable rx: %s", esp_err_to_name(er)); return er; }

    ESP_LOGI(TAG, "init step 3/9: shared data_if (TX+RX)");
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = s_i2s_tx,
        .rx_handle = s_i2s_rx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (!s_data_if) { ESP_LOGE(TAG, "data_if create failed"); return ESP_FAIL; }

    s_gpio_if = audio_codec_new_gpio();

    ESP_LOGI(TAG, "init step 4/9: ES8311 ctrl_if + codec_if (DAC)");
    audio_codec_i2c_cfg_t out_i2c = {
        .addr       = ES8311_I2C_ADDR,
        .bus_handle = esp_i2c_bus_handle,
    };
    s_ctrl_if_out = audio_codec_new_i2c_ctrl(&out_i2c);
    if (!s_ctrl_if_out) { ESP_LOGE(TAG, "es8311 ctrl create failed"); return ESP_FAIL; }
    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode      = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if         = s_ctrl_if_out,
        .gpio_if         = s_gpio_if,
        .pa_pin          = -1,         /* board_cfg: pa: -1 */
        .use_mclk        = true,
        .hw_gain.pa_gain = 6,          /* board_cfg: pa_gain: 6 */
    };
    s_out_codec_if = es8311_codec_new(&es8311_cfg);
    if (!s_out_codec_if) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 5/9: play_dev = ES8311 + shared data_if");
    esp_codec_dev_cfg_t play_cfg = {
        .codec_if = s_out_codec_if,
        .data_if  = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_play_dev = esp_codec_dev_new(&play_cfg);
    if (!s_play_dev) { ESP_LOGE(TAG, "play_dev create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 6/9: ES7210 ctrl_if + codec_if (ADC, all 4 mics)");
    audio_codec_i2c_cfg_t in_i2c = {
        .addr       = ES7210_I2C_ADDR,
        .bus_handle = esp_i2c_bus_handle,
    };
    s_ctrl_if_in = audio_codec_new_i2c_ctrl(&in_i2c);
    if (!s_ctrl_if_in) { ESP_LOGE(TAG, "es7210 ctrl create failed"); return ESP_FAIL; }
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if      = s_ctrl_if_in,
        /* TDM with 4 slots: enable all 4 mics so the ADC drives every
           slot. The reference sets MIC1|MIC3 then OR-s MIC2|MIC4 for TDM. */
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    s_in_codec_if = es7210_codec_new(&es7210_cfg);
    if (!s_in_codec_if) { ESP_LOGE(TAG, "es7210_codec_new failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 7/9: record_dev = ES7210 + same shared data_if");
    esp_codec_dev_cfg_t rec_cfg = {
        .codec_if = s_in_codec_if,
        .data_if  = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    s_record_dev = esp_codec_dev_new(&rec_cfg);
    if (!s_record_dev) { ESP_LOGE(TAG, "record_dev create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "init step 8/9: open codec devs at default fs");
    esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    /* 42 dB analog gain on the ES7210. Reference 08_Audio_Test uses
       30 dB for echo demos; for voice recording 42 dB matches typical
       handheld mic preamps and brings recorded peaks closer to FS so
       playback doesn't sound 10x quieter than internet radio. */
    esp_codec_dev_set_in_gain(s_record_dev, 42.0f);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = 44100,
    };
    int rc = esp_codec_dev_open(s_play_dev, &fs);
    if (rc != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "play_dev open: %d", rc); return ESP_FAIL; }
    /* Open the record device at the same rate so the TDM bit/frame
       clocks are consistent -- TX and RX share the same I2S clock in
       TDM mode, so mismatched rates cause "sample_rate conflict" errors
       and audio glitches on either side. */
    rc = esp_codec_dev_open(s_record_dev, &fs);
    if (rc != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "record_dev open: %d", rc); return ESP_FAIL; }
    s_cur_rate = 44100; s_cur_bits = 16; s_cur_ch = 2;

    ESP_LOGI(TAG, "init step 9/9: esp_audio_simple_player_new");
    esp_asp_cfg_t cfg = {
        .out.cb       = radio_out_cb,
        .out.user_ctx = NULL,
        .task_prio    = 6,
        .task_stack   = 12 * 1024,
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

static void codec_drain_silence(void)
{
    if (!s_play_dev) return;
    static int16_t zero[256] = {0};
    for (int i = 0; i < 8; i++) {
        esp_codec_dev_write(s_play_dev, zero, sizeof(zero));
    }
}

static void codec_vol_ramp(int from, int to, int step_ms)
{
    if (!s_play_dev) return;
    if (from == to) {
        esp_codec_dev_set_out_vol(s_play_dev, to);
        return;
    }
    int dir = (to > from) ? 1 : -1;
    for (int v = from; v != to; v += dir) {
        esp_codec_dev_set_out_vol(s_play_dev, v);
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
    esp_codec_dev_set_out_vol(s_play_dev, to);
}

esp_err_t radio_play(const char *uri)
{
    if (!s_player) return ESP_ERR_INVALID_STATE;
    if (!uri || !*uri) return ESP_ERR_INVALID_ARG;
    /* Decide playback gain by URI scheme. Local recordings have peaks
       at ~2% full-scale because the ES7210 is set to 30 dB in_gain;
       internet streams come in already loudness-normalized. Boost
       file:// playback by 8x (clipping on saturation) so it sounds
       comparable to internet radio. */
    if (strncmp(uri, "file://", 7) == 0) {
        s_out_gain_q8 = 256 * 8;   /* +18 dB */
    } else {
        s_out_gain_q8 = 256;       /* unity */
    }
    if (s_play_dev) {
        codec_vol_ramp(s_volume, 0, 5);
    }
    esp_audio_simple_player_stop(s_player);
    codec_drain_silence();
    ESP_LOGI(TAG, "play: %s (gain x%d)", uri, s_out_gain_q8 / 256);
    esp_gmf_err_t err = esp_audio_simple_player_run(s_player, uri, NULL);
    if (err == ESP_OK) {
        strncpy(s_cur_uri, uri, sizeof(s_cur_uri) - 1);
        s_cur_uri[sizeof(s_cur_uri) - 1] = 0;
    }
    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

const char *radio_current_uri(void)
{
    return s_cur_uri[0] ? s_cur_uri : NULL;
}

esp_err_t radio_stop(void)
{
    if (!s_player) return ESP_ERR_INVALID_STATE;
    if (s_play_dev) {
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
    if (s_play_dev && s_state == ESP_ASP_STATE_RUNNING) {
        esp_codec_dev_set_out_vol(s_play_dev, v);
    }
}

int radio_get_volume(void) { return s_volume; }

uint32_t radio_get_sample_rate(void)
{
    return s_cur_rate ? s_cur_rate : 44100;
}

void radio_beep(void)
{
    if (!s_play_dev) return;
    int rate = s_cur_rate ? s_cur_rate : 44100;
    int n_frames = rate;
    int half_period = rate / 880 / 2;
    int amp = 8000;
    int chunk_frames = 256;
    int16_t buf[256 * 2];
    int hi = 1;
    int phase = 0;
    int saved_vol = s_volume;
    esp_codec_dev_set_out_vol(s_play_dev, 70);
    int written = 0;
    while (written < n_frames) {
        int this_n = (n_frames - written < chunk_frames) ? (n_frames - written) : chunk_frames;
        for (int i = 0; i < this_n; i++) {
            int16_t s = hi ? amp : -amp;
            buf[2 * i]     = s;
            buf[2 * i + 1] = s;
            if (++phase >= half_period) { phase = 0; hi = !hi; }
        }
        esp_codec_dev_write(s_play_dev, buf, this_n * 2 * sizeof(int16_t));
        written += this_n;
    }
    memset(buf, 0, sizeof(buf));
    int silence_frames = rate / 10;
    int s_written = 0;
    while (s_written < silence_frames) {
        int this_n = (silence_frames - s_written < chunk_frames) ? (silence_frames - s_written) : chunk_frames;
        esp_codec_dev_write(s_play_dev, buf, this_n * 2 * sizeof(int16_t));
        s_written += this_n;
    }
    esp_codec_dev_set_out_vol(s_play_dev, saved_vol);
}

/* Recorder accessors. With the new architecture the recorder doesn't
   build its own codec_if -- it uses the record_dev that radio_init
   created with the shared data_if. */
void *radio_get_record_dev(void)        { return (void *)s_record_dev; }
void *radio_get_play_dev(void)          { return (void *)s_play_dev; }
/* Legacy accessors retained for ABI; not used by the new recorder. */
void *radio_get_i2s_rx_handle(void)     { return (void *)s_i2s_rx; }
void *radio_get_codec_ctrl_if(void)     { return (void *)s_ctrl_if_in; }
void *radio_get_codec_data_if_in(void)  { return (void *)s_data_if; }
void *radio_get_codec_if(void)          { return (void *)s_out_codec_if; }
