/* webui.c -- tiny HTTP control panel.
 *
 * Endpoints:
 *   GET  /                 -> the HTML page (one self-contained file)
 *   GET  /api/state        -> JSON status (recording, playing, vol, brightness, ...)
 *   GET  /api/list         -> JSON list of recordings
 *   POST /api/cfg          -> apply brightness/volume/dim/off
 *   POST /api/rec/start    -> recorder_start()
 *   POST /api/rec/stop     -> recorder_stop()
 *   POST /api/play         -> radio_play(body)
 *   POST /api/stop         -> radio_stop()
 *   GET  /rec/<file>       -> stream a recording as audio/wav
 *   GET  /screen.bmp       -> capture the LVGL framebuffer as a BMP
 *
 * The BMP encoder is a 12-line wrapper around the framebuffer: emit a
 * BITMAPINFOHEADER + BI_BITFIELDS for RGB565 and stream rows verbatim.
 * No JPEG dep, browser handles BMP natively, ~220 KB per frame at
 * 640x172 which the typical iPhone/laptop pulls in well under a second.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "mbedtls/base64.h"

#include "webui.h"
#include "recorder.h"
#include "radio.h"
#include "sdcard_bsp.h"
#include "config.h"       /* g_config, config_save_* */
#include "fan_control.h"  /* FanConfig */

/* ── Basic Auth configuration ──────────────────────────────────── */

static char s_auth_username[64] = {0};
static char s_auth_password[128] = {0};
static bool s_auth_enabled = false;

void webui_set_auth(const char *username, const char *password)
{
    if (!username || !username[0] || !password) {
        s_auth_enabled = false;
        s_auth_username[0] = '\0';
        s_auth_password[0] = '\0';
        return;
    }

    strncpy(s_auth_username, username, sizeof(s_auth_username) - 1);
    strncpy(s_auth_password, password, sizeof(s_auth_password) - 1);
    s_auth_enabled = true;
}

bool webui_check_auth(httpd_req_t *req)
{
    if (!s_auth_enabled) {
        return true;
    }

    char auth_header[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }

    if (strncmp(auth_header, "Basic ", 6) != 0) {
        return false;
    }

    unsigned char decoded[192];
    size_t decoded_len = 0;
    const char *b64_data = auth_header + 6;
    size_t b64_len = strlen(b64_data);

    int ret = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                                     (const unsigned char *)b64_data, b64_len);
    if (ret != 0 || decoded_len == 0) {
        return false;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr((char *)decoded, ':');
    if (!colon) {
        return false;
    }
    *colon = '\0';
    const char *username = (char *)decoded;
    const char *password = colon + 1;

    return (strcmp(username, s_auth_username) == 0 &&
            strcmp(password, s_auth_password) == 0);
}

esp_err_t webui_auth_pre_request_hook(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    return webui_check_auth(req) ? ESP_OK : ESP_FAIL;
}

static void send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"WebUI\"");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"Unauthorized\"}");
}

#define CHECK_AUTH(req) do { \
    if (!webui_check_auth(req)) { \
        send_unauthorized(req); \
        return ESP_FAIL; \
    } \
} while(0)

/* These live in main/ (cli.h). Declare locally so this component
   doesn't need to include from main/. */
extern void app_cfg_set_brightness(int v);
extern void app_cfg_set_dim_off(int dim_s, int off_s);
extern int  app_cfg_get_brightness(void);
extern int  app_cfg_get_dim_s(void);
extern int  app_cfg_get_off_s(void);
/* Clock face customization. */
extern int  app_cfg_get_clock_x(void);
extern int  app_cfg_get_clock_y(void);
extern int  app_cfg_get_clock_size(void);
extern uint32_t app_cfg_get_clock_rgba(void);
extern int  app_cfg_get_show_ms(void);
extern void app_cfg_set_clock_pos(int x, int y);
extern void app_cfg_set_clock_size(int sz);
extern void app_cfg_set_clock_rgba(uint32_t rgba);
extern void app_cfg_set_show_ms(int show);
extern int  app_cfg_get_show_seconds(void);
extern void app_cfg_set_show_seconds(int show);
extern int  app_cfg_get_show_clock(void);
extern void app_cfg_set_show_clock(int show);
extern const char *app_cfg_get_clock_text(void);
extern void app_cfg_set_clock_text(const char *s);
extern int  app_cfg_get_bg_mode(void);
extern int  app_cfg_get_bg_refresh_s(void);
extern const char *app_cfg_get_bg_url(void);
extern int  app_cfg_get_canvas_w(void);
extern int  app_cfg_get_canvas_h(void);
extern void app_cfg_set_bg_mode(int m);
extern void app_cfg_set_bg_url(const char *url);
extern void app_cfg_set_bg_refresh_s(int s);
extern void app_cfg_clock_bg_reload(void);
extern void app_cfg_bg_fetch_now(void);
extern uint32_t app_cfg_get_bg_color(void);
extern void app_cfg_set_bg_color(uint32_t rgba);
/* Quotes tile. */
extern const char *app_cfg_get_quotes_sym_l(void);
extern const char *app_cfg_get_quotes_sym_r(void);
extern int  app_cfg_get_quotes_refresh_s(void);
extern uint32_t app_cfg_get_quotes_up_rgba(void);
extern uint32_t app_cfg_get_quotes_down_rgba(void);
extern void app_cfg_set_quotes_sym_l(const char *s);
extern void app_cfg_set_quotes_sym_r(const char *s);
extern void app_cfg_set_quotes_refresh_s(int s);
extern void app_cfg_set_quotes_up_rgba(uint32_t rgba);
extern void app_cfg_set_quotes_down_rgba(uint32_t rgba);
extern void app_cfg_set_active_tile(int idx);
/* Snapshot the LCD framebuffer into the caller's buffer (RGB565,
   panel byte order). Returns bytes written or -1 on error. The
   snapshot is taken under the lvgl mutex so the BMP encoder doesn't
   tear when LVGL flushes mid-read. */
extern int  webui_snapshot_fb(void *out, size_t cap);

static const char *TAG = "webui";

static httpd_handle_t s_srv = NULL;
static const void    *s_fb  = NULL;
static int            s_fb_w = 0;
static int            s_fb_h = 0;

void webui_set_framebuffer(const void *fb, int w, int h)
{
    s_fb = fb; s_fb_w = w; s_fb_h = h;
}

/* ---------- helpers ---------- */

static esp_err_t send_str(httpd_req_t *r, const char *ctype, const char *s)
{
    httpd_resp_set_type(r, ctype);
    return httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
}

