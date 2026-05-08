/* Minimal ES8311 + I2S driver for MIDI-style square-wave playback.
   Uses the new i2c_master API (via i2c_bsp) so it does not pull in
   the legacy driver/i2c.c constructor that aborts on IDF 5.2.x. */

#include "audio_min.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "i2c_bsp.h"

static const char *TAG = "audio_min";

/* --- Pinout from codec_board/board_cfg.txt for S3_LCD_3_49 --- */
#define PIN_I2S_MCLK   GPIO_NUM_7
#define PIN_I2S_BCLK   GPIO_NUM_15
#define PIN_I2S_LRCK   GPIO_NUM_46
#define PIN_I2S_DOUT   GPIO_NUM_45
#define PIN_I2S_DIN    GPIO_NUM_6

#define ES8311_ADDR    0x18

#define SAMPLE_RATE    16000
#define MCLK_MULT      256
#define BIT_DEPTH      I2S_DATA_BIT_WIDTH_16BIT

static i2c_master_dev_handle_t es8311_dev = NULL;
static i2s_chan_handle_t       i2s_tx     = NULL;
static TaskHandle_t            midi_task  = NULL;
static volatile bool           is_playing = false;

/* --- ES8311 register helpers --- */
static esp_err_t es_w(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(es8311_dev, buf, 2, 100);
}

static esp_err_t es_r(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(es8311_dev, &reg, 1, val, 1, 100);
}

/* Hardcoded init for MCLK=256*Fs, 16 kHz, 16-bit, slave mode, DAC out.
   Sequence taken from ES8311 datasheet "Quick start" appendix. */
static esp_err_t es8311_boot(void)
{
    /* Reset + enable analog */
    es_w(0x45, 0x00);     /* GP register */
    es_w(0x01, 0x30);     /* clkmgr: enable mclk, codec clocks */
    es_w(0x02, 0x00);     /* mclk div = 1, multiplier = 1 (256 fs) */
    es_w(0x03, 0x10);     /* adc fsmode + osr */
    es_w(0x16, 0x24);     /* adc internal */
    es_w(0x04, 0x10);     /* dac osr */
    es_w(0x05, 0x00);     /* adc/dac div = 1 */
    es_w(0x0B, 0x00);     /* system */
    es_w(0x0C, 0x00);     /* system */
    es_w(0x10, 0x1F);     /* power on stage 1 */
    es_w(0x11, 0x7F);     /* power on stage 2 */
    es_w(0x00, 0x80);     /* CSM start, slave mode */

    /* Slave mode, internal MCLK from BCLK pin (use_mclk=true, invert=false) */
    es_w(0x01, 0x3F);     /* select mclk source */

    es_w(0x13, 0x10);
    es_w(0x1B, 0x0A);
    es_w(0x1C, 0x6A);
    es_w(0x44, 0x08);     /* GPIO reg, no DAC2ADC */

    /* I2S serial port format: 16-bit, normal I2S */
    es_w(0x09, 0x0C);     /* DAC SDP: 16-bit, I2S */
    es_w(0x0A, 0x0C);     /* ADC SDP: 16-bit, I2S */

    /* Power up DAC */
    es_w(0x12, 0x00);     /* enable DAC */
    es_w(0x14, 0x1A);     /* analog pga */
    es_w(0x37, 0x08);     /* DAC ramp rate */

    /* Volume: 0xBF = 0 dB */
    es_w(0x32, 0xBF);

    /* Unmute DAC */
    es_w(0x31, 0x00);

    /* Read chip ID for sanity */
    uint8_t id1 = 0, id2 = 0;
    es_r(0xFD, &id1);
    es_r(0xFE, &id2);
    ESP_LOGI(TAG, "ES8311 chip ID: 0x%02x 0x%02x", id1, id2);
    return ESP_OK;
}

/* --- Square-wave MIDI player ---
   Twinkle Twinkle Little Star: C C G G A A G | F F E E D D C
   MIDI notes: 60 60 67 67 69 69 67 | 65 65 64 64 62 62 60 */
