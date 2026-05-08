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

#ifdef __cplusplus
}
#endif