static int read_body(httpd_req_t *r, char *buf, int max)
{
    int total = 0;
    while (total < max - 1) {
        int n = httpd_req_recv(r, buf + total, max - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = 0;
    return total;
}

/* ---------- index page ---------- */

/* Single-page UI. Polls /api/state every 1 s and refreshes /screen.bmp
   every 1 s by default. The screen <img> uses a cache-buster query so
   the browser actually re-requests instead of using the disk cache. */
static const char k_index_html[] =
"<!doctype html>\n"
"<html><head><meta charset=utf-8><title>photonichat</title>\n"
"<meta name=viewport content='width=device-width,initial-scale=1'>\n"
"<style>\n"
"body{font-family:-apple-system,Segoe UI,sans-serif;background:#181820;color:#eee;margin:0;padding:12px;max-width:760px}\n"
"h2{margin:14px 0 6px;color:#9cf}\n"
"section{background:#22222c;border-radius:8px;padding:12px;margin-bottom:12px}\n"
"button{background:#3a4;color:#fff;border:0;border-radius:6px;padding:8px 14px;margin:2px;font-size:14px;cursor:pointer}\n"
"button.warn{background:#a32}\n"
"button:disabled{opacity:.4}\n"
"input[type=range]{width:100%}\n"
"label{display:block;margin:8px 0 2px;color:#aac}\n"
"a{color:#9cf}\n"
"#screen{image-rendering:pixelated;width:100%;max-width:640px;border:1px solid #333;background:#000}\n"
"table{width:100%;border-collapse:collapse;font-size:13px}\n"
"td{padding:4px 6px;border-bottom:1px solid #333}\n"
".meta{color:#888;font-size:11px}\n"
".vu{height:8px;background:#333;border-radius:3px;overflow:hidden;margin:4px 0}\n"
".vu>div{height:100%;background:#3c4;transition:width .1s}\n"
".pill{display:inline-block;padding:2px 8px;border-radius:10px;font-size:12px}\n"
".pill.rec{background:#a22}.pill.play{background:#249}.pill.idle{background:#444}\n"
"</style></head><body>\n"
"<h1 style='margin:0'>photonichat</h1>\n"
"<div class=meta>simple control panel</div>\n"
"<section><h2>Status</h2>\n"
" <div id=status>...</div>\n"
" <div class=vu><div id=vul style='width:0%'></div></div>\n"
" <div class=vu><div id=vur style='width:0%'></div></div>\n"
"</section>\n"
"<section><h2>Screen mirror</h2>\n"
" <img id=screen src='/screen.bmp' alt=screen>\n"
" <label>refresh: <span id=rrl>1.0</span> s</label>\n"
" <input id=rr type=range min=0.5 max=10 step=0.5 value=1>\n"
"</section>\n"
"<section><h2>Clock</h2>\n"
" <label>show clock</label>\n"
" <input id=ckon type=checkbox>\n"
" <label>custom text (empty = use the time)</label>\n"
" <input id=cktx type=text maxlength=39 placeholder='e.g. Hello' style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>size</label>\n"
" <select id=cks><option value=0>XS</option><option value=1>S</option><option value=2>M</option><option value=3 selected>L</option></select>\n"
" <label>show seconds</label>\n"
" <input id=css type=checkbox>\n"
" <label>show milliseconds</label>\n"
" <input id=cms type=checkbox>\n"
" <label>color</label>\n"
" <input id=ckc type=color value='#ffffff'>\n"
" <label>opacity: <span id=ckal>255</span></label>\n"
" <input id=cka type=range min=0 max=255 value=255>\n"
" <label>position (drag the box on the map)</label>\n"
" <div id=ckmap style='position:relative;width:100%;max-width:640px;border:1px solid #555;background:#000;height:172px;overflow:hidden'>\n"
"  <div id=ckbox style='position:absolute;border:2px solid #4af;background:rgba(60,180,255,.15);cursor:move;color:#fff;font:bold 14px sans-serif;text-align:center;line-height:1.1'>00:00:00</div>\n"
" </div>\n"
" <div class=meta>x=<span id=ckxv>0</span> y=<span id=ckyv>0</span> (offset from center)</div>\n"
" <button id=ckcenter>Center</button>\n"
"</section>\n"
"<section><h2>Background</h2>\n"
" <label>mode</label>\n"
" <select id=bgm><option value=0>Sun map</option><option value=1>Custom upload</option><option value=2>URL</option><option value=3>Solid color</option></select>\n"
" <label>solid color (used when mode = Solid color)</label>\n"
" <input id=bgc type=color value='#202020'>\n"
" <label>upload an image (any size; converted to <span id=bgwh>640x172</span> RGB565)</label>\n"
" <input id=bgfile type=file accept='image/*'>\n"
" <div id=bgstat class=meta></div>\n"
" <label>URL (must serve raw RGB565 panel-byte-order, exactly <span id=bgsize>220160</span> bytes)</label>\n"
" <input id=bgurl type=text style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px' placeholder='https://example.com/bg.rgb565'>\n"
" <label>refresh every (s, 0=once)</label>\n"
" <input id=bgref type=number min=0 max=86400 value=0 style='width:120px'>\n"
" <button id=bgfetch>Fetch now</button> <span id=bgfetchstat class=meta></span>\n"
"</section>\n"
"<section><h2>Quotes</h2>\n"
" <div class=meta>Two side-by-side instrument quotes fetched from https://pchat.photonicat.com/q/&lt;symbol&gt; every N seconds. Examples: xauusd, xagusd, eurusd, btcusd, aapl.</div>\n"
" <label>left symbol</label>\n"
" <input id=qsl type=text style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px' maxlength=15 placeholder='xauusd'>\n"
" <label>right symbol</label>\n"
" <input id=qsr type=text style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px' maxlength=15 placeholder='xagusd'>\n"
" <label>refresh every (s, min 5)</label>\n"
" <input id=qref type=number min=5 max=3600 value=60 style='width:120px'>\n"
" <label>up color (positive change)</label>\n"
" <input id=quc type=color value='#33dd66'>\n"
" <label>down color (negative change)</label>\n"
" <input id=qdc type=color value='#ff4040'>\n"
" <button id=qfetch>Fetch now</button> <span id=qfetchstat class=meta></span>\n"
"</section>\n"
"<section><h2>Settings</h2>\n"
" <label>brightness: <span id=brl>?</span></label>\n"
" <input id=br type=range min=0 max=255>\n"
" <label>volume: <span id=vl>?</span></label>\n"
" <input id=vol type=range min=0 max=100>\n"
" <label>dim after (s, 0=never): <span id=dl>?</span></label>\n"
" <input id=ds type=range min=0 max=600 step=10>\n"
" <label>off after (s, 0=never): <span id=ol>?</span></label>\n"
" <input id=os type=range min=0 max=1800 step=10>\n"
"</section>\n"
"<section><h2>WiFi</h2>\n"
" <div id=wifistatus class=meta>Loading...</div>\n"
" <div style='margin:10px 0'>\n"
"  <button id=wifiscan>Scan</button>\n"
"  <button id=wifidisconnect>Disconnect</button>\n"
" </div>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Saved Networks</h3>\n"
" <div id=wifisaved style='margin-bottom:12px'></div>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Add Network</h3>\n"
" <label>SSID</label>\n"
" <input id=wifissid type=text style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>Password</label>\n"
" <input id=wifipass type=password style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>priority (0-255, higher = preferred)</label>\n"
" <input id=wifiprio type=number min=0 max=255 value=10 style='width:120px'>\n"
" <button id=wifiadd>Add & Connect</button>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Scan Results</h3>\n"
" <div id=wifiscanres></div>\n"
"</section>\n"
"<section><h2>Recorder</h2>\n"
" <button id=brec>Start REC</button>\n"
" <button id=bstop class=warn>Stop</button>\n"
" <button id=bpstop>Stop playback</button>\n"
"</section>\n"
"<section><h2>Recordings</h2>\n"
" <table id=tbl><tr><th>name</th><th>size</th><th>dur</th><th></th></tr></table>\n"
"</section>\n"
"<section id=devsettings><h2>Device Settings</h2>\n"
" <div id=dsstat class=meta>Load settings...</div>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>WiFi</h3>\n"
" <label>SSID</label>\n"
" <input id=cf_ssid type=text maxlength=32 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>Password (leave blank to keep current)</label>\n"
" <input id=cf_wifipass type=password maxlength=64 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>NAS Connection</h3>\n"
" <label>NAS type</label>\n"
" <select id=cf_nas_type style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
"  <option value=mock>Mock (test)</option><option value=synology>Synology DSM</option>\n"
"  <option value=qnap>QNAP QTS</option><option value=truenas>TrueNAS</option>\n"
"  <option value=fnos>FNOS</option><option value=unraid>Unraid</option>\n"
"  <option value=netdata>Netdata</option><option value=snmp>SNMP</option>\n"
"  <option value=linux_http>Linux (HTTP)</option><option value=linux_serial>Linux (Serial)</option>\n"
"  <option value=windows>Windows</option>\n"
" </select>\n"
" <label>IP / Host</label>\n"
" <input id=cf_nas_ip type=text maxlength=39 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>Port</label>\n"
" <input id=cf_nas_port type=number min=1 max=65535 style='width:120px'>\n"
" <label>Username</label>\n"
" <input id=cf_nas_user type=text maxlength=31 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>Password (leave blank to keep current)</label>\n"
" <input id=cf_nas_pass type=password maxlength=64 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>HTTPS <input id=cf_nas_https type=checkbox></label>\n"
" <label>SNMP community</label>\n"
" <input id=cf_snmp_comm type=text maxlength=31 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>SNMP version</label>\n"
" <select id=cf_snmp_ver style='width:120px;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
"  <option value=1>1</option><option value=2>2</option><option value=3>3</option>\n"
" </select>\n"
" <label>Serial baud</label>\n"
" <input id=cf_serial_baud type=number style='width:120px'>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Display</h3>\n"
" <label>Poll interval (s, 1-30)</label>\n"
" <input id=cf_poll_sec type=number min=1 max=30 style='width:120px'>\n"
" <label>Rotation angle</label>\n"
" <select id=cf_rotation style='width:120px;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
"  <option value=0>0</option><option value=90>90</option><option value=180>180</option><option value=270>270</option>\n"
" </select>\n"
" <label>Auto dim <input id=cf_autodim type=checkbox></label>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Disk Slots</h3>\n"
" <label>SATA disk count (0-16)</label>\n"
" <input id=cf_sata type=number min=0 max=16 style='width:120px'>\n"
" <label>M.2 disk count (0-16)</label>\n"
" <input id=cf_m2 type=number min=0 max=16 style='width:120px'>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Time & Weather</h3>\n"
" <label>Timezone (-12 to 14)</label>\n"
" <input id=cf_tz type=number min=-12 max=14 style='width:120px'>\n"
" <label>Auto cycle enabled <input id=cf_cycle_en type=checkbox></label>\n"
" <label>Auto cycle interval (s)</label>\n"
" <input id=cf_cycle_int type=number min=1 max=300 style='width:120px'>\n"
" <label>Weather API key</label>\n"
" <input id=cf_wkey type=text maxlength=64 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <label>Weather city</label>\n"
" <input id=cf_wcity type=text maxlength=31 style='width:100%;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
" <h3 style='margin:12px 0 6px;font-size:14px;color:#aac'>Fan</h3>\n"
" <label>Enabled <input id=cf_fan_en type=checkbox></label>\n"
" <label>Mode\n"
" <select id=cf_fan_mode style='width:120px;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
"  <option value=0>Auto</option><option value=1>Manual</option>\n"
" </select></label>\n"
" <label>Manual PWM % <input id=cf_fan_manual type=range min=0 max=100></label>\n"
" <label>Temp source\n"
" <select id=cf_fan_tsrc style='width:120px;background:#111;color:#eee;border:1px solid #333;padding:6px;border-radius:4px'>\n"
"  <option value=0>Max CPU+Sys</option><option value=1>Avg CPU+Sys</option>\n"
"  <option value=2>CPU only</option><option value=3>Sys only</option>\n"
" </select></label>\n"
" <label>Hysteresis</label>\n"
" <input id=cf_fan_hyst type=number style='width:120px'>\n"
" <label>Min change %</label>\n"
" <input id=cf_fan_minchg type=number style='width:120px'>\n"
" <label>Min PWM %</label>\n"
" <input id=cf_fan_minpwm type=range min=0 max=100>\n"
" <label>Emergency temp</label>\n"
" <input id=cf_fan_emerg type=number style='width:120px'>\n"
" <label>Stall detect (s)</label>\n"
" <input id=cf_fan_stall type=number style='width:120px'>\n"
" <label>Ramp time (ms)</label>\n"
" <input id=cf_fan_ramp type=number style='width:120px'>\n"
" <label>Fan curve (5 points: temp &rarr; PWM %)</label>\n"
" <div id=cf_fan_curve style='display:grid;grid-template-columns:1fr 1fr 1fr 1fr 1fr;gap:4px'>\n"
"  <input id=cf_fc0t type=number placeholder='t0'><input id=cf_fc0p type=number placeholder='p0'>\n"
"  <input id=cf_fc1t type=number placeholder='t1'><input id=cf_fc1p type=number placeholder='p1'>\n"
"  <input id=cf_fc2t type=number placeholder='t2'><input id=cf_fc2p type=number placeholder='p2'>\n"
"  <input id=cf_fc3t type=number placeholder='t3'><input id=cf_fc3p type=number placeholder='p3'>\n"
"  <input id=cf_fc4t type=number placeholder='t4'><input id=cf_fc4p type=number placeholder='p4'>\n"
" </div>\n"
" <button id=cfsave style='margin-top:10px'>Save Settings</button>\n"
"</section>\n"
"<script>\n"
"let rr=1.0;\n"
"function f(p,o){return fetch(p,o).then(r=>r.json?r.json().catch(()=>{}):r)}\n"
"function setIdle(id,v){let e=document.getElementById(id);if(e!==document.activeElement)e.value=v}\n"
"let dragging=false,canvasW=640,canvasH=172;\n"
"function sizePx(sz){return [80,180,260,360][sz]||360}\n"
"function sizeH(sz){return [16,48,64,96][sz]||96}\n"
"function applyClockUI(s){\n"
" canvasW=s.canvas_w||640;canvasH=s.canvas_h||172;\n"
" if(document.activeElement!==document.getElementById('cks'))document.getElementById('cks').value=s.clock_size;\n"
" document.getElementById('cms').checked=!!s.show_ms;\n"
" document.getElementById('css').checked=!!s.show_seconds;\n"
" document.getElementById('ckon').checked=s.show_clock!==0;\n"
" if(document.activeElement!==document.getElementById('cktx'))document.getElementById('cktx').value=s.clock_text||'';\n"
" if(document.activeElement!==document.getElementById('bgm'))document.getElementById('bgm').value=s.bg_mode||0;\n"
" if(document.activeElement!==document.getElementById('bgurl'))document.getElementById('bgurl').value=s.bg_url||'';\n"
" if(document.activeElement!==document.getElementById('qsl'))document.getElementById('qsl').value=s.q_sym_l||'';\n"
" if(document.activeElement!==document.getElementById('qsr'))document.getElementById('qsr').value=s.q_sym_r||'';\n"
" if(document.activeElement!==document.getElementById('qref'))document.getElementById('qref').value=s.q_refresh_s||60;\n"
" let qu=s.q_up_rgba>>>0,qd=s.q_down_rgba>>>0;\n"
" let qur=(qu>>>24)&0xff,qug=(qu>>>16)&0xff,qub=(qu>>>8)&0xff;\n"
" let qdr=(qd>>>24)&0xff,qdg=(qd>>>16)&0xff,qdb=(qd>>>8)&0xff;\n"
" if(document.activeElement!==document.getElementById('quc'))document.getElementById('quc').value='#'+[qur,qug,qub].map(x=>x.toString(16).padStart(2,'0')).join('');\n"
" if(document.activeElement!==document.getElementById('qdc'))document.getElementById('qdc').value='#'+[qdr,qdg,qdb].map(x=>x.toString(16).padStart(2,'0')).join('');\n"
" if(document.activeElement!==document.getElementById('bgref'))document.getElementById('bgref').value=s.bg_refresh_s||0;\n"
" let cw=s.canvas_w||640,ch=s.canvas_h||172;document.getElementById('bgwh').textContent=cw+'x'+ch;document.getElementById('bgsize').textContent=cw*ch*2;\n"
" let bc=s.bg_color>>>0,br=(bc>>>24)&0xff,bg=(bc>>>16)&0xff,bb=(bc>>>8)&0xff;\n"
" if(document.activeElement!==document.getElementById('bgc'))document.getElementById('bgc').value='#'+[br,bg,bb].map(x=>x.toString(16).padStart(2,'0')).join('');\n"
" let r=(s.clock_rgba>>>24)&0xff,g=(s.clock_rgba>>>16)&0xff,b=(s.clock_rgba>>>8)&0xff,a=s.clock_rgba&0xff;\n"
" if(document.activeElement!==document.getElementById('ckc'))\n"
"  document.getElementById('ckc').value='#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');\n"
" if(document.activeElement!==document.getElementById('cka')){document.getElementById('cka').value=a;document.getElementById('ckal').textContent=a}\n"
" if(!dragging){positionBox(s.clock_x,s.clock_y,s.clock_size)}\n"
" document.getElementById('ckxv').textContent=s.clock_x;document.getElementById('ckyv').textContent=s.clock_y;\n"
"}\n"
"function positionBox(x,y,sz){\n"
" let m=document.getElementById('ckmap'),b=document.getElementById('ckbox');\n"
" let mw=m.clientWidth,mh=m.clientHeight,sx=mw/canvasW,sy=mh/canvasH;\n"
" let bw=sizePx(sz)*sx,bh=sizeH(sz)*sy;\n"
" b.style.width=bw+'px';b.style.height=bh+'px';\n"
" let cx=mw/2+x*sx,cy=mh/2+y*sy;\n"
" b.style.left=(cx-bw/2)+'px';b.style.top=(cy-bh/2)+'px';\n"
"}\n"
"function pushClock(extra){let fd=new URLSearchParams();for(let k in extra)fd.append(k,extra[k]);fetch('/api/clock',{method:'POST',body:fd})}\n"
"function pollState(){fetch('/api/state').then(r=>r.json()).then(s=>{\n"
" setIdle('br',s.brightness);document.getElementById('brl').textContent=s.brightness;\n"
" setIdle('vol',s.volume);document.getElementById('vl').textContent=s.volume;\n"
" setIdle('ds',s.dim_s);document.getElementById('dl').textContent=s.dim_s;\n"
" setIdle('os',s.off_s);document.getElementById('ol').textContent=s.off_s;\n"
" applyClockUI(s);\n"
" let pill='<span class=\"pill idle\">idle</span>';\n"
" if(s.recording)pill='<span class=\"pill rec\">REC '+s.elapsed+'s</span>';\n"
" else if(s.playing)pill='<span class=\"pill play\">Playing '+(s.uri||'').split('/').pop()+'</span>';\n"
" document.getElementById('status').innerHTML=pill;\n"
" document.getElementById('vul').style.width=Math.min(100,s.peak_l/327)+'%';\n"
" document.getElementById('vur').style.width=Math.min(100,s.peak_r/327)+'%';\n"
"}).catch(()=>{})}\n"
"setInterval(pollState,500);pollState();\n"
"function refScr(){document.getElementById('screen').src='/screen.bmp?'+Date.now()}\n"
"setInterval(refScr,rr*1000);\n"
"document.getElementById('rr').oninput=e=>{rr=+e.target.value;document.getElementById('rrl').textContent=rr.toFixed(1)};\n"
"function pushCfg(k,v){let fd=new URLSearchParams();fd.append(k,v);fetch('/api/cfg',{method:'POST',body:fd})}\n"
"document.getElementById('br').oninput=e=>{document.getElementById('brl').textContent=e.target.value;pushCfg('brightness',e.target.value)};\n"
"document.getElementById('vol').oninput=e=>{document.getElementById('vl').textContent=e.target.value;pushCfg('volume',e.target.value)};\n"
"document.getElementById('ds').oninput=e=>{document.getElementById('dl').textContent=e.target.value;pushCfg('dim_s',e.target.value)};\n"
"document.getElementById('os').oninput=e=>{document.getElementById('ol').textContent=e.target.value;pushCfg('off_s',e.target.value)};\n"
"document.getElementById('cks').onchange=e=>pushClock({size:e.target.value});\n"
"document.getElementById('cms').onchange=e=>pushClock({show_ms:e.target.checked?1:0});\n"
"document.getElementById('css').onchange=e=>pushClock({show_seconds:e.target.checked?1:0});\n"
"document.getElementById('ckon').onchange=e=>pushClock({show_clock:e.target.checked?1:0});\n"
"let txTimer;document.getElementById('cktx').oninput=e=>{clearTimeout(txTimer);txTimer=setTimeout(()=>pushClock({text:e.target.value}),250)};\n"
"document.getElementById('ckcenter').onclick=()=>pushClock({x:0,y:0});\n"
"function pushBg(o){let fd=new URLSearchParams();for(let k in o)fd.append(k,o[k]);fetch('/api/bg',{method:'POST',body:fd})}\n"
"document.getElementById('bgm').onchange=e=>pushBg({mode:e.target.value});\n"
"let bgUrlT;document.getElementById('bgurl').oninput=e=>{clearTimeout(bgUrlT);bgUrlT=setTimeout(()=>pushBg({url:e.target.value}),400)};\n"
"document.getElementById('bgref').onchange=e=>pushBg({refresh_s:e.target.value});\n"
"document.getElementById('bgc').onchange=e=>{let h=e.target.value;let r=parseInt(h.substr(1,2),16),g=parseInt(h.substr(3,2),16),b=parseInt(h.substr(5,2),16);pushBg({color:(((r<<24)|(g<<16)|(b<<8)|0xff)>>>0)})};\n"
"document.getElementById('bgfetch').onclick=async()=>{let s=document.getElementById('bgfetchstat');s.textContent='fetching...';try{let r=await fetch('/api/bg/fetch',{method:'POST'});let j=await r.json();s.textContent=j.ok?'fetched':('failed: '+(j.err||r.status))}catch(e){s.textContent='failed'}};\n"
"function pushQuotes(o){let fd=new URLSearchParams();for(let k in o)fd.append(k,o[k]);fetch('/api/quotes',{method:'POST',body:fd})}\n"
"let qslT;document.getElementById('qsl').oninput=e=>{clearTimeout(qslT);qslT=setTimeout(()=>pushQuotes({sl:e.target.value}),400)};\n"
"let qsrT;document.getElementById('qsr').oninput=e=>{clearTimeout(qsrT);qsrT=setTimeout(()=>pushQuotes({sr:e.target.value}),400)};\n"
"document.getElementById('qref').onchange=e=>pushQuotes({refresh_s:e.target.value});\n"
"document.getElementById('quc').onchange=e=>{let h=e.target.value;let r=parseInt(h.substr(1,2),16),g=parseInt(h.substr(3,2),16),b=parseInt(h.substr(5,2),16);pushQuotes({up_rgba:(((r<<24)|(g<<16)|(b<<8)|0xff)>>>0)})};\n"
"document.getElementById('qdc').onchange=e=>{let h=e.target.value;let r=parseInt(h.substr(1,2),16),g=parseInt(h.substr(3,2),16),b=parseInt(h.substr(5,2),16);pushQuotes({down_rgba:(((r<<24)|(g<<16)|(b<<8)|0xff)>>>0)})};\n"
"document.getElementById('qfetch').onclick=async()=>{let s=document.getElementById('qfetchstat');s.textContent='fetching...';try{let r=await fetch('/api/quotes/fetch',{method:'POST'});let j=await r.json();s.textContent=j.ok?'kicked':('failed: '+(j.err||r.status))}catch(e){s.textContent='failed'}};\n"
/* Convert the picked image: load -> draw to a canvas of canvas_w x
   canvas_h -> read pixels -> pack RGB565 -> byte-swap to panel order
   -> POST to /api/bg/upload as raw binary. */
"document.getElementById('bgfile').onchange=async e=>{\n"
" let f=e.target.files[0];if(!f)return;\n"
" let st=await fetch('/api/state').then(r=>r.json());\n"
" let cw=st.canvas_w||640,ch=st.canvas_h||172;\n"
" let img=new Image();img.src=URL.createObjectURL(f);\n"
" await new Promise((res,rej)=>{img.onload=res;img.onerror=rej});\n"
" let cv=document.createElement('canvas');cv.width=cw;cv.height=ch;\n"
" cv.getContext('2d').drawImage(img,0,0,cw,ch);\n"
" let id=cv.getContext('2d').getImageData(0,0,cw,ch).data;\n"
" let out=new Uint8Array(cw*ch*2);\n"
" for(let i=0,j=0;i<id.length;i+=4,j+=2){\n"
"  let r=id[i]>>3,g=id[i+1]>>2,b=id[i+2]>>3;\n"
"  let p=(r<<11)|(g<<5)|b;\n"
   /* Panel uses LV_COLOR_16_SWAP (high byte first per pixel). */
"  out[j]=(p>>8)&0xff;out[j+1]=p&0xff;\n"
" }\n"
" document.getElementById('bgstat').textContent='uploading '+out.length+' bytes...';\n"
" let resp=await fetch('/api/bg/upload',{method:'POST',body:out,headers:{'Content-Type':'application/octet-stream'}});\n"
" document.getElementById('bgstat').textContent=resp.ok?'uploaded -- pick \"Custom upload\" mode to display.':'upload failed: '+resp.status;\n"
"};\n"
"function rgbaFromInputs(){let h=document.getElementById('ckc').value;let r=parseInt(h.substr(1,2),16),g=parseInt(h.substr(3,2),16),b=parseInt(h.substr(5,2),16);let a=+document.getElementById('cka').value;return ((r<<24)|(g<<16)|(b<<8)|a)>>>0}\n"
"document.getElementById('ckc').onchange=()=>pushClock({rgba:rgbaFromInputs()});\n"
"document.getElementById('cka').oninput=e=>{document.getElementById('ckal').textContent=e.target.value;pushClock({rgba:rgbaFromInputs()})};\n"
"(()=>{let m=document.getElementById('ckmap'),b=document.getElementById('ckbox'),sx,sy,startX,startY,boxX,boxY;\n"
" function down(e){dragging=true;let t=(e.touches?e.touches[0]:e);let mw=m.clientWidth,mh=m.clientHeight;sx=mw/canvasW;sy=mh/canvasH;startX=t.clientX;startY=t.clientY;boxX=parseInt(b.style.left);boxY=parseInt(b.style.top);e.preventDefault()}\n"
" function move(e){if(!dragging)return;let t=(e.touches?e.touches[0]:e);b.style.left=(boxX+t.clientX-startX)+'px';b.style.top=(boxY+t.clientY-startY)+'px';let mw=m.clientWidth,mh=m.clientHeight;let cx=parseInt(b.style.left)+b.clientWidth/2,cy=parseInt(b.style.top)+b.clientHeight/2;let x=Math.round((cx-mw/2)/sx),y=Math.round((cy-mh/2)/sy);document.getElementById('ckxv').textContent=x;document.getElementById('ckyv').textContent=y}\n"
" function up(e){if(!dragging)return;dragging=false;let mw=m.clientWidth,mh=m.clientHeight;let cx=parseInt(b.style.left)+b.clientWidth/2,cy=parseInt(b.style.top)+b.clientHeight/2;let x=Math.round((cx-mw/2)/sx),y=Math.round((cy-mh/2)/sy);pushClock({x:x,y:y})}\n"
" b.addEventListener('mousedown',down);window.addEventListener('mousemove',move);window.addEventListener('mouseup',up);\n"
" b.addEventListener('touchstart',down);window.addEventListener('touchmove',move);window.addEventListener('touchend',up);\n"
"})();\n"
"document.getElementById('brec').onclick=()=>fetch('/api/rec/start',{method:'POST'}).then(loadList);\n"
"document.getElementById('bstop').onclick=()=>fetch('/api/rec/stop',{method:'POST'}).then(loadList);\n"
"document.getElementById('bpstop').onclick=()=>fetch('/api/stop',{method:'POST'});\n"
"function loadList(){fetch('/api/list').then(r=>r.json()).then(j=>{\n"
" let t=document.getElementById('tbl');t.innerHTML='<tr><th>name</th><th>size</th><th>dur</th><th></th></tr>';\n"
" j.files.forEach(f=>{let r=t.insertRow();r.insertCell().textContent=f.name;\n"
"  r.insertCell().textContent=(f.bytes>1048576?(f.bytes/1048576).toFixed(1)+' MB':(f.bytes>>10)+' KB');\n"
"  r.insertCell().textContent=(f.duration_ms/1000).toFixed(1)+'s';\n"
"  let c=r.insertCell();c.innerHTML='<a href=\"/rec/'+f.name+'\">DL</a>'+\n"
"   ' <button onclick=\"fetch(\\'/api/play\\',{method:\\'POST\\',body:\\'file://sdcard/recordings/'+f.name+'\\'})\">play</button>';\n"
" })\n"
"}).catch(()=>{})}\n"
"loadList();\n"
"/* ---------- WiFi ---------- */\n"
"let wifiAuth='';\n"
"let isScanning=false;\n"
"function setWifiAuth(u,p){wifiAuth='Basic '+btoa(u+':'+p)}\n"
"function wifiFetch(url,opts){opts=opts||{};opts.headers=opts.headers||{};if(wifiAuth)opts.headers['Authorization']=wifiAuth;return fetch(url,opts)}\n"
"function loadWifiStatus(){if(isScanning)return;wifiFetch('/api/wifi/status').then(r=>{\n"
"  if(!r.ok){throw new Error('HTTP '+r.status);}\n"
"  return r.json();\n"
"}).then(s=>{\n"
"  let html='<strong>State:</strong> '+s.state;\n"
"  if(s.ssid)html+=' | <strong>SSID:</strong> '+s.ssid;\n"
"  if(s.ip)html+=' | <strong>IP:</strong> '+s.ip;\n"
"  if(s.rssi)html+=' | <strong>RSSI:</strong> '+s.rssi+' dBm ('+s.quality+'%)';\n"
"  if(s.channel)html+=' | <strong>Channel:</strong> '+s.channel;\n"
"  if(s.ap_active)html+=' | <strong>AP:</strong> active';\n"
"  document.getElementById('wifistatus').innerHTML=html;\n"
"}).catch(e=>{document.getElementById('wifistatus').textContent='status error: '+e.message})}\n"
"function loadWifiNetworks(){if(isScanning)return;wifiFetch('/api/wifi/networks').then(r=>r.json()).then(j=>{\n"
"  let d=document.getElementById('wifisaved');\n"
"  if(!j.networks||j.networks.length===0){d.innerHTML='<span class=meta>No saved networks</span>';return}\n"
"  let html='';\n"
"  j.networks.forEach(n=>{html+='<div style=\"padding:6px 0;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center\"><span>'+n.ssid+' <span class=meta>prio:'+n.priority+'</span></span><button onclick=\"wifiConnect(\\''+n.ssid+'\\')\">Connect</button> <button class=warn onclick=\"wifiRemove(\\''+n.ssid+'\\')\">Del</button></div>'});\n"
"  d.innerHTML=html;\n"
"}).catch(()=>{})}\n"
"function wifiScan(){if(isScanning)return;isScanning=true;\n"
"  document.getElementById('wifiscanres').innerHTML='<span class=meta>Scanning... (may take 10+ seconds)</span>';\n"
"  wifiFetch('/api/wifi/scan').then(r=>{\n"
"   if(!r.ok){throw new Error('HTTP '+r.status);}\n"
"   return r.json();\n"
"  }).then(j=>{\n"
"   let d=document.getElementById('wifiscanres');\n"
"   if(!j.networks||j.networks.length===0){d.innerHTML='<span class=meta>No networks found (empty result)</span>';return}\n"
"   let html='';\n"
"   j.networks.forEach(n=>{html+='<div style=\"padding:6px 0;border-bottom:1px solid #333;cursor:pointer\" onclick=\"document.getElementById(\\'wifissid\\').value=\\''+n.ssid+'\\'\">'+n.ssid+' <span class=meta>('+n.rssi+' dBm, '+n.auth+')</span></div>'});\n"
"   d.innerHTML=html;\n"
"  }).catch(e=>{document.getElementById('wifiscanres').textContent='scan failed: '+e.message})\n"
"  .finally(()=>{isScanning=false;loadWifiStatus();loadWifiNetworks()})}\n"
"function wifiConnect(ssid){wifiFetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid})}).then(()=>{setTimeout(loadWifiStatus,2000);setTimeout(loadWifiStatus,5000)})}\n"
"function wifiRemove(ssid){if(!confirm('Remove '+ssid+'?'))return;wifiFetch('/api/wifi/networks/'+encodeURIComponent(ssid),{method:'DELETE'}).then(()=>{loadWifiNetworks()})}\n"
"document.getElementById('wifiscan').onclick=wifiScan;\n"
"document.getElementById('wifidisconnect').onclick=()=>{wifiFetch('/api/wifi/disconnect',{method:'POST'}).then(()=>loadWifiStatus())};\n"
"document.getElementById('wifiadd').onclick=()=>{\n"
"  let ssid=document.getElementById('wifissid').value;\n"
"  let pass=document.getElementById('wifipass').value;\n"
"  let prio=+document.getElementById('wifiprio').value;\n"
"  if(!ssid){alert('SSID required');return}\n"
"  wifiFetch('/api/wifi/networks',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass,priority:prio})})\n"
"   .then(r=>r.json()).then(j=>{\n"
"    if(j.status==='ok'){wifiConnect(ssid);loadWifiNetworks()}\n"
"    else{alert('Add failed: '+(j.error||'unknown'))}\n"
"   }).catch(e=>alert('Add failed: '+e))\n"
"};\n"
"setWifiAuth('admin','admin');\n"
"setInterval(loadWifiStatus,5000);loadWifiStatus();loadWifiNetworks();\n"
"/* Device Settings: load + save */\n"
"function loadSettings(){fetch('/api/settings').then(r=>r.json()).then(j=>{\n"
" document.getElementById('cf_ssid').value=j.wifi_ssid||'';\n"
" document.getElementById('cf_nas_type').value=j.nas_type||'mock';\n"
" document.getElementById('cf_nas_ip').value=j.nas_ip||'';\n"
" document.getElementById('cf_nas_port').value=j.nas_port||0;\n"
" document.getElementById('cf_nas_user').value=j.nas_user||'';\n"
" document.getElementById('cf_snmp_comm').value=j.snmp_comm||'';\n"
" document.getElementById('cf_snmp_ver').value=j.snmp_ver||2;\n"
" document.getElementById('cf_serial_baud').value=j.serial_baud||115200;\n"
" document.getElementById('cf_poll_sec').value=j.poll_sec||5;\n"
" document.getElementById('cf_rotation').value=j.rotation_angle||0;\n"
" document.getElementById('cf_autodim').checked=!!j.autodim;\n"
" document.getElementById('cf_nas_https').checked=!!j.nas_https;\n"
" document.getElementById('cf_tz').value=j.timezone||0;\n"
" document.getElementById('cf_cycle_en').checked=!!j.auto_cycle_enabled;\n"
" document.getElementById('cf_cycle_int').value=j.auto_cycle_interval_sec||10;\n"
" document.getElementById('cf_sata').value=j.sata_disk_count||0;\n"
" document.getElementById('cf_m2').value=j.m2_disk_count||0;\n"
" document.getElementById('cf_wkey').value=j.weather_api_key||'';\n"
" document.getElementById('cf_wcity').value=j.weather_city||'';\n"
" document.getElementById('cf_fan_en').checked=!!j.fan_enabled;\n"
" document.getElementById('cf_fan_mode').value=j.fan_mode||0;\n"
" document.getElementById('cf_fan_manual').value=j.fan_manual_pct||0;\n"
" document.getElementById('cf_fan_tsrc').value=j.fan_temp_source||0;\n"
" document.getElementById('cf_fan_hyst').value=j.fan_hysteresis||0;\n"
" document.getElementById('cf_fan_minchg').value=j.fan_min_change_pct||0;\n"
" document.getElementById('cf_fan_minpwm').value=j.fan_min_pwm_pct||0;\n"
" document.getElementById('cf_fan_emerg').value=j.fan_emergency_temp||0;\n"
" document.getElementById('cf_fan_stall').value=j.fan_stall_detect_sec||0;\n"
" document.getElementById('cf_fan_ramp').value=j.fan_ramp_time_ms||0;\n"
" if(j.fan_curve){for(let i=0;i<5;i++){if(j.fan_curve[i]){\n"
"  document.getElementById('cf_fc'+i+'t').value=j.fan_curve[i].temp;\n"
"  document.getElementById('cf_fc'+i+'p').value=j.fan_curve[i].pct;\n"
" }}}\n"
" document.getElementById('dsstat').textContent='Loaded.';\n"
"}).catch(e=>{document.getElementById('dsstat').textContent='Load failed: '+e.message})}\n"
"document.getElementById('cfsave').onclick=()=>{\n"
" let p=new URLSearchParams();\n"
" p.set('wifi_ssid',document.getElementById('cf_ssid').value);\n"
" p.set('wifi_pass',document.getElementById('cf_wifipass').value);\n"
" p.set('nas_type',document.getElementById('cf_nas_type').value);\n"
" p.set('nas_ip',document.getElementById('cf_nas_ip').value);\n"
" p.set('nas_port',document.getElementById('cf_nas_port').value);\n"
" p.set('nas_user',document.getElementById('cf_nas_user').value);\n"
" p.set('nas_pass',document.getElementById('cf_nas_pass').value);\n"
" p.set('nas_https',document.getElementById('cf_nas_https').checked?'1':'0');\n"
" p.set('snmp_comm',document.getElementById('cf_snmp_comm').value);\n"
" p.set('snmp_ver',document.getElementById('cf_snmp_ver').value);\n"
" p.set('serial_baud',document.getElementById('cf_serial_baud').value);\n"
" p.set('poll_sec',document.getElementById('cf_poll_sec').value);\n"
" p.set('rotation_angle',document.getElementById('cf_rotation').value);\n"
" p.set('autodim',document.getElementById('cf_autodim').checked?'1':'0');\n"
" p.set('timezone',document.getElementById('cf_tz').value);\n"
" p.set('auto_cycle_enabled',document.getElementById('cf_cycle_en').checked?'1':'0');\n"
" p.set('auto_cycle_interval_sec',document.getElementById('cf_cycle_int').value);\n"
" p.set('sata_disk_count',document.getElementById('cf_sata').value);\n"
" p.set('m2_disk_count',document.getElementById('cf_m2').value);\n"
" p.set('weather_api_key',document.getElementById('cf_wkey').value);\n"
" p.set('weather_city',document.getElementById('cf_wcity').value);\n"
" p.set('fan_enabled',document.getElementById('cf_fan_en').checked?'1':'0');\n"
" p.set('fan_mode',document.getElementById('cf_fan_mode').value);\n"
" p.set('fan_manual_pct',document.getElementById('cf_fan_manual').value);\n"
" p.set('fan_temp_source',document.getElementById('cf_fan_tsrc').value);\n"
" p.set('fan_hysteresis',document.getElementById('cf_fan_hyst').value);\n"
" p.set('fan_min_change_pct',document.getElementById('cf_fan_minchg').value);\n"
" p.set('fan_min_pwm_pct',document.getElementById('cf_fan_minpwm').value);\n"
" p.set('fan_emergency_temp',document.getElementById('cf_fan_emerg').value);\n"
" p.set('fan_stall_detect_sec',document.getElementById('cf_fan_stall').value);\n"
" p.set('fan_ramp_time_ms',document.getElementById('cf_fan_ramp').value);\n"
" for(let i=0;i<5;i++){p.set('fan_curve_'+i+'_temp',document.getElementById('cf_fc'+i+'t').value);p.set('fan_curve_'+i+'_pct',document.getElementById('cf_fc'+i+'p').value)}\n"
" document.getElementById('dsstat').textContent='Saving...';\n"
" fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})\n"
"  .then(r=>r.json()).then(j=>{\n"
"   document.getElementById('dsstat').textContent=j.reboot_required?'Saved. Reboot required for some changes.':'Saved.';\n"
"  }).catch(e=>{document.getElementById('dsstat').textContent='Save failed: '+e.message})\n"
"};\n"
"loadSettings();\n"
"</script></body></html>\n";

static esp_err_t h_index(httpd_req_t *r)
{
    return send_str(r, "text/html; charset=utf-8", k_index_html);
}

static esp_err_t h_root_redirect(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "/ui");
    httpd_resp_send(r, NULL, 0);
    return ESP_OK;
}

/* ---------- /api/state ---------- */

static esp_err_t h_state(httpd_req_t *r)
{
    /* Read current peaks WITHOUT resetting them; the LVGL UI shares
       these readers, but here we just want a snapshot. To avoid double-
       reading, we cheat and read them anyway -- the on-device VU also
       reads-and-resets so a 500ms web poll just trades samples between
       the two consumers. */
    uint16_t pl_in = 0, pr_in = 0, pl_out = 0, pr_out = 0;
    recorder_peak_lr(&pl_in, &pr_in);
    radio_out_peak(&pl_out, &pr_out);
    bool playing = radio_is_playing();
    bool recording = recorder_is_recording();
    /* Pick the active source for the VU values reported to the page. */
    uint16_t pl = playing ? pl_out : pl_in;
    uint16_t pr = playing ? pr_out : pr_in;

    /* Inline the JSON-escape twice (C, no closures) for clock_text
       and bg_url. Strip control chars, escape " and \. */
    char ct_esc[96];
    {
        const char *s = app_cfg_get_clock_text();
        size_t o = 0;
        while (s && *s && o < sizeof(ct_esc) - 2) {
            char c = *s++;
            if (c == '"' || c == '\\') {
                if (o + 2 >= sizeof(ct_esc) - 1) break;
                ct_esc[o++] = '\\';
            } else if ((unsigned char)c < 0x20) continue;
            ct_esc[o++] = c;
        }
        ct_esc[o] = 0;
    }
    char bgu_esc[256];
    {
        const char *s = app_cfg_get_bg_url();
        size_t o = 0;
        while (s && *s && o < sizeof(bgu_esc) - 2) {
            char c = *s++;
            if (c == '"' || c == '\\') {
                if (o + 2 >= sizeof(bgu_esc) - 1) break;
                bgu_esc[o++] = '\\';
            } else if ((unsigned char)c < 0x20) continue;
            bgu_esc[o++] = c;
        }
        bgu_esc[o] = 0;
    }
    char json[1280];
    int n = snprintf(json, sizeof(json),
        "{\"recording\":%d,\"elapsed\":%u,"
        "\"playing\":%d,\"uri\":\"%s\","
        "\"peak_l\":%u,\"peak_r\":%u,"
        "\"brightness\":%d,\"volume\":%d,\"dim_s\":%d,\"off_s\":%d,"
        "\"clock_x\":%d,\"clock_y\":%d,\"clock_size\":%d,"
        "\"clock_rgba\":%u,\"show_ms\":%d,\"show_seconds\":%d,"
        "\"show_clock\":%d,\"clock_text\":\"%s\","
        "\"bg_mode\":%d,\"bg_refresh_s\":%d,\"bg_url\":\"%s\","
        "\"bg_color\":%u,\"canvas_w\":%d,\"canvas_h\":%d,"
        "\"q_sym_l\":\"%s\",\"q_sym_r\":\"%s\","
        "\"q_refresh_s\":%d,\"q_up_rgba\":%u,\"q_down_rgba\":%u}",
        (int)recording, recorder_elapsed_s(),
        (int)playing,   radio_current_uri() ? radio_current_uri() : "",
        (unsigned)pl, (unsigned)pr,
        app_cfg_get_brightness(),
        radio_get_volume(),
        app_cfg_get_dim_s(),
        app_cfg_get_off_s(),
        app_cfg_get_clock_x(),
        app_cfg_get_clock_y(),
        app_cfg_get_clock_size(),
        (unsigned)app_cfg_get_clock_rgba(),
        app_cfg_get_show_ms(),
        app_cfg_get_show_seconds(),
        app_cfg_get_show_clock(),
        ct_esc,
        app_cfg_get_bg_mode(),
        app_cfg_get_bg_refresh_s(),
        bgu_esc,
        (unsigned)app_cfg_get_bg_color(),
        app_cfg_get_canvas_w(),
        app_cfg_get_canvas_h(),
        app_cfg_get_quotes_sym_l(),
        app_cfg_get_quotes_sym_r(),
        app_cfg_get_quotes_refresh_s(),
        (unsigned)app_cfg_get_quotes_up_rgba(),
        (unsigned)app_cfg_get_quotes_down_rgba());
    (void)n;
    return send_str(r, "application/json", json);
}

/* ---------- /api/list ---------- */

static esp_err_t h_list(httpd_req_t *r)
{
    static char names[16][64];
    int count = recorder_list(names, 16);
    /* Build JSON. ~64 entries * (name + 80 bytes meta) = ~9 KB max. */
    static char json[4096];
    int p = 0;
    p += snprintf(json + p, sizeof(json) - p, "{\"files\":[");
    for (int i = 0; i < count; i++) {
        uint32_t bytes = 0, dur_ms = 0;
        recorder_file_info(names[i], &bytes, &dur_ms);
        p += snprintf(json + p, sizeof(json) - p,
                      "%s{\"name\":\"%s\",\"bytes\":%u,\"duration_ms\":%u}",
                      i ? "," : "",
                      names[i], (unsigned)bytes, (unsigned)dur_ms);
        if (p > (int)sizeof(json) - 100) break;
    }
    p += snprintf(json + p, sizeof(json) - p, "]}");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, json, p);
}

