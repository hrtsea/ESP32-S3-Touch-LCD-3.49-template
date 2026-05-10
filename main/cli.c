/* cli.c -- minimal UART/USB-Serial-JTAG command shell for debugging.
 *
 * Commands:
 *   help                  list commands
 *   mem                   free internal/PSRAM heap, largest block
 *   wifi                  current SSID, IP, RSSI
 *   audio_off             call audio_min_shutdown()
 *   radio_init            run radio_init() and report each step
 *   radio_play <url>      stream a URL (HTTP or HTTPS)
 *   radio_stop            stop playback
 *   radio_status          state + last music info
 *   radio_test            audio_off -> radio_init -> radio_play <test url>
 *
 * Reads from the UART_NUM_0 driver via esp_console_repl. The host can pipe
 * commands in at 115200 8N1 (idf.py monitor or just `screen /dev/cu.* 115200`).
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "audio_min.h"
#include "radio.h"
#include "recorder.h"
#include "sdcard_bsp.h"
#include "cli.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "esp_vfs_fat.h"

static const char *TAG = "cli";

/* Espressif's known-good test asset. URL ends in .mp3 so the simple_player
   can pick the MP3 decoder by extension. mbedtls is configured to allocate
   SSL state from PSRAM (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC) so this works
   even with fragmented internal heap. */
#define RADIO_TEST_URL "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3"

static int cmd_mem(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t int_lb   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_lb   = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_lb    = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    printf("internal: free=%u largest=%u  DMA: free=%u largest=%u  PSRAM: free=%u largest=%u\n",
           (unsigned)int_free, (unsigned)int_lb,
           (unsigned)dma_free, (unsigned)dma_lb,
           (unsigned)ps_free,  (unsigned)ps_lb);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    (void)argc; (void)argv;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip = {0};
        if (nif) esp_netif_get_ip_info(nif, &ip);
        printf("ssid=%s rssi=%d ip=" IPSTR "\n",
               (char *)ap.ssid, (int)ap.rssi, IP2STR(&ip.ip));
    } else {
        printf("not associated\n");
    }
    return 0;
}

static int cmd_audio_off(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("calling audio_min_shutdown()\n");
    audio_min_shutdown();
    printf("ok\n");
    return 0;
}

static int cmd_radio_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("calling radio_init()\n");
    esp_err_t r = radio_init();
    printf("radio_init -> %s (0x%x)\n", esp_err_to_name(r), (unsigned)r);
    return r == ESP_OK ? 0 : 1;
}

static struct {
    struct arg_str *url;
    struct arg_end *end;
} s_play_args;

static struct {
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_end *end;
} s_wifi_connect_args;

static struct {
    struct arg_int *idx;
    struct arg_end *end;
} s_lang_args;

static int cmd_lang(int argc, char **argv)
{
    if (argc == 1) {
        printf("lang = %d\n", app_cfg_get_lang());
        return 0;
    }
    int n = arg_parse(argc, argv, (void **)&s_lang_args);
    if (n != 0) { arg_print_errors(stderr, s_lang_args.end, "lang"); return 1; }
    int idx = s_lang_args.idx->ival[0];
    app_cfg_set_lang(idx);
    printf("lang -> %d (reboot or re-enter menu to refresh labels)\n", idx);
    return 0;
}

static int cmd_bl(int argc, char **argv)
{
    if (argc < 2) { printf("usage: bl <0..255>\n"); return 1; }
    int v = atoi(argv[1]);
    app_cfg_set_brightness(v);
    printf("brightness=%d\n", v);
    return 0;
}

static int cmd_no_sleep(int argc, char **argv)
{
    (void)argc; (void)argv;
    app_cfg_set_dim_off(0, 0);
    printf("dim_s=0 off_s=0 (never dim/off)\n");
    return 0;
}

static int cmd_rec_start(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *p = NULL;
    esp_err_t r = recorder_start(&p);
    printf("recorder_start -> %s (path=%s)\n", esp_err_to_name(r), p ? p : "?");
    return r == ESP_OK ? 0 : 1;
}

static int cmd_rec_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t r = recorder_stop();
    printf("recorder_stop -> %s\n", esp_err_to_name(r));
    return 0;
}

static int cmd_rec_list(int argc, char **argv)
{
    (void)argc; (void)argv;
    static char names[16][64];
    int n = recorder_list(names, 16);
    printf("recordings: %d\n", n);
    for (int i = 0; i < n; i++) printf("  %s\n", names[i]);
    return 0;
}

static int cmd_rec_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("recording=%d elapsed=%us\n",
           (int)recorder_is_recording(), recorder_elapsed_s());
    return 0;
}

static int cmd_sd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint64_t total = 0, free = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free) != ESP_OK || total == 0) {
        printf("sd: not mounted\n");
        return 1;
    }
    printf("sd: total=%llu MB free=%llu MB (%.1f%%)\n",
           total / (1024ULL*1024), free / (1024ULL*1024),
           total ? (100.0 * free / total) : 0.0);
    return 0;
}

