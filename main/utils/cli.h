#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the USB-Serial-JTAG console with a small set of debug commands.
   See cli.c for the registered commands. Call once after Wi-Fi/UI are up. */
void cli_start(void);

/* Persist an SSID + password to NVS and start an association. Implemented
   in main.cpp; declared here so cli.c can call it without touching the
   private statics in main. */
void app_cfg_wifi_connect_save(const char *ssid, const char *pass);

/* Public language getter/setter (also used by i18n.c). Lang index is
   0=en, 1=zh, 2=ja, 3=ko -- see scripts/translations.py. Setter persists
   via app_cfg_save() but does not re-render existing labels. */
int  app_cfg_get_lang(void);
void app_cfg_set_lang(int lang);

/* Set backlight 0..255 (255 = full). Also kicks the activity timer so
   any pending dim/off transitions reset. Persists to cfg. */
void app_cfg_set_brightness(int v);

/* Set dim_s / off_s in seconds (0 = never). Persists. */
void app_cfg_set_dim_off(int dim_s, int off_s);

#ifdef __cplusplus
}
#endif