/* ---------- /api/cfg ---------- */

static int form_int(const char *body, const char *key, int dflt)
{
    /* Tiny urlencoded form parser. body looks like "brightness=120&volume=70" */
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&') && p[klen] == '=') {
            return atoi(p + klen + 1);
        }
        p++;
    }
    return dflt;
}

/* form_str is defined later (near /api/quotes). Forward-declare so
   form_bool can use it here. Returns 1 on success, 0 if key absent. */
static int form_str(const char *body, const char *key, char *dst, size_t dst_sz);

/* Parse a boolean field: accepts 0/1/on/true (case-insensitive via first char). */
static bool form_bool(const char *body, const char *key, bool dflt)
{
    char buf[8];
    if (!form_str(body, key, buf, sizeof(buf))) return dflt;
    if (buf[0]=='1' || buf[0]=='t' || buf[0]=='T' || buf[0]=='o' || buf[0]=='O') return true;
    return false;
}

/* Check whether a key is present in the body (even if empty). */
static bool form_has(const char *body, const char *key)
{
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || p[-1] == '&') && p[klen] == '=') return true;
        p++;
    }
    return false;
}

static esp_err_t h_cfg(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[256];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "empty");

    /* Brightness / dim accept "missing" as "don't change". The form
       posts only the slider that moved, so we have to detect presence. */
    if (strstr(body, "brightness=")) {
        app_cfg_set_brightness(form_int(body, "brightness", 128));
    }
    if (strstr(body, "volume=")) {
        radio_set_volume(form_int(body, "volume", 70));
    }
    if (strstr(body, "dim_s=") || strstr(body, "off_s=")) {
        /* dim_off setter takes both at once; we need the current value
           if only one was sent. Cheap workaround: re-apply both from
           form, defaulting to 0. */
        int dim_s = form_int(body, "dim_s", 0);
        int off_s = form_int(body, "off_s", 0);
        app_cfg_set_dim_off(dim_s, off_s);
    }
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* ---------- /api/clock ---------- */

