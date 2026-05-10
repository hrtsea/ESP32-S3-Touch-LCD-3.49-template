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
#include "es7210_adc.h"

#include "i2c_bsp.h"
#include "recorder.h"
#include "radio.h"
#include "sdcard_bsp.h"

static const char *TAG = "recorder";

#define REC_DIR        "/sdcard/recordings"
/* Match the radio engine's clock so the shared I2S BCLK isn't fought
   over (peer-mode conflict). 44.1 kHz / 16-bit / stereo so the L/R VU
   bars track real left and right input levels. ~10.6 MB/min. */
#define REC_RATE       44100
#define REC_BITS       16
#define REC_CHANNELS   2
#define REC_CHUNK      512   /* stereo frames per read; total samples = 2*512 */

static esp_codec_dev_handle_t   s_codec_in   = NULL;
static volatile bool            s_recording  = false;   /* writing to file */
static volatile bool            s_monitor    = false;   /* worker running for VU only */
static volatile bool            s_init_done  = false;
static FILE                    *s_fp         = NULL;
static uint32_t                 s_pcm_bytes  = 0;
static uint32_t                 s_started_ms = 0;
static char                     s_cur_path[96] = {0};
static TaskHandle_t             s_worker     = NULL;
/* VU meter: peak abs sample per channel since the UI last read it.
   Stereo capture, two channels interleaved (L, R, L, R...). */
static volatile uint16_t        s_peak_l     = 0;
static volatile uint16_t        s_peak_r     = 0;
/* Session peaks (don't reset on read) so we can confirm at rec_stop
   that the mic actually captured signal, not zeros. */
static volatile uint16_t        s_session_peak_l = 0;
static volatile uint16_t        s_session_peak_r = 0;
static volatile uint32_t        s_nonzero_samples = 0;
/* Playback state we paused so we can resume on rec_stop. radio_play
   parks the URL string in radio.c; this just remembers whether
   playback was active when we started recording. */
static bool                     s_paused_pb_was_playing = false;
static char                     s_paused_pb_uri[256] = {0};

