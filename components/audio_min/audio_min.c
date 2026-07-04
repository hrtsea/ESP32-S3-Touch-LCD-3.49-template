/* Minimal MIDI-style square-wave playback that reuses the radio engine's
   ES8311 play_dev instead of owning its own I2S + codec.

   Rationale: the full radio stack (radio.c) owns the I2S TDM controller
   and both the ES8311 DAC and ES7210 ADC.  Having audio_min bring up its
   own separate I2S STD master on the *same* pins / same codec chip
   races the radio init, and after radio_init() has run the audio_min
   codec handle is stale anyway -- which is why pressing Play on the
   Hello tile produced silence.

   This rewrite keeps the same public API (audio_min_play_midi /
   audio_min_is_playing / audio_min_set_volume / audio_min_shutdown) so
   all callers keep working, but the actual audio output goes through
   esp_codec_dev_write() on radio's play_dev.  When the radio is
   playing we politely refuse to start MIDI and return false -- the UI
   should gray the button out in that case. */

#include "audio_min.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "radio.h"

static const char *TAG = "audio_min";

static TaskHandle_t            midi_task     = NULL;
static volatile bool           is_playing    = false;
static volatile bool           shutdown_req  = false;
static volatile bool           started       = false;
static volatile uint16_t       sw_gain_q8    = 256; /* Q8.8: 256 = unity */

/* --- Square-wave MIDI player (Twinkle Twinkle Little Star) --- */
static const struct {
    uint8_t  midi;    /* 0 = rest */
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

#define BUF_SAMPLES 512   /* stereo 16-bit samples per chunk */

static void render_square(int16_t *buf, int n_samples, float freq,
                          float amp, int sample_rate)
{
    static float phase = 0.0f;
    if (freq < 1.0f) {
        for (int i = 0; i < n_samples; i++) {
            buf[2*i+0] = 0;
            buf[2*i+1] = 0;
        }
        phase = 0.0f;
        return;
    }
    float step = freq / (float)sample_rate;
    int32_t g  = (int32_t)sw_gain_q8;
    int16_t hi = (int16_t)((int32_t)(amp * 12000.0f) * g / 256);
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
    (void)arg;
    esp_codec_dev_handle_t play_dev =
        (esp_codec_dev_handle_t)radio_get_play_dev();
    if (!play_dev) {
        ESP_LOGE(TAG, "no play_dev available; exiting MIDI task");
        midi_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int sample_rate = (int)radio_get_sample_rate();
    ESP_LOGI(TAG, "MIDI task running @ %d Hz", sample_rate);

    int16_t *buf = (int16_t *)heap_caps_malloc(
        BUF_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "failed to allocate DMA buffer");
        midi_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (!shutdown_req) {
        if (!is_playing) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        for (size_t n = 0;
             n < sizeof(TWINKLE)/sizeof(TWINKLE[0])
             && is_playing && !shutdown_req;
             n++) {
            uint8_t  midi = TWINKLE[n].midi;
            uint16_t ms   = TWINKLE[n].ms;
            float    hz   = (midi == 0) ? 0.0f : midi_to_hz(midi);
            int total_samples = (int)((uint32_t)sample_rate * ms / 1000);

            while (total_samples > 0 && is_playing && !shutdown_req) {
                int chunk = (total_samples > BUF_SAMPLES)
                            ? BUF_SAMPLES : total_samples;
                /* small inter-note gap so we hear note boundaries */
                float amp = (total_samples <
                             (int)((uint32_t)sample_rate * 30 / 1000))
                            ? 0.0f : 0.5f;
                render_square(buf, chunk, hz, amp, sample_rate);
                esp_codec_dev_write(play_dev, buf,
                                    chunk * 2 * sizeof(int16_t));
                total_samples -= chunk;
            }
        }
    }

    free(buf);
    midi_task = NULL;
    vTaskDelete(NULL);
}

/* --- Public API --- */

esp_err_t audio_min_init(void)
{
    if (started) return ESP_OK;

    /* We no longer bring up our own I2S / codec.  The radio engine
       owns the shared TDM I2S bus + ES8311 + ES7210.  We just spawn
       a MIDI task that writes into radio's play_dev. */
    shutdown_req = false;
    is_playing   = false;

    BaseType_t rc = xTaskCreatePinnedToCore(
        midi_task_fn, "midi", 4096, NULL, 5, &midi_task, 1);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "failed to create MIDI task");
        return ESP_FAIL;
    }

    started = true;
    ESP_LOGI(TAG, "audio_min ready (reuses radio play_dev)");
    return ESP_OK;
}

void audio_min_set_volume(uint8_t vol_0_100)
{
    if (vol_0_100 > 100) vol_0_100 = 100;
    /* Linear 0..100 -> 0..256 (Q8.8). 100 = unity, 0 = silent. */
    sw_gain_q8 = (uint16_t)(((uint32_t)vol_0_100 * 256u + 50u) / 100u);
}

void audio_min_play_midi(bool play)
{
    if (!started) return;
    if (play && radio_is_playing()) {
        ESP_LOGW(TAG, "refusing MIDI start: radio is playing");
        return;
    }
    is_playing = play;
}

bool audio_min_is_playing(void)
{
    return started && is_playing;
}

bool audio_min_is_available(void)
{
    /* We can play only when radio is not playing and the engine is up */
    return started && !radio_is_playing() && radio_get_play_dev();
}

void audio_min_shutdown(void)
{
    if (!started) return;
    is_playing   = false;
    shutdown_req = true;
    /* Wait for the MIDI task to self-delete (max ~250 ms). */
    for (int i = 0; i < 50 && midi_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    started = false;
    ESP_LOGI(TAG, "audio_min shut down");
}