/* form keys: x, y (pixel offset from screen center), size (0..3),
   rgba (uint32), show_ms (0/1). Each is optional; missing keys leave
   the corresponding cfg field unchanged. */
static esp_err_t h_clock(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[256];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "empty");
    if (strstr(body, "x=") || strstr(body, "y=")) {
        int x = form_int(body, "x", app_cfg_get_clock_x());
        int y = form_int(body, "y", app_cfg_get_clock_y());
        app_cfg_set_clock_pos(x, y);
    }
    if (strstr(body, "size=")) {
        app_cfg_set_clock_size(form_int(body, "size", 3));
    }
    if (strstr(body, "rgba=")) {
        /* form_int is signed int; rgba up to 0xFFFFFFFF needs unsigned
           parse via strtoul. Find the value. */
        const char *p = strstr(body, "rgba=");
        if (p) {
            unsigned long v = strtoul(p + 5, NULL, 0);
            app_cfg_set_clock_rgba((uint32_t)v);
        }
    }
    if (strstr(body, "show_ms=")) {
        app_cfg_set_show_ms(form_int(body, "show_ms", 0));
    }
    if (strstr(body, "show_seconds=")) {
        app_cfg_set_show_seconds(form_int(body, "show_seconds", 1));
    }
    if (strstr(body, "show_clock=")) {
        app_cfg_set_show_clock(form_int(body, "show_clock", 1));
    }
    /* URL-decoded custom text. body parser is naive: find "text=",
       copy until '&' or end, %XX-decode and '+' -> ' '. */
    const char *tp = strstr(body, "text=");
    if (tp) {
        tp += 5;
        char dec[64];
        size_t o = 0;
        while (*tp && *tp != '&' && o < sizeof(dec) - 1) {
            char c = *tp++;
            if (c == '+') c = ' ';
            else if (c == '%' && tp[0] && tp[1]) {
                char hex[3] = { tp[0], tp[1], 0 };
                c = (char)strtol(hex, NULL, 16);
                tp += 2;
            }
            dec[o++] = c;
        }
        dec[o] = 0;
        app_cfg_set_clock_text(dec);
    }
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* ---------- /api/bg ---------- */

