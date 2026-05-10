#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise recorder backend (ES8311 ADC -> I2S RX -> Opus encoder).
   Call after the radio engine is up; we share the I2S/codec it owns. */
esp_err_t recorder_init(void);

/* Begin recording. Spawns a worker task that reads ADC, encodes to Opus,
   and appends to a new file under /sdcard/recordings/. The path is
   chosen automatically based on RTC time. Returns the chosen path
   (caller need not free). */
esp_err_t   recorder_start(const char **out_path);

/* Stop the worker, flush + close the file. Safe to call when not active. */
esp_err_t   recorder_stop(void);

bool        recorder_is_recording(void);

/* Elapsed seconds since recorder_start; 0 if not active. */
unsigned    recorder_elapsed_s(void);

/* Enumerate up to max_n files under /sdcard/recordings into buf
   (each entry: filename without directory, NUL terminated). Returns
   the count actually written. */
int         recorder_list(char buf[][64], int max_n);

/* Build an absolute path /sdcard/recordings/<name> in `out`. */
void        recorder_full_path(char *out, size_t cap, const char *name);

/* Delete a file under /sdcard/recordings/<name>. */
esp_err_t   recorder_delete(const char *name);

/* Peak absolute sample seen since the last call (0..32767). Reading also
   resets the internal peak so successive calls show fresh levels. Use
   this to drive a VU meter from the UI. Returns 0 when not recording. */
#include <stdint.h>
uint16_t    recorder_peak_level(void);

#ifdef __cplusplus
}
#endif
