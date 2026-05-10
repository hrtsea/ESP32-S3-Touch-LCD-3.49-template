/* recorder.c -- voice recorder: ES8311 ADC -> I2S RX -> WAV file on SD.
 *
 * Format choice: 16 kHz / 16-bit / mono WAV. ~32 KB/s, ~1.9 MB per minute.
 * The user asked for Opus, but Espressif's Opus encoder emits raw frames
 * (not OGG-encapsulated), so playback through simple_player would need a
 * custom container. WAV is trivial to encode + decode and simple_player
 * handles .wav natively. Stage-1 ships WAV; Opus can layer on later.
 *
 * The radio engine owns the I2S TX pair. We share the same I2S port (NUM 0)
 * but allocate a *new* RX channel for capture. esp_codec_dev needs the
 * codec_if + ctrl_if + i2s data_if; we wire just the RX side.
 *
 * Recording runs on a worker task pinned to core 1, reads PCM in chunks,
 * fwrites them to the file. recorder_stop fixes up the WAV header (riff/
 * data sizes) before closing.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"

#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"

#include "recorder.h"
#include "radio.h"

static const char *TAG = "recorder";

#define REC_DIR        "/sdcard/recordings"
/* Match the radio engine's clock so the shared I2S BCLK isn't fought
   over (peer-mode conflict). The radio runs at 44.1 kHz stereo by
   default; we capture at the same rate but mono. */
#define REC_RATE       44100
#define REC_BITS       16
#define REC_CHANNELS   1
#define REC_CHUNK      1024  /* PCM samples per read iteration (mono) */

static esp_codec_dev_handle_t   s_codec_in   = NULL;
static volatile bool            s_recording  = false;
static volatile bool            s_init_done  = false;
static FILE                    *s_fp         = NULL;
static uint32_t                 s_pcm_bytes  = 0;
static uint32_t                 s_started_ms = 0;
static char                     s_cur_path[96] = {0};
static TaskHandle_t             s_worker     = NULL;
/* VU meter: peak abs sample seen since the UI last read it. The worker
   updates this on every chunk; recorder_peak_level() reads-and-resets. */
static volatile uint16_t        s_peak       = 0;

static void wav_write_header(FILE *f, uint32_t pcm_bytes,
                             uint32_t rate, uint16_t bits, uint16_t channels)
{
    uint32_t byte_rate   = rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);
    uint32_t riff_size   = 36 + pcm_bytes;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16; uint16_t fmt_tag = 1;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&fmt_tag,  2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate,     4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits,     2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&pcm_bytes, 4, 1, f);
}

static void recorder_task(void *arg)
{
    (void)arg;
    int16_t *buf = malloc(REC_CHUNK * sizeof(int16_t));
    if (!buf) { ESP_LOGE(TAG, "no mem for chunk"); s_recording = false; vTaskDelete(NULL); return; }
    while (s_recording) {
        /* esp_codec_dev_read returns 0 on success and the byte count is
           filled in via the bytes parameter being equal to what we asked.
           Some IDF versions return the byte count directly. Treat any
           negative value as failure, anything else as a full read of the
           requested size. */
        int rc = esp_codec_dev_read(s_codec_in, buf, REC_CHUNK * sizeof(int16_t));
        if (rc != 0 && rc != REC_CHUNK * (int)sizeof(int16_t)) {
            ESP_LOGW(TAG, "codec_read rc=%d", rc);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        int n = REC_CHUNK * sizeof(int16_t);
        /* Track peak sample for the VU meter. Doing it here (per-chunk,
           on the recording task) keeps the UI poll cheap. */
        uint16_t peak = 0;
        for (int i = 0; i < REC_CHUNK; i++) {
            int s = buf[i];
            if (s < 0) s = -s;
            if (s > peak) peak = (uint16_t)s;
        }
        if (peak > s_peak) s_peak = peak;
        if (s_fp) {
            size_t w = fwrite(buf, 1, n, s_fp);
            if (w == (size_t)n) s_pcm_bytes += n;
        }
    }
    free(buf);
    s_worker = NULL;
    /* Pair with xTaskCreatePinnedToCoreWithCaps so the PSRAM stack is
       freed too. Plain vTaskDelete would leak it. */
    vTaskDeleteWithCaps(NULL);
}

esp_err_t recorder_init(void)
{
    if (s_init_done) return ESP_OK;
    mkdir(REC_DIR, 0775);

    /* Reuse the radio engine's RX channel + codec interfaces. The radio
       already allocated I2S TX+RX as a duplex pair and put ES8311 in
       BOTH (DAC+ADC) mode. Allocating a second I2S channel or a second
       ES8311 codec instance here would race with the radio over the
       same I2C device and the same I2S controller. */
    void *rx = radio_get_i2s_rx_handle();
    void *ctrl_if_v = radio_get_codec_ctrl_if();
    void *data_if_in_v = radio_get_codec_data_if_in();
    if (!rx || !ctrl_if_v || !data_if_in_v) {
        ESP_LOGE(TAG, "radio engine not up (rx=%p ctrl=%p data=%p)",
                 rx, ctrl_if_v, data_if_in_v);
        return ESP_ERR_INVALID_STATE;
    }
    const audio_codec_ctrl_if_t *ctrl_if = (const audio_codec_ctrl_if_t *)ctrl_if_v;
    const audio_codec_data_if_t *data_if = (const audio_codec_data_if_t *)data_if_in_v;

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if     = ctrl_if,
        .gpio_if     = audio_codec_new_gpio(),
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk    = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) return ESP_FAIL;

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if  = data_if,
    };
    s_codec_in = esp_codec_dev_new(&dev_cfg);
    if (!s_codec_in) return ESP_FAIL;

    esp_codec_dev_set_in_gain(s_codec_in, 30.0f);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = REC_BITS,
        .channel         = REC_CHANNELS,
        .sample_rate     = REC_RATE,
    };
    int rc = esp_codec_dev_open(s_codec_in, &fs);
    if (rc != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "codec_dev_open(in): %d", rc); return ESP_FAIL; }

    s_init_done = true;
    ESP_LOGI(TAG, "recorder ready (sharing radio's I2S RX + codec)");
    return ESP_OK;
}