/* form keys: mode (0=sun,1=upload,2=url), url, refresh_s. */
static esp_err_t h_bg(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[400];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "empty");
    if (strstr(body, "mode=")) {
        app_cfg_set_bg_mode(form_int(body, "mode", 0));
    }
    if (strstr(body, "refresh_s=")) {
        app_cfg_set_bg_refresh_s(form_int(body, "refresh_s", 0));
    }
    if (strstr(body, "color=")) {
        const char *p = strstr(body, "color=");
        if (p) {
            unsigned long v = strtoul(p + 6, NULL, 0);
            app_cfg_set_bg_color((uint32_t)v);
        }
    }
    /* URL-decode the url field. Same naive parser as h_clock's text. */
    const char *up = strstr(body, "url=");
    if (up) {
        up += 4;
        char dec[180];
        size_t o = 0;
        while (*up && *up != '&' && o < sizeof(dec) - 1) {
            char c = *up++;
            if (c == '+') c = ' ';
            else if (c == '%' && up[0] && up[1]) {
                char hex[3] = { up[0], up[1], 0 };
                c = (char)strtol(hex, NULL, 16);
                up += 2;
            }
            dec[o++] = c;
        }
        dec[o] = 0;
        app_cfg_set_bg_url(dec);
    }
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* POST raw RGB565 pixels to be saved as the custom clock background.
   Body must be exactly canvas_w*canvas_h*2 bytes. The uploader (the
   webui page) is responsible for rendering the image to that
   resolution and byte-swapping pixels for the panel. */
