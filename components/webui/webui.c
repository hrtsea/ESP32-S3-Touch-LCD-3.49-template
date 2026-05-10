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

#include "webui.h"
#include "recorder.h"
#include "radio.h"
#include "sdcard_bsp.h"

/* These live in main/ (cli.h). Declare locally so this component
   doesn't need to include from main/. */
extern void app_cfg_set_brightness(int v);
extern void app_cfg_set_dim_off(int dim_s, int off_s);

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
"<section><h2>Recorder</h2>\n"
" <button id=brec>Start REC</button>\n"
" <button id=bstop class=warn>Stop</button>\n"
" <button id=bpstop>Stop playback</button>\n"
"</section>\n"
"<section><h2>Recordings</h2>\n"
" <table id=tbl><tr><th>name</th><th>size</th><th>dur</th><th></th></tr></table>\n"
"</section>\n"
"<script>\n"
"let rr=1.0;\n"
"function f(p,o){return fetch(p,o).then(r=>r.json?r.json().catch(()=>{}):r)}\n"
"function pollState(){fetch('/api/state').then(r=>r.json()).then(s=>{\n"
" document.getElementById('br').value=s.brightness;document.getElementById('brl').textContent=s.brightness;\n"
" document.getElementById('vol').value=s.volume;document.getElementById('vl').textContent=s.volume;\n"
" document.getElementById('ds').value=s.dim_s;document.getElementById('dl').textContent=s.dim_s;\n"
" document.getElementById('os').value=s.off_s;document.getElementById('ol').textContent=s.off_s;\n"
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
"</script></body></html>\n";

static esp_err_t h_index(httpd_req_t *r)
{
    return send_str(r, "text/html; charset=utf-8", k_index_html);
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

    char json[512];
    int n = snprintf(json, sizeof(json),
        "{\"recording\":%d,\"elapsed\":%u,"
        "\"playing\":%d,\"uri\":\"%s\","
        "\"peak_l\":%u,\"peak_r\":%u,"
        "\"brightness\":%d,\"volume\":%d,\"dim_s\":%d,\"off_s\":%d}",
        (int)recording, recorder_elapsed_s(),
        (int)playing,   radio_current_uri() ? radio_current_uri() : "",
        (unsigned)pl, (unsigned)pr,
        0, radio_get_volume(), 0, 0);
    /* TODO surface brightness/dim from cfg via a getter; for now return
       0 placeholders -- the page sets them via /api/cfg and re-queries. */
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

static esp_err_t h_cfg(httpd_req_t *r)
{
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

/* ---------- /api/rec start/stop and /api/play|stop ---------- */

static esp_err_t h_rec_start(httpd_req_t *r)
{
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
    recorder_stop();
    return send_str(r, "application/json", "{\"ok\":true}");
}

static esp_err_t h_play(httpd_req_t *r)
{
    char body[300];
    int n = read_body(r, body, sizeof(body));
    if (n <= 0) return send_str(r, "text/plain", "missing uri");
    radio_play(body);
    return send_str(r, "application/json", "{\"ok\":true}");
}

static esp_err_t h_stop(httpd_req_t *r)
{
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
        httpd_resp_send_chunk(r, NULL, 0);
        return ESP_FAIL;
    }
    memset(row, 0, row_padded);
    const uint16_t *fb = (const uint16_t *)s_fb;
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
    httpd_resp_send_chunk(r, NULL, 0);
    return ESP_OK;
}

/* ---------- start/stop ---------- */

esp_err_t webui_start(void)
{
    if (s_srv) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port  = 80;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 16;
    /* Internal RAM is fragmented after LVGL is up; we keep the stack
       on the small side so task create succeeds, and our handlers
       allocate transient buffers (BMP row, file streaming) from PSRAM. */
    cfg.stack_size   = 4 * 1024;
    cfg.task_priority = 4;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;

    esp_err_t e = httpd_start(&s_srv, &cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(e)); return e; }

    httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = h_index },
        { .uri = "/api/state",       .method = HTTP_GET,  .handler = h_state },
        { .uri = "/api/list",        .method = HTTP_GET,  .handler = h_list },
        { .uri = "/api/cfg",         .method = HTTP_POST, .handler = h_cfg },
        { .uri = "/api/rec/start",   .method = HTTP_POST, .handler = h_rec_start },
        { .uri = "/api/rec/stop",    .method = HTTP_POST, .handler = h_rec_stop },
        { .uri = "/api/play",        .method = HTTP_POST, .handler = h_play },
        { .uri = "/api/stop",        .method = HTTP_POST, .handler = h_stop },
        { .uri = "/screen.bmp",      .method = HTTP_GET,  .handler = h_screen_bmp },
        { .uri = "/rec/*",           .method = HTTP_GET,  .handler = h_rec_get },
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_srv, &routes[i]);
    }
    ESP_LOGI(TAG, "webui up on port %d", cfg.server_port);
    return ESP_OK;
}

void webui_stop(void)
{
    if (!s_srv) return;
    httpd_stop(s_srv);
    s_srv = NULL;
}
