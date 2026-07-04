#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_min_init(void);
void      audio_min_play_midi(bool play);
bool      audio_min_is_playing(void);
/* Software gain, 0..100. Applied per-sample before codec write. */
void      audio_min_set_volume(uint8_t vol_0_100);

/* Returns true when audio_min can actually start playback -- i.e. the
   radio engine owns the play_dev and is not currently streaming.  When
   false, pressing the play button should be a no-op. */
bool      audio_min_is_available(void);

/* Stop the MIDI task.  With the new radio-owned architecture this just
   kills the worker thread; the codec / I2S stay owned by radio.c. */
void      audio_min_shutdown(void);

#ifdef __cplusplus
}
#endif
