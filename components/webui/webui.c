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

/* ---------- /api/clock ---------- */

/* form keys: x, y (pixel offset from screen center), size (0..3),
   rgba (uint32), show_ms (0/1). Each is optional; missing keys leave
   the corresponding cfg field unchanged. */
static esp_err_t h_clock(httpd_req_t *r)
{
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

/* ---------- start/stop ---------- */

static esp_err_t webui_register_routes(httpd_handle_t srv)
{
    httpd_uri_t routes[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = h_index },
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
    cfg.max_uri_handlers = 24;
    cfg.stack_size   = 4 * 1024;
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