#define CLOCK_BG_PATH_LOCAL "/sdcard/clock_bg.bin"
static esp_err_t h_bg_upload(httpd_req_t *r)
{
    CHECK_AUTH(r);
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "sd not mounted");
        return ESP_FAIL;
    }
    int need = app_cfg_get_canvas_w() * app_cfg_get_canvas_h() * 2;
    if (r->content_len != (size_t)need) {
        char msg[80];
        snprintf(msg, sizeof(msg), "expect %d bytes (RGB565)", need);
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, msg);
        return ESP_FAIL;
    }
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%s.part", CLOCK_BG_PATH_LOCAL);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
        return ESP_FAIL;
    }
    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return ESP_FAIL; }
    int got = 0;
    while (got < need) {
        int chunk = need - got > 4096 ? 4096 : need - got;
        int rc = httpd_req_recv(r, buf, chunk);
        if (rc <= 0) break;
        if (fwrite(buf, 1, rc, f) != (size_t)rc) break;
        got += rc;
    }
    free(buf);
    fclose(f);
    if (got != need) {
        unlink(tmp);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "short read");
        return ESP_FAIL;
    }
    rename(tmp, CLOCK_BG_PATH_LOCAL);
    /* If the user is currently in upload mode, repaint immediately. */
    if (app_cfg_get_bg_mode() == 1) app_cfg_clock_bg_reload();
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* Manual "fetch now" trigger for URL background mode. Kicks the
   fetcher; the actual download happens on a background task. */
static esp_err_t h_bg_fetch(httpd_req_t *r)
{
    CHECK_AUTH(r);
    if (app_cfg_get_bg_mode() != 2) {
        return send_str(r, "application/json",
                        "{\"ok\":false,\"err\":\"not in URL mode\"}");
    }
    const char *u = app_cfg_get_bg_url();
    if (!u || !u[0]) {
        return send_str(r, "application/json",
                        "{\"ok\":false,\"err\":\"empty URL\"}");
    }
    app_cfg_bg_fetch_now();
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* ---------- /api/quotes ---------- */

/* Extract a single form field's raw value (no URL-decoding needed --
   symbols are alnum). Writes up to dst_sz-1 bytes to dst. Returns 1 on
   success. */
static int form_str(const char *body, const char *key, char *dst, size_t dst_sz)
{
    if (!body || !key || !dst || dst_sz == 0) return 0;
    char pat[24];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    /* Ensure it's an actual key boundary (start-of-body or '&'). */
    if (p != body && p[-1] != '&') {
        /* find the next occurrence on a boundary */
        do {
            p = strstr(p + 1, pat);
            if (!p) return 0;
        } while (p != body && p[-1] != '&');
    }
    p += strlen(pat);
    size_t o = 0;
    while (*p && *p != '&' && o < dst_sz - 1) {
        char c = *p++;
        if (c == '+') c = ' ';
        else if (c == '%' && p[0] && p[1]) {
            char hex[3] = { p[0], p[1], 0 };
            c = (char)strtol(hex, NULL, 16);
            p += 2;
        }
        dst[o++] = c;
    }
    dst[o] = 0;
    return 1;
}

static esp_err_t h_quotes(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[400];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "empty");
    char tmp[32];
    if (form_str(body, "sl", tmp, sizeof(tmp))) {
        app_cfg_set_quotes_sym_l(tmp);
    }
    if (form_str(body, "sr", tmp, sizeof(tmp))) {
        app_cfg_set_quotes_sym_r(tmp);
    }
    if (strstr(body, "refresh_s=")) {
        app_cfg_set_quotes_refresh_s(form_int(body, "refresh_s", 60));
    }
    if (strstr(body, "up_rgba=")) {
        const char *p = strstr(body, "up_rgba=") + 8;
        app_cfg_set_quotes_up_rgba((uint32_t)strtoul(p, NULL, 0));
    }
    if (strstr(body, "down_rgba=")) {
        const char *p = strstr(body, "down_rgba=") + 10;
        app_cfg_set_quotes_down_rgba((uint32_t)strtoul(p, NULL, 0));
    }
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* Manual "fetch now" -- nudges the quotes poll task. */
static esp_err_t h_quotes_fetch(httpd_req_t *r)
{
    CHECK_AUTH(r);
    (void)r;
    /* Reuse the setter side-effect: setting the left symbol to its
       current value triggers quotes_kick() inside main.cpp. Cheap and
       avoids exposing yet another extern. */
    app_cfg_set_quotes_sym_l(app_cfg_get_quotes_sym_l());
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* Programmatic tile-switch (POST /api/goto with body "tile=N"). Lets
   the webui jump straight to a page from a phone, and lets test
   scripts grab a screenshot of any specific tile. */
static esp_err_t h_goto(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[64];
    int n = read_body(r, body, sizeof(body));
    int t = 0;
    if (n > 0) t = form_int(body, "tile", 0);
    app_cfg_set_active_tile(t);
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* ---------- /api/rec start/stop and /api/play|stop ---------- */

static esp_err_t h_rec_start(httpd_req_t *r)
{
    CHECK_AUTH(r);
    const char *path = NULL;
    esp_err_t e = recorder_start(&path);
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":%s,\"path\":\"%s\"}",
                     e == ESP_OK ? "true" : "false", path ? path : "");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_rec_stop(httpd_req_t *r)
{
    CHECK_AUTH(r);
    recorder_stop();
    return send_str(r, "application/json", "{\"ok\":true}");
}

static esp_err_t h_play(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char body[300];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "missing uri");
    radio_play(body);
    return send_str(r, "application/json", "{\"ok\":true}");
}

static esp_err_t h_stop(httpd_req_t *r)
{
    CHECK_AUTH(r);
    radio_stop();
    return send_str(r, "application/json", "{\"ok\":true}");
}

/* ---------- /rec/<file> ---------- */

static esp_err_t h_rec_get(httpd_req_t *r)
{
    /* URI looks like /rec/20260510-160430.wav */
    const char *name = r->uri + strlen("/rec/");
    if (!*name || strchr(name, '/') || strchr(name, '\\')) {
        httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "bad name");
        return ESP_FAIL;
    }
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "sd not mounted");
        return ESP_FAIL;
    }
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/recordings/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(r, "audio/wav");
    char disp[160];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(r, "Content-Disposition", disp);
    /* Stream in chunks. ~16 KB chunks balance throughput vs RAM use. */
    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return ESP_FAIL; }
    size_t got;
    while ((got = fread(buf, 1, 8192, f)) > 0) {
        if (httpd_resp_send_chunk(r, buf, got) != ESP_OK) break;
    }
    httpd_resp_send_chunk(r, NULL, 0);
    free(buf);
    fclose(f);
    return ESP_OK;
}