static esp_err_t worker_spawn(void);

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
    /* Stereo: REC_CHUNK frames * 2 channels * sizeof(int16_t). */
    int total_samples = REC_CHUNK * REC_CHANNELS;
    int buf_bytes     = total_samples * sizeof(int16_t);
    int16_t *buf = malloc(buf_bytes);
    if (!buf) {
        ESP_LOGE(TAG, "no mem for chunk");
        s_recording = false; s_monitor = false;
        s_worker = NULL;
        vTaskDeleteWithCaps(NULL);
        return;
    }
    while (s_recording || s_monitor) {
        /* When playback is active and we're only in monitor mode (not
           actually recording to a file), back off the codec read loop.
           The TDM I2S frame is shared between record_dev and play_dev,
           and constantly reading from it while the player is writing
           causes audible stutter and short-decode on playback. */
        if (!s_recording && radio_is_playing()) {
            s_peak_l = 0; s_peak_r = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        int rc = esp_codec_dev_read(s_codec_in, buf, buf_bytes);
        if (rc != 0 && rc != buf_bytes) {
            ESP_LOGW(TAG, "codec_read rc=%d", rc);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        uint16_t peak_l = 0, peak_r = 0;
        for (int i = 0; i < REC_CHUNK; i++) {
            int l = buf[2 * i];
            int r = buf[2 * i + 1];
            if (l < 0) l = -l;
            if (r < 0) r = -r;
            if (l > peak_l) peak_l = (uint16_t)l;
            if (r > peak_r) peak_r = (uint16_t)r;
        }
        if (peak_l > s_peak_l) s_peak_l = peak_l;
        if (peak_r > s_peak_r) s_peak_r = peak_r;
        if (s_recording) {
            if (peak_l > s_session_peak_l) s_session_peak_l = peak_l;
            if (peak_r > s_session_peak_r) s_session_peak_r = peak_r;
            /* Count how many samples in this chunk were non-zero so we
               can tell at stop time whether the mic actually delivered
               signal (vs all zeros from a misconfigured codec path). */
            for (int i = 0; i < REC_CHUNK; i++) {
                if (buf[2*i] != 0 || buf[2*i+1] != 0) s_nonzero_samples++;
            }
            if (s_fp) {
                size_t w = fwrite(buf, 1, buf_bytes, s_fp);
                if (w == (size_t)buf_bytes) s_pcm_bytes += buf_bytes;
            }
        }
    }
    free(buf);
    s_worker = NULL;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t worker_spawn(void)
{
    if (s_worker) return ESP_OK;
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(
        recorder_task, "recorder", 4 * 1024, NULL, 5, &s_worker, 1,
        MALLOC_CAP_SPIRAM);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate recorder rc=%d", (int)r);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t recorder_init(void)
{
    if (s_init_done) return ESP_OK;
    mkdir(REC_DIR, 0775);

    /* The radio engine has already created an ES7210-backed record_dev
       that shares the same TDM data_if as the play_dev (the reference
       08_Audio_Test pattern). Just reuse it -- creating a second
       codec_if/dev_new for the same chip races on I2C and produces
       silent (zero) reads. */
    void *rec_v = radio_get_record_dev();
    if (!rec_v) {
        ESP_LOGE(TAG, "radio_get_record_dev() returned NULL");
        return ESP_ERR_INVALID_STATE;
    }
    s_codec_in = (esp_codec_dev_handle_t)rec_v;

    esp_codec_dev_set_in_gain(s_codec_in, 30.0f);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = REC_BITS,
        .channel         = REC_CHANNELS,
        .sample_rate     = REC_RATE,
    };
    int rc = esp_codec_dev_open(s_codec_in, &fs);
    if (rc != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "codec_dev_open(in): %d", rc); return ESP_FAIL; }

    s_init_done = true;
    ESP_LOGI(TAG, "recorder ready (using radio's record_dev)");
    return ESP_OK;
}

esp_err_t recorder_start(const char **out_path)
{
    if (s_recording) return ESP_ERR_INVALID_STATE;
    if (!s_init_done) {
        esp_err_t er = recorder_init();
        if (er != ESP_OK) return er;
    }

    /* If the radio engine is currently playing something (radio stream
       or a previously played recording), pause it so the new recording
       isn't bleeding the speaker output back into the mic. Save the
       URI so we can resume on rec_stop. */
    s_paused_pb_was_playing = false;
    s_paused_pb_uri[0] = 0;
    if (radio_is_playing()) {
        const char *u = radio_current_uri();
        if (u) {
            strncpy(s_paused_pb_uri, u, sizeof(s_paused_pb_uri) - 1);
            s_paused_pb_uri[sizeof(s_paused_pb_uri) - 1] = 0;
            s_paused_pb_was_playing = true;
            ESP_LOGI(TAG, "pausing playback for recording: %s", s_paused_pb_uri);
        }
        radio_stop();
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
    s_session_peak_l = 0;
    s_session_peak_r = 0;
    s_nonzero_samples = 0;
    wav_write_header(s_fp, 0, REC_RATE, REC_BITS, REC_CHANNELS);

    s_started_ms = (uint32_t)(esp_log_timestamp());
    s_recording  = true;
    if (worker_spawn() != ESP_OK) {
        s_recording = false;
        fclose(s_fp); s_fp = NULL;
        return ESP_FAIL;
    }
    if (out_path) *out_path = s_cur_path;
    ESP_LOGI(TAG, "recording -> %s", s_cur_path);
    return ESP_OK;
}

esp_err_t recorder_monitor_start(void)
{
    if (!s_init_done) {
        esp_err_t er = recorder_init();
        if (er != ESP_OK) return er;
    }
    if (s_monitor || s_recording) return ESP_OK;
    s_monitor = true;
    if (worker_spawn() != ESP_OK) {
        s_monitor = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void recorder_monitor_stop(void)
{
    if (s_recording) return;   /* keep the worker; recording owns it */
    s_monitor = false;
    /* Worker exits on its own next loop iteration. */
}

esp_err_t recorder_stop(void)
{
    if (!s_recording) return ESP_OK;
    s_recording = false;
    /* If monitor isn't keeping the worker alive, wait for it to drain
       its current codec read before we patch the WAV header (otherwise
       the worker could fwrite *after* we've rewritten the header). */
    if (!s_monitor) {
        for (int i = 0; i < 50 && s_worker != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    } else {
        /* Worker is still running but recording flag is false, so it
           won't fwrite anymore. One short delay to clear the in-flight
           chunk it may already be writing. */
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (s_fp) {
        fflush(s_fp);
        fseek(s_fp, 0, SEEK_SET);
        wav_write_header(s_fp, s_pcm_bytes, REC_RATE, REC_BITS, REC_CHANNELS);
        fclose(s_fp);
        s_fp = NULL;
    }
    ESP_LOGI(TAG, "stopped, %lu bytes pcm  peak L=%u R=%u  nonzero_samples=%lu",
             (unsigned long)s_pcm_bytes,
             (unsigned)s_session_peak_l, (unsigned)s_session_peak_r,
             (unsigned long)s_nonzero_samples);
    /* Resume whatever was playing before recording started. */
    if (s_paused_pb_was_playing && s_paused_pb_uri[0]) {
        ESP_LOGI(TAG, "resuming playback: %s", s_paused_pb_uri);
        radio_play(s_paused_pb_uri);
        s_paused_pb_was_playing = false;
        s_paused_pb_uri[0] = 0;
    }
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
    /* Don't opendir on a missing/unmounted card -- the FATFS driver
       will spam read errors trying to read the boot sector. */
    if (!sdcard_is_mounted()) return 0;
    DIR *d = opendir(REC_DIR);
    if (!d) return 0;
    /* readdir() returns entries in FATFS internal order, NOT sorted by
       filename. If we hit max_n while iterating, we may keep older
       files and drop the newest. Read everything (up to a cap), then
       sort descending by filename, then return the first max_n. */
    enum { MAX_SCAN = 64 };
    static char scan[MAX_SCAN][64];
    int total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && total < MAX_SCAN) {
        if (e->d_name[0] == '.') continue;
        size_t len = strlen(e->d_name);
        if (len < 5) continue;
        if (strcasecmp(e->d_name + len - 4, ".wav") == 0 ||
            strcasecmp(e->d_name + len - 4, ".mp3") == 0 ||
            (len >= 5 && strcasecmp(e->d_name + len - 5, ".opus") == 0)) {
            strncpy(scan[total], e->d_name, 63);
            scan[total][63] = 0;
            total++;
        }
    }
    closedir(d);
    /* Sort descending by filename. Filenames are timestamps
       (YYYYMMDD-HHMMSS.wav), so lexicographic descending == newest
       first. Insertion sort is fine for ~64 entries. */
    for (int i = 1; i < total; i++) {
        char key[64];
        memcpy(key, scan[i], 64);
        int j = i - 1;
        while (j >= 0 && strcmp(scan[j], key) < 0) {
            memcpy(scan[j + 1], scan[j], 64);
            j--;
        }
        memcpy(scan[j + 1], key, 64);
    }
    int out_n = total < max_n ? total : max_n;
    for (int i = 0; i < out_n; i++) {
        memcpy(buf[i], scan[i], 64);
    }
    return out_n;
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

esp_err_t recorder_file_info(const char *name, uint32_t *out_bytes,
                             uint32_t *out_duration_ms)
{
    if (!sdcard_is_mounted()) return ESP_ERR_INVALID_STATE;
    char p[96];
    recorder_full_path(p, sizeof(p), name);
    struct stat st;
    if (stat(p, &st) != 0) return ESP_FAIL;
    uint32_t bytes = (uint32_t)st.st_size;
    if (out_bytes) *out_bytes = bytes;
    if (out_duration_ms) {
        /* Strip the 44-byte WAV header, then compute ms.
           Bytes/sec = REC_RATE * REC_CHANNELS * (REC_BITS/8) = 176400. */
        uint32_t pcm = (bytes > 44) ? (bytes - 44) : 0;
        uint64_t bps = (uint64_t)REC_RATE * REC_CHANNELS * (REC_BITS / 8);
        *out_duration_ms = bps ? (uint32_t)(((uint64_t)pcm * 1000) / bps) : 0;
    }
    return ESP_OK;
}

uint16_t recorder_peak_level(void)
{
    uint16_t l = s_peak_l, r = s_peak_r;
    s_peak_l = 0; s_peak_r = 0;
    return l > r ? l : r;
}

void recorder_peak_lr(uint16_t *out_l, uint16_t *out_r)
{
    if (out_l) *out_l = s_peak_l;
    if (out_r) *out_r = s_peak_r;
    s_peak_l = 0; s_peak_r = 0;
}
