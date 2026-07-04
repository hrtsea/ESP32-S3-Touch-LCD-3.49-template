/* i18n.c -- runtime glue for the generated translation tables. The strings
   live in i18n_strings.h; the CJK glyph subset font lives in font_cjk_14.c.
   Both are produced by scripts/gen_i18n.py from scripts/translations.py. */

#include "i18n.h"
#include "esp_log.h"

extern int  app_cfg_get_lang(void);
extern void app_cfg_set_lang(int lang);

const char *tr(i18n_key_t key)
{
    int lang = i18n_lang();
    if (lang < 0 || lang >= I18N_LANG_COUNT) lang = 0;
    if ((unsigned)key >= (unsigned)I18N_KEY_COUNT) return "";
    const char *s = k_i18n_strings[lang][key];
    if (!s || !*s) s = k_i18n_strings[0][key];
    return s ? s : "";
}

int i18n_lang(void) { return app_cfg_get_lang(); }
void i18n_set_lang(int idx) { app_cfg_set_lang(idx); }

/* The generated font_cjk_14 lives in flash as const, so we can't write a
   fallback into it (writes to a flash-mapped region trigger
   "Cache disabled but cached memory region accessed" panics).
   Instead build a writable copy in RAM that adds the fallback chain. */
static lv_font_t s_i18n_font;
static bool      s_i18n_font_ready = false;

const lv_font_t *i18n_font(void)
{
    /* English: keep crisp Latin-only Montserrat 14 -- zero CJK lookup cost
       and the original look. Only zh/ja/ko pay the 32k-glyph table walk. */
    if (i18n_lang() == 0) {
        return &lv_font_montserrat_14;
    }
    if (!s_i18n_font_ready) {
        s_i18n_font = font_cjk_14;
        s_i18n_font.fallback = &lv_font_montserrat_14;
        s_i18n_font_ready = true;
    }
    return &s_i18n_font;
}
