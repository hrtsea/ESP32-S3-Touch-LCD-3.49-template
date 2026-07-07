#pragma once

#include "lvgl.h"
#include "i18n_strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the localized UTF-8 string for `key` in the active language.
   Falls back to English on out-of-range key/lang. Named tr() rather than
   t() because LVGL timer callbacks idiomatically use a `lv_timer_t *t`
   parameter, which would otherwise shadow the function. */
const char *tr(i18n_key_t key);

/* Active language index (0=en, 1=zh, 2=ja, 3=ko). Reads from g_cfg.lang;
   if the key isn't in cfg yet, returns 0. */
int   i18n_lang(void);

/* Set the active language and persist via app_cfg_save(). UI labels are not
   re-rendered live; the caller is expected to rebuild the affected tile
   (e.g. settings menu) on next entry. */
void  i18n_set_lang(int lang_idx);

/* Font that includes both the basic Latin set (via fallback) and the CJK
   subset used by the translation table. Use this everywhere user-visible
   text is set, so a Chinese label still renders Latin punctuation. */
extern const lv_font_t font_cjk_14;
const lv_font_t *i18n_font(void);

void tz_apply_current(void);
const char *tz_current_city_name(void);

#ifdef __cplusplus
}
#endif