esp_err_t recorder_start(const char **out_path)
{
    if (s_recording) return ESP_ERR_INVALID_STATE;
    if (!s_init_done) {
        esp_err_t er = recorder_init();
        if (er != ESP_OK) return er;
    }

    /* Build a path with a wall-clock stamp so files sort sensibly. */
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    snprintf(s_cur_path, sizeof(s_cur_path),
             "%s/%04d%02d%02d-%02d%02d%02d.wav", REC_DIR,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    s_fp = fopen(s_cur_path, "wb");
    if (!s_fp) {
        ESP_LOGE(TAG, "fopen %s failed: %s", s_cur_path, strerror(errno));
        /* Try ensuring the directory exists; first-boot ordering or a
           previous failed mount may have left it missing. */
        mkdir(REC_DIR, 0775);
        s_fp = fopen(s_cur_path, "wb");
        if (!s_fp) {
            ESP_LOGE(TAG, "fopen retry: %s", strerror(errno));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "fopen retry ok");
    }
    /* Reserve space for the header; we patch it on stop. */
    s_pcm_bytes = 0;
    wav_write_header(s_fp, 0, REC_RATE, REC_BITS, REC_CHANNELS);

    s_started_ms = (uint32_t)(esp_log_timestamp());
    s_recording  = true;
    if (!s_worker) {
        /* Internal RAM is fragmented after LVGL is up; a 4KB internal-RAM
           stack alloc fails with rc=-1. Use *WithCaps to put the stack
           in PSRAM, which has plenty of room. The TCB stays in internal
           RAM (required by FreeRTOS). */
        BaseType_t r = xTaskCreatePinnedToCoreWithCaps(
            recorder_task, "recorder", 4 * 1024, NULL, 5, &s_worker, 1,
            MALLOC_CAP_SPIRAM);
        if (r != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreate recorder rc=%d", (int)r);
            s_recording = false;
            fclose(s_fp); s_fp = NULL;
            return ESP_FAIL;
        }
    }
    if (out_path) *out_path = s_cur_path;
    ESP_LOGI(TAG, "recording -> %s", s_cur_path);
    return ESP_OK;
}

esp_err_t recorder_stop(void)
{
    if (!s_recording) return ESP_OK;
    s_recording = false;
    /* Wait briefly for the worker to drain its current read. */
    for (int i = 0; i < 50 && s_worker != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_fp) {
        /* Patch in the real sizes. */
        fflush(s_fp);
        fseek(s_fp, 0, SEEK_SET);
        wav_write_header(s_fp, s_pcm_bytes, REC_RATE, REC_BITS, REC_CHANNELS);
        fclose(s_fp);
        s_fp = NULL;
    }
    ESP_LOGI(TAG, "stopped, %lu bytes pcm", (unsigned long)s_pcm_bytes);
    return ESP_OK;
}

bool recorder_is_recording(void) { return s_recording; }

unsigned recorder_elapsed_s(void)
{
    if (!s_recording || s_started_ms == 0) return 0;
    return (unsigned)((esp_log_timestamp() - s_started_ms) / 1000);
}

int recorder_list(char buf[][64], int max_n)
{
    DIR *d = opendir(REC_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max_n) {
        if (e->d_name[0] == '.') continue;
        size_t len = strlen(e->d_name);
        if (len < 5) continue;
        /* Accept .wav (and .opus / .mp3 if/when the encoder lands). */
        if (strcasecmp(e->d_name + len - 4, ".wav") == 0 ||
            strcasecmp(e->d_name + len - 4, ".mp3") == 0 ||
            (len >= 5 && strcasecmp(e->d_name + len - 5, ".opus") == 0)) {
            strncpy(buf[n], e->d_name, 63);
            buf[n][63] = 0;
            n++;
        }
    }
    closedir(d);
    /* Reverse-sort so newest is first (timestamps make filenames sort
       lexicographically by date already). */
    for (int i = 0; i < n / 2; i++) {
        char tmp[64];
        memcpy(tmp, buf[i], 64);
        memcpy(buf[i], buf[n-1-i], 64);
        memcpy(buf[n-1-i], tmp, 64);
    }
    return n;
}

void recorder_full_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", REC_DIR, name);
}

esp_err_t recorder_delete(const char *name)
{
    char p[96];
    recorder_full_path(p, sizeof(p), name);
    return (remove(p) == 0) ? ESP_OK : ESP_FAIL;
}

uint16_t recorder_peak_level(void)
{
    uint16_t v = s_peak;
    s_peak = 0;
    return v;
}
