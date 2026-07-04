#include "ui_bg_fetcher.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdcard_bsp.h"

#define CLOCK_BG_PATH "/sdcard/clock_bg.bin"

static const char *TAG = "ui_bg_fetcher";

/* HTTP fetcher for bg_mode=2 (URL). Runs on its own task; sleeps for
   bg_refresh_s between fetches. Writes to CLOCK_BG_PATH then asks
   LVGL to reload the canvas. */
/* PNG/JPEG decoders (LV_USE_PNG / LV_USE_SJPG) used to be enabled
   here so URL backgrounds could be raw images. They were turned off
   because lodepng + tjpgd's static tables landed in internal DRAM and
   shrunk the DMA-capable heap to ~1.5 KB, breaking radio_init's I2S
   alloc and killing audio playback + mic VU. URL bg mode now only
   accepts the raw RGB565 panel-byte-order format. */

static TaskHandle_t s_bg_fetcher = NULL;
static volatile bool s_bg_fetcher_kick = false;

static esp_err_t bg_fetch_once(const char *url)
{
    if (!sdcard_is_mounted()) return ESP_ERR_INVALID_STATE;
    esp_http_client_config_t cfg = {0};
    cfg.url = url;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_err_t e = esp_http_client_open(c, 0);
    if (e != ESP_OK) { esp_http_client_cleanup(c); return e; }
    int hl = esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        ESP_LOGW(TAG, "bg fetch %s -> HTTP %d (len=%d)", url, status, hl);
        esp_http_client_close(c); esp_http_client_cleanup(c);
        return ESP_FAIL;
    }
    /* Stream the raw download (whatever format) to a temp file. */
    char dl[80];
    snprintf(dl, sizeof(dl), "%s.dl", CLOCK_BG_PATH);
    FILE *f = fopen(dl, "wb");
    if (!f) { esp_http_client_close(c); esp_http_client_cleanup(c); return ESP_FAIL; }
    char *buf = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); esp_http_client_close(c); esp_http_client_cleanup(c); return ESP_ERR_NO_MEM; }
    int total = 0;
    uint8_t magic[8] = {0};
    int magic_have = 0;
    while (1) {
        int n = esp_http_client_read(c, buf, 4096);
        if (n <= 0) break;
        if (fwrite(buf, 1, n, f) != (size_t)n) break;
        if (magic_have < (int)sizeof(magic)) {
            int take = n < (int)sizeof(magic) - magic_have
                       ? n : (int)sizeof(magic) - magic_have;
            memcpy(magic + magic_have, buf, take);
            magic_have += take;
        }
        total += n;
    }
    free(buf);
    fclose(f);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (total <= 0) { unlink(dl); return ESP_FAIL; }

    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%s.part", CLOCK_BG_PATH);

    size_t need = (size_t)canvas_w * canvas_h * 2;
    int packed = -1;
    if (magic_have >= 8 && magic[0] == 0x89 && magic[1] == 'P' &&
        magic[2] == 'N' && magic[3] == 'G') {
        ESP_LOGW(TAG, "bg fetch: PNG received but decoder is disabled "
                       "(LV_USE_PNG=n -- starves I2S DMA heap). Pre-convert "
                       "to raw RGB565 panel-byte-order, %zu bytes.", need);
    } else if (magic_have >= 3 && magic[0] == 0xFF && magic[1] == 0xD8 &&
               magic[2] == 0xFF) {
        ESP_LOGW(TAG, "bg fetch: JPEG received but decoder is disabled. "
                       "Pre-convert to raw RGB565 panel-byte-order, %zu bytes.", need);
    } else if ((size_t)total == need) {
        /* Raw RGB565 panel-byte-order payload -- just promote it. */
        ESP_LOGI(TAG, "bg fetched %d bytes (raw rgb565) from %s", total, url);
        packed = rename(dl, tmp);
    } else {
        ESP_LOGW(TAG, "bg fetch: unknown format, %d bytes (need %zu raw rgb565)",
                 total, need);
    }
    unlink(dl);    /* harmless if rename already consumed it */
    if (packed != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    /* FATFS rename() doesn't overwrite an existing destination -- unlink
       first so the new background actually replaces the old one. */
    unlink(CLOCK_BG_PATH);
    int rr = rename(tmp, CLOCK_BG_PATH);
    if (rr != 0) {
        ESP_LOGW(TAG, "bg fetch: rename %s -> %s failed (errno=%d)",
                 tmp, CLOCK_BG_PATH, errno);
        unlink(tmp);
        return ESP_FAIL;
    }
    /* Repaint on the LVGL task. */
    if (lvgl_lock(50)) { clock_bg_apply(); lvgl_unlock(); }
    return ESP_OK;
}

static void bg_fetcher_task(void *arg)
{
    (void)arg;
    /* Same Wi-Fi + radio-init defer as the quotes task. The TLS
       handshake during the first HTTPS GET will allocate enough
       internal RAM to starve radio_init's I2S DMA descriptors on a
       fragmented heap. */
    for (int i = 0; i < 120; i++) {
        if (g_wifi_connected) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    while (1) {
        if (g_cfg.bg_mode != 2 || !g_cfg.bg_url[0]) goto out;
        bg_fetch_once(g_cfg.bg_url);
        /* refresh_s == 0 means "once" -- exit after the first fetch
           (but the kick flag can still wake us via bg_fetcher_ensure
           spawning a fresh task). */
        if (g_cfg.bg_refresh_s == 0) goto out;
        int wait_s = g_cfg.bg_refresh_s;
        for (int i = 0; i < wait_s * 10; i++) {
            if (s_bg_fetcher_kick) { s_bg_fetcher_kick = false; break; }
            vTaskDelay(pdMS_TO_TICKS(100));
            if (g_cfg.bg_mode != 2) goto out;
        }
    }
out:
    s_bg_fetcher = NULL;
    vTaskDeleteWithCaps(NULL);
}

void bg_fetcher_ensure(void)
{
    if (g_cfg.bg_mode != 2 || !g_cfg.bg_url[0]) return;
    if (s_bg_fetcher) {
        s_bg_fetcher_kick = true;   /* poke existing task */
        return;
    }
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(
        bg_fetcher_task, "bg_fetcher", 6 * 1024, NULL, 4, &s_bg_fetcher, 0,
        MALLOC_CAP_SPIRAM);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "bg fetcher task spawn failed");
        s_bg_fetcher = NULL;
    }
}