static int cmd_sd_format(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (recorder_is_recording()) recorder_stop();
    printf("formatting SD (this can take 30-90s)...\n");
    esp_err_t r = sdcard_format();
    mkdir("/sdcard/recordings", 0775);
    printf("format -> %s\n", esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
}

static int cmd_wifi_connect(int argc, char **argv)
{
    int n = arg_parse(argc, argv, (void **)&s_wifi_connect_args);
    if (n != 0) {
        arg_print_errors(stderr, s_wifi_connect_args.end, "wifi_connect");
        return 1;
    }
    const char *ssid = s_wifi_connect_args.ssid->sval[0];
    const char *pass = (s_wifi_connect_args.pass->count > 0)
                        ? s_wifi_connect_args.pass->sval[0] : "";
    printf("wifi_connect: ssid=%s pass_len=%u\n", ssid, (unsigned)strlen(pass));
    app_wifi_connect_save(ssid, pass);
    return 0;
}

static int cmd_radio_play(int argc, char **argv)
{
    int n = arg_parse(argc, argv, (void **)&s_play_args);
    if (n != 0) {
        arg_print_errors(stderr, s_play_args.end, "radio_play");
        return 1;
    }
    const char *url = s_play_args.url->sval[0];
    printf("calling radio_play(%s)\n", url);
    esp_err_t r = radio_play(url);
    printf("radio_play -> %s (0x%x)\n", esp_err_to_name(r), (unsigned)r);
    return r == ESP_OK ? 0 : 1;
}

static int cmd_radio_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t r = radio_stop();
    printf("radio_stop -> %s\n", esp_err_to_name(r));
    return 0;
}

static int cmd_radio_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("radio: playing=%d\n", (int)radio_is_playing());
    return 0;
}

static int cmd_radio_test(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("[test] step 1: audio_min_shutdown\n");
    audio_min_shutdown();
    printf("[test] step 2: radio_init\n");
    esp_err_t r = radio_init();
    if (r != ESP_OK) {
        printf("[test] FAIL at radio_init: %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("[test] step 3: radio_play(%s)\n", RADIO_TEST_URL);
    r = radio_play(RADIO_TEST_URL);
    if (r != ESP_OK) {
        printf("[test] FAIL at radio_play: %s\n", esp_err_to_name(r));
        return 1;
    }
    printf("[test] play started, audio should be flowing now\n");
    return 0;
}

void cli_start(void)
{
    /* USB-Serial-JTAG-backed REPL: stdin reaches us via the same
       /dev/cu.usbmodem* port that idf.py monitor uses. Requires
       CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y in sdkconfig.defaults. */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "esp> ";
    repl_cfg.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hw_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl) != ESP_OK) {
        ESP_LOGE(TAG, "console init failed");
        return;
    }

    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        { .command = "mem",          .help = "show heap free / largest block",  .func = &cmd_mem },
        { .command = "wifi",         .help = "show Wi-Fi SSID/IP/RSSI",          .func = &cmd_wifi },
        { .command = "audio_off",    .help = "shut down audio_min (free I2S/codec)", .func = &cmd_audio_off },
        { .command = "radio_init",   .help = "initialise the radio engine",     .func = &cmd_radio_init },
        { .command = "radio_stop",   .help = "stop radio playback",             .func = &cmd_radio_stop },
        { .command = "radio_status", .help = "show radio playing state",        .func = &cmd_radio_status },
        { .command = "radio_test",   .help = "audio_off -> radio_init -> play test URL", .func = &cmd_radio_test },
        { .command = "rec_start",    .help = "start a recording", .func = &cmd_rec_start },
        { .command = "rec_stop",     .help = "stop the recording", .func = &cmd_rec_stop },
        { .command = "rec_list",     .help = "list recordings on SD", .func = &cmd_rec_list },
        { .command = "rec_status",   .help = "is the recorder running?", .func = &cmd_rec_status },
        { .command = "sd_info",      .help = "SD card free/total bytes", .func = &cmd_sd_info },
        { .command = "sd_format",    .help = "delete all files in /sdcard/recordings", .func = &cmd_sd_format },
        { .command = "bl",           .help = "set backlight 0..255 (e.g. bl 255)", .func = &cmd_bl },
        { .command = "no_sleep",     .help = "disable dim/off (sets dim_s=off_s=0)", .func = &cmd_no_sleep },
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }

    /* radio_play takes a URL argument. */
    s_play_args.url = arg_str1(NULL, NULL, "<url>", "stream URL");
    s_play_args.end = arg_end(2);
    const esp_console_cmd_t play_cmd = {
        .command  = "radio_play",
        .help     = "play a URL through the radio engine",
        .hint     = NULL,
        .func     = &cmd_radio_play,
        .argtable = &s_play_args,
    };
    esp_console_cmd_register(&play_cmd);

    /* wifi_connect <ssid> [pass]: persist creds + start association. */
    s_wifi_connect_args.ssid = arg_str1(NULL, NULL, "<ssid>", "Wi-Fi SSID");
    s_wifi_connect_args.pass = arg_str0(NULL, NULL, "[pass]", "Wi-Fi password (optional for open APs)");
    s_wifi_connect_args.end  = arg_end(2);
    const esp_console_cmd_t wcc = {
        .command  = "wifi_connect",
        .help     = "save SSID/pass to NVS and connect (e.g. wifi_connect MyAP secret)",
        .hint     = NULL,
        .func     = &cmd_wifi_connect,
        .argtable = &s_wifi_connect_args,
    };
    esp_console_cmd_register(&wcc);

    /* lang [idx]: get/set active language (0=en, 1=zh, 2=ja, 3=ko). */
    s_lang_args.idx = arg_int1(NULL, NULL, "<idx>", "0=en 1=zh 2=ja 3=ko");
    s_lang_args.end = arg_end(2);
    const esp_console_cmd_t lc = {
        .command  = "lang",
        .help     = "show or set UI language (0=en 1=zh 2=ja 3=ko)",
        .hint     = NULL,
        .func     = &cmd_lang,
        .argtable = &s_lang_args,
    };
    esp_console_cmd_register(&lc);

    if (esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGE(TAG, "console start failed");
        return;
    }
    ESP_LOGI(TAG, "CLI ready -- type 'help'");
}