static const struct {
    uint8_t midi;     /* MIDI note (0 = rest) */
    uint16_t ms;
} TWINKLE[] = {
    {60,400},{60,400},{67,400},{67,400},{69,400},{69,400},{67,800},
    {65,400},{65,400},{64,400},{64,400},{62,400},{62,400},{60,800},
    {67,400},{67,400},{65,400},{65,400},{64,400},{64,400},{62,800},
    {67,400},{67,400},{65,400},{65,400},{64,400},{64,400},{62,800},
    {60,400},{60,400},{67,400},{67,400},{69,400},{69,400},{67,800},
    {65,400},{65,400},{64,400},{64,400},{62,400},{62,400},{60,800},
    {0,200},
};

static float midi_to_hz(uint8_t midi)
{
    /* 440 Hz at MIDI 69 (A4); each semitone = 2^(1/12) */
    static const float TABLE[12] = {
        16.3516f,17.3239f,18.3540f,19.4454f,20.6017f,21.8268f,
        23.1247f,24.4997f,25.9565f,27.5000f,29.1352f,30.8677f
    };
    int oct = midi / 12;
    int n   = midi % 12;
    return TABLE[n] * (float)(1 << oct);
}

#define BUF_SAMPLES 512   /* stereo 16-bit samples */

static void render_square(int16_t *buf, int n_samples, float freq, float amp)
{
    /* Generate stereo (L+R same) 16-bit square at given freq, amp 0..1. */
    static float phase = 0.0f;
    if (freq < 1.0f) {
        for (int i = 0; i < n_samples; i++) {
            buf[2*i+0] = 0;
            buf[2*i+1] = 0;
        }
        phase = 0.0f;
        return;
    }
    float step = freq / (float)SAMPLE_RATE;
    int16_t hi = (int16_t)(amp * 12000.0f);
    int16_t lo = -hi;
    for (int i = 0; i < n_samples; i++) {
        int16_t v = (phase < 0.5f) ? hi : lo;
        buf[2*i+0] = v;
        buf[2*i+1] = v;
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}

static void midi_task_fn(void *arg)
{
    int16_t *buf = heap_caps_malloc(BUF_SAMPLES * 2 * sizeof(int16_t),
                                    MALLOC_CAP_DMA);
    assert(buf);

    while (1) {
        if (!is_playing) {
            /* feed silence to keep TX alive */
            memset(buf, 0, BUF_SAMPLES * 2 * sizeof(int16_t));
            size_t w = 0;
            i2s_channel_write(i2s_tx, buf, BUF_SAMPLES * 2 * sizeof(int16_t),
                              &w, 100);
            continue;
        }

        for (size_t n = 0; n < sizeof(TWINKLE)/sizeof(TWINKLE[0]) && is_playing; n++) {
            uint8_t  midi = TWINKLE[n].midi;
            uint16_t ms   = TWINKLE[n].ms;
            float    hz   = (midi == 0) ? 0.0f : midi_to_hz(midi);
            int total_samples = (int)((uint32_t)SAMPLE_RATE * ms / 1000);

            while (total_samples > 0 && is_playing) {
                int chunk = total_samples > BUF_SAMPLES ? BUF_SAMPLES : total_samples;
                /* small inter-note gap so we hear note boundary */
                float amp = (total_samples < (int)((uint32_t)SAMPLE_RATE * 30 / 1000))
                            ? 0.0f : 0.5f;
                render_square(buf, chunk, hz, amp);
                size_t w = 0;
                i2s_channel_write(i2s_tx, buf, chunk * 2 * sizeof(int16_t),
                                  &w, portMAX_DELAY);
                total_samples -= chunk;
            }
        }
    }
}

esp_err_t audio_min_init(void)
{
    /* 1) Add ES8311 onto the existing port-0 master bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(esp_i2c_bus_handle, &dev_cfg, &es8311_dev));
    ESP_LOGI(TAG, "ES8311 on i2c, dev=%p", es8311_dev);

    /* 2) Bring up codec */
    es8311_boot();

    /* 3) Bring up I2S TX in master mode (codec is slave) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(BIT_DEPTH, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx));

    /* 4) Spawn MIDI task (idle by default) */
    xTaskCreatePinnedToCore(midi_task_fn, "midi", 4096, NULL, 5, &midi_task, 1);
    ESP_LOGI(TAG, "audio_min ready (16 kHz, 16-bit, stereo)");
    return ESP_OK;
}

void audio_min_play_midi(bool play)
{
    is_playing = play;
}

bool audio_min_is_playing(void)
{
    return is_playing;
}