/* ---------- /screen.bmp ---------- */

/* Minimal BMP writer for RGB565 framebuffer. Browsers accept BMP +
   BITFIELDS bitmask info header. We avoid the upside-down convention
   by writing height as negative (origin at top-left). */
static esp_err_t h_screen_bmp(httpd_req_t *r)
{
    if (!s_fb || s_fb_w <= 0 || s_fb_h <= 0) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "fb not set");
        return ESP_FAIL;
    }
    int w = s_fb_w;
    int h = s_fb_h;
    /* Snapshot under the LVGL mutex so the BMP encoder reads a stable
       copy. Otherwise the framebuffer changes mid-encode and the
       browser shows torn frames. */
    size_t snap_bytes = (size_t)w * h * 2;
    uint16_t *snap = heap_caps_malloc(snap_bytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snap) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "snap oom");
        return ESP_FAIL;
    }
    int got = webui_snapshot_fb(snap, snap_bytes);
    if (got < 0) {
        free(snap);
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "snap fail");
        return ESP_FAIL;
    }
    /* BMP rows must be padded to 4 bytes. RGB565 = 2 B/px, so 4 B padding
       hits when (w*2)%4 != 0. Use a per-row scratch. */
    int row_bytes = w * 2;
    int pad = (4 - (row_bytes & 3)) & 3;
    int row_padded = row_bytes + pad;
    uint32_t pixel_bytes = (uint32_t)row_padded * h;
    uint32_t header_size = 14 + 56;   /* BITMAPV3INFOHEADER (56) for bitfields */
    uint32_t file_size = header_size + pixel_bytes;
    uint8_t hdr[14 + 56] = {0};
    /* BITMAPFILEHEADER */
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (uint8_t)(file_size      ); hdr[3] = (uint8_t)(file_size >>  8);
    hdr[4] = (uint8_t)(file_size >> 16); hdr[5] = (uint8_t)(file_size >> 24);
    hdr[10] = (uint8_t)header_size;
    /* BITMAPV3INFOHEADER (40) + 3 DWORD masks (12) -- = 56 actually we
       use 40 + 12 + 4 reserved? esp standard is 56 for V3. Browsers
       accept biSize=40 with BI_BITFIELDS (3) and 3 masks following. */
    uint8_t *bi = hdr + 14;
    uint32_t biSize = 56;
    bi[0] = (uint8_t)biSize;
    bi[4]  = (uint8_t)(w      ); bi[5]  = (uint8_t)(w >>  8);
    bi[6]  = (uint8_t)(w >> 16); bi[7]  = (uint8_t)(w >> 24);
    /* negative height -> top-down rows. */
    int32_t neg_h = -h;
    bi[8]  = (uint8_t)(neg_h      ); bi[9]  = (uint8_t)(neg_h >>  8);
    bi[10] = (uint8_t)(neg_h >> 16); bi[11] = (uint8_t)(neg_h >> 24);
    bi[12] = 1;            /* planes */
    bi[14] = 16;           /* bpp */
    bi[16] = 3;            /* BI_BITFIELDS */
    bi[20] = (uint8_t)(pixel_bytes      );
    bi[21] = (uint8_t)(pixel_bytes >>  8);
    bi[22] = (uint8_t)(pixel_bytes >> 16);
    bi[23] = (uint8_t)(pixel_bytes >> 24);
    /* RGB565 masks for the byte order this project uses
       (LV_COLOR_16_SWAP=y means high byte first per pixel; the
       framebuffer stores RGB565 with bytes pre-swapped for the panel.
       We need to UN-swap when emitting to the BMP). */
    /* Standard masks for native RGB565: R=0xF800 G=0x07E0 B=0x001F */
    uint32_t mr = 0xF800, mg = 0x07E0, mb = 0x001F;
    bi[40] = (uint8_t)mr;     bi[41] = (uint8_t)(mr >>  8);
    bi[42] = (uint8_t)(mr>>16); bi[43] = (uint8_t)(mr >> 24);
    bi[44] = (uint8_t)mg;     bi[45] = (uint8_t)(mg >>  8);
    bi[46] = (uint8_t)(mg>>16); bi[47] = (uint8_t)(mg >> 24);
    bi[48] = (uint8_t)mb;     bi[49] = (uint8_t)(mb >>  8);
    bi[50] = (uint8_t)(mb>>16); bi[51] = (uint8_t)(mb >> 24);

    httpd_resp_set_type(r, "image/bmp");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(r, (const char *)hdr, sizeof(hdr)) != ESP_OK) {
        return ESP_FAIL;
    }
    /* Stream rows. We byte-swap each pixel since the panel framebuffer
       is in big-endian RGB565 (LV_COLOR_16_SWAP). */
    uint8_t *row = heap_caps_malloc(row_padded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!row) {
        free(snap);
        httpd_resp_send_chunk(r, NULL, 0);
        return ESP_FAIL;
    }
    memset(row, 0, row_padded);
    const uint16_t *fb = snap;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t v = fb[y * w + x];
            /* Un-swap the bytes back to native 16-bit little endian. */
            uint16_t native = (uint16_t)((v >> 8) | (v << 8));
            row[x * 2 + 0] = (uint8_t)(native & 0xff);
            row[x * 2 + 1] = (uint8_t)(native >> 8);
        }
        if (httpd_resp_send_chunk(r, (const char *)row, row_padded) != ESP_OK) {
            break;
        }
    }
    free(row);
    free(snap);
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

/* ---------- /api/settings ---------- */

/* GET /api/settings — return all device config as JSON.
   Passwords are NOT echoed (empty string) for security. */
static esp_err_t h_settings_get(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char *json = malloc(2048);
    if (!json) return send_str(r, "application/json", "{\"error\":\"oom\"}");
    const AppConfig *c = &g_config;
    int n = snprintf(json, 2048,
        "{\"wifi_ssid\":\"%s\",\"wifi_pass\":\"\","
        "\"nas_type\":\"%s\",\"nas_ip\":\"%s\",\"nas_port\":%u,"
        "\"nas_user\":\"%s\",\"nas_pass\":\"\",\"nas_https\":%d,"
        "\"snmp_comm\":\"%s\",\"snmp_ver\":%u,\"serial_baud\":%lu,"
        "\"poll_sec\":%u,\"rotation_angle\":%u,\"autodim\":%d,"
        "\"timezone\":%d,"
        "\"auto_cycle_enabled\":%d,\"auto_cycle_interval_sec\":%u,"
        "\"sata_disk_count\":%u,\"m2_disk_count\":%u,"
        "\"weather_api_key\":\"%s\",\"weather_city\":\"%s\","
        "\"fan_enabled\":%d,\"fan_mode\":%d,\"fan_manual_pct\":%u,"
        "\"fan_temp_source\":%d,\"fan_hysteresis\":%u,\"fan_min_change_pct\":%u,"
        "\"fan_min_pwm_pct\":%u,\"fan_emergency_temp\":%d,"
        "\"fan_stall_detect_sec\":%u,\"fan_ramp_time_ms\":%u,"
        "\"fan_curve\":["
        "{\"temp\":%d,\"pct\":%u},{\"temp\":%d,\"pct\":%u},"
        "{\"temp\":%d,\"pct\":%u},{\"temp\":%d,\"pct\":%u},"
        "{\"temp\":%d,\"pct\":%u}]}",
        c->ssid,
        c->nas_type, c->nas_ip, (unsigned)c->nas_port,
        c->nas_user, (int)c->nas_https,
        c->snmp_comm, (unsigned)c->snmp_ver, (unsigned long)c->serial_baud,
        (unsigned)c->poll_sec, (unsigned)c->rotation_angle, (int)c->autodim,
        (int)c->timezone,
        (int)c->auto_cycle_enabled, (unsigned)c->auto_cycle_interval_sec,
        (unsigned)c->sata_disk_count, (unsigned)c->m2_disk_count,
        c->weather_api_key, c->weather_city,
        (int)c->fan.enabled, (int)c->fan.mode, (unsigned)c->fan.manual_pwm_pct,
        (int)c->fan.temp_source, (unsigned)c->fan.hysteresis, (unsigned)c->fan.min_change_pct,
        (unsigned)c->fan.min_pwm_pct, (int)c->fan.emergency_temp,
        (unsigned)c->fan.stall_detect_sec, (unsigned)c->fan.ramp_time_ms,
        (int)c->fan.curve[0].temp, (unsigned)c->fan.curve[0].pwm_pct,
        (int)c->fan.curve[1].temp, (unsigned)c->fan.curve[1].pwm_pct,
        (int)c->fan.curve[2].temp, (unsigned)c->fan.curve[2].pwm_pct,
        (int)c->fan.curve[3].temp, (unsigned)c->fan.curve[3].pwm_pct,
        (int)c->fan.curve[4].temp, (unsigned)c->fan.curve[4].pwm_pct);
    if (n <= 0 || n >= 2048) {
        free(json);
        return send_str(r, "application/json", "{\"error\":\"overflow\"}");
    }
    esp_err_t ret = send_str(r, "application/json", json);
    free(json);
    return ret;
}

/* POST /api/settings — update config fields from form-urlencoded body.
   Only fields present in the body are changed; missing fields keep their
   current value. Passwords: empty value means "keep current".
   Returns {"ok":true,"reboot_required":<0|1>}. reboot_required is set when
   fields that need a restart to take effect are modified. */
static esp_err_t h_settings_post(httpd_req_t *r)
{
    CHECK_AUTH(r);
    char *body = malloc(2048);
    if (!body) return send_str(r, "application/json", "{\"error\":\"oom\"}");
    int n = read_body(r, body, 2048);
    if (n <= 0) { free(body); return send_str(r, "text/plain", "empty"); }

    bool reboot_required = false;
    char tmp[80];

    /* WiFi: empty password keeps current value */
    if (form_has(body, "wifi_ssid") || form_has(body, "wifi_pass")) {
        char ssid[33] = {0};
        char pass[65] = {0};
        if (form_str(body, "wifi_ssid", tmp, sizeof(tmp))) {
            strncpy(ssid, tmp, sizeof(ssid) - 1);
        } else {
            strncpy(ssid, g_config.ssid, sizeof(ssid) - 1);
        }
        if (form_str(body, "wifi_pass", tmp, sizeof(tmp)) && tmp[0]) {
            strncpy(pass, tmp, sizeof(pass) - 1);
        } else {
            strncpy(pass, g_config.wifipass, sizeof(pass) - 1);
        }
        config_save_wifi(ssid, pass);
        reboot_required = true;
    }

    /* NAS connection */
    if (form_has(body, "nas_type") || form_has(body, "nas_ip") ||
        form_has(body, "nas_port") || form_has(body, "nas_user") ||
        form_has(body, "nas_pass") || form_has(body, "nas_https")) {
        char type[16] = {0}, ip[40] = {0}, user[32] = {0}, pass[65] = {0};
        if (form_str(body, "nas_type", tmp, sizeof(tmp))) strncpy(type, tmp, sizeof(type)-1);
        else strncpy(type, g_config.nas_type, sizeof(type)-1);
        if (form_str(body, "nas_ip", tmp, sizeof(tmp))) strncpy(ip, tmp, sizeof(ip)-1);
        else strncpy(ip, g_config.nas_ip, sizeof(ip)-1);
        if (form_str(body, "nas_user", tmp, sizeof(tmp))) strncpy(user, tmp, sizeof(user)-1);
        else strncpy(user, g_config.nas_user, sizeof(user)-1);
        if (form_str(body, "nas_pass", tmp, sizeof(tmp)) && tmp[0]) strncpy(pass, tmp, sizeof(pass)-1);
        else strncpy(pass, g_config.nas_pass, sizeof(pass)-1);
        uint16_t port = (uint16_t)form_int(body, "nas_port", g_config.nas_port);
        bool https = form_has(body, "nas_https") ? form_bool(body, "nas_https", false) : g_config.nas_https;
        config_save_nas(type, ip, port, user, pass, https);
        reboot_required = true;
    }

    /* SNMP / serial (no dedicated setter — write g_config + config_save) */
    if (form_has(body, "snmp_comm")) {
        form_str(body, "snmp_comm", g_config.snmp_comm, sizeof(g_config.snmp_comm));
        reboot_required = true;
    }
    if (form_has(body, "snmp_ver")) {
        g_config.snmp_ver = (uint8_t)form_int(body, "snmp_ver", g_config.snmp_ver);
        reboot_required = true;
    }
    if (form_has(body, "serial_baud")) {
        g_config.serial_baud = (uint32_t)form_int(body, "serial_baud", (int)g_config.serial_baud);
        reboot_required = true;
    }

    /* Display: brightness is kept from g_config (it has its own endpoint) */
    if (form_has(body, "poll_sec") || form_has(body, "rotation_angle") || form_has(body, "autodim")) {
        uint8_t rot = (uint8_t)form_int(body, "rotation_angle", g_config.rotation_angle);
        bool autodim = form_has(body, "autodim") ? form_bool(body, "autodim", false) : g_config.autodim;
        config_save_display(rot, g_config.brightness, autodim);
        if (form_has(body, "rotation_angle")) reboot_required = true;
    }

    /* Disk slots */
    if (form_has(body, "sata_disk_count") || form_has(body, "m2_disk_count")) {
        uint8_t sata = (uint8_t)form_int(body, "sata_disk_count", g_config.sata_disk_count);
        uint8_t m2   = (uint8_t)form_int(body, "m2_disk_count", g_config.m2_disk_count);
        config_save_disk_config(sata, m2);
        reboot_required = true;
    }

    /* Timezone / auto-cycle / weather (no dedicated setter) */
    if (form_has(body, "timezone")) g_config.timezone = (int8_t)form_int(body, "timezone", g_config.timezone);
    if (form_has(body, "auto_cycle_enabled"))
        g_config.auto_cycle_enabled = form_bool(body, "auto_cycle_enabled", false);
    if (form_has(body, "auto_cycle_interval_sec"))
        g_config.auto_cycle_interval_sec = (uint8_t)form_int(body, "auto_cycle_interval_sec", g_config.auto_cycle_interval_sec);
    if (form_has(body, "weather_api_key"))
        form_str(body, "weather_api_key", g_config.weather_api_key, sizeof(g_config.weather_api_key));
    if (form_has(body, "weather_city"))
        form_str(body, "weather_city", g_config.weather_city, sizeof(g_config.weather_city));

    /* Fan: assemble FanConfig from individual keys, then save */
    bool fan_changed = form_has(body, "fan_enabled") || form_has(body, "fan_mode") ||
                       form_has(body, "fan_manual_pct") || form_has(body, "fan_temp_source") ||
                       form_has(body, "fan_hysteresis") || form_has(body, "fan_min_change_pct") ||
                       form_has(body, "fan_min_pwm_pct") || form_has(body, "fan_emergency_temp") ||
                       form_has(body, "fan_stall_detect_sec") || form_has(body, "fan_ramp_time_ms");
    for (int i = 0; i < FAN_CURVE_POINTS; i++) {
        char k[24];
        snprintf(k, sizeof(k), "fan_curve_%d_temp", i);
        if (form_has(body, k)) fan_changed = true;
        snprintf(k, sizeof(k), "fan_curve_%d_pct", i);
        if (form_has(body, k)) fan_changed = true;
    }
    if (fan_changed) {
        FanConfig fc = g_config.fan;  /* start from current */
        if (form_has(body, "fan_enabled")) fc.enabled = form_bool(body, "fan_enabled", false);
        if (form_has(body, "fan_mode")) fc.mode = (FanMode)form_int(body, "fan_mode", fc.mode);
        if (form_has(body, "fan_manual_pct")) fc.manual_pwm_pct = (uint8_t)form_int(body, "fan_manual_pct", fc.manual_pwm_pct);
        if (form_has(body, "fan_temp_source")) fc.temp_source = (TempSource)form_int(body, "fan_temp_source", fc.temp_source);
        if (form_has(body, "fan_hysteresis")) fc.hysteresis = (uint8_t)form_int(body, "fan_hysteresis", fc.hysteresis);
        if (form_has(body, "fan_min_change_pct")) fc.min_change_pct = (uint8_t)form_int(body, "fan_min_change_pct", fc.min_change_pct);
        if (form_has(body, "fan_min_pwm_pct")) fc.min_pwm_pct = (uint8_t)form_int(body, "fan_min_pwm_pct", fc.min_pwm_pct);
        if (form_has(body, "fan_emergency_temp")) fc.emergency_temp = (int16_t)form_int(body, "fan_emergency_temp", fc.emergency_temp);
        if (form_has(body, "fan_stall_detect_sec")) fc.stall_detect_sec = (uint8_t)form_int(body, "fan_stall_detect_sec", fc.stall_detect_sec);
        if (form_has(body, "fan_ramp_time_ms")) fc.ramp_time_ms = (uint16_t)form_int(body, "fan_ramp_time_ms", fc.ramp_time_ms);
        for (int i = 0; i < FAN_CURVE_POINTS; i++) {
            char k[24];
            snprintf(k, sizeof(k), "fan_curve_%d_temp", i);
            if (form_has(body, k)) fc.curve[i].temp = (int16_t)form_int(body, k, fc.curve[i].temp);
            snprintf(k, sizeof(k), "fan_curve_%d_pct", i);
            if (form_has(body, k)) fc.curve[i].pwm_pct = (uint8_t)form_int(body, k, fc.curve[i].pwm_pct);
        }
        config_save_fan(&fc);
        fan_control_apply_config(&fc);  /* hot-apply */
    }

    /* Persist fields that lack a dedicated setter */
    config_save();

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"reboot_required\":%d}", (int)reboot_required);
    esp_err_t ret = send_str(r, "application/json", resp);
    free(body);
    return ret;
}

/* ---------- start/stop ---------- */

static esp_err_t webui_register_routes(httpd_handle_t srv)
{
    httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = h_root_redirect },
        { .uri = "/ui",              .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/state",       .method = HTTP_GET,  .handler = h_state },
        { .uri = "/api/list",        .method = HTTP_GET,  .handler = h_list },
        { .uri = "/api/cfg",         .method = HTTP_POST, .handler = h_cfg },
        { .uri = "/api/clock",       .method = HTTP_POST, .handler = h_clock },
        { .uri = "/api/bg",          .method = HTTP_POST, .handler = h_bg },
        { .uri = "/api/bg/upload",   .method = HTTP_POST, .handler = h_bg_upload },
        { .uri = "/api/bg/fetch",    .method = HTTP_POST, .handler = h_bg_fetch },
        { .uri = "/api/quotes",      .method = HTTP_POST, .handler = h_quotes },
        { .uri = "/api/quotes/fetch",.method = HTTP_POST, .handler = h_quotes_fetch },
        { .uri = "/api/goto",        .method = HTTP_POST, .handler = h_goto },
        { .uri = "/api/rec/start",   .method = HTTP_POST, .handler = h_rec_start },
        { .uri = "/api/rec/stop",    .method = HTTP_POST, .handler = h_rec_stop },
        { .uri = "/api/play",        .method = HTTP_POST, .handler = h_play },
        { .uri = "/api/stop",        .method = HTTP_POST, .handler = h_stop },
        { .uri = "/screen.bmp",      .method = HTTP_GET,  .handler = h_screen_bmp },
        { .uri = "/api/settings",    .method = HTTP_GET,  .handler = h_settings_get },
        { .uri = "/api/settings",    .method = HTTP_POST, .handler = h_settings_post },
        { .uri = "/rec/*",           .method = HTTP_GET,  .handler = h_rec_get },
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        httpd_register_uri_handler(srv, &routes[i]);
    }
    return ESP_OK;
}

esp_err_t webui_start_with_httpd(httpd_handle_t srv)
{
    if (!srv) return ESP_ERR_INVALID_ARG;
    if (s_srv) return ESP_OK;
    s_srv = srv;
    esp_err_t e = webui_register_routes(s_srv);
    if (e != ESP_OK) {
        s_srv = NULL;
        return e;
    }
    ESP_LOGI(TAG, "webui routes registered on shared httpd");
    return ESP_OK;
}

esp_err_t webui_start(void)
{
    if (s_srv) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port  = 80;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 28;
    cfg.stack_size   = 8 * 1024;
    cfg.task_priority = 2;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    esp_err_t e = httpd_start(&s_srv, &cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(e)); return e; }

    e = webui_register_routes(s_srv);
    if (e != ESP_OK) { httpd_stop(s_srv); s_srv = NULL; return e; }
    ESP_LOGI(TAG, "webui up on port %d", cfg.server_port);
    return ESP_OK;
}

void webui_stop(void)
{
    if (!s_srv) return;
    httpd_stop(s_srv);
    s_srv = NULL;
}
