#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the radio engine (ES8311 + I2S + esp_audio_simple_player).
   Must be called *after* audio_min_shutdown() so the I2S/codec is free. */
esp_err_t radio_init(void);

/* Play a short ~300 ms beep through the DAC so the user can hear that
   speaker output works independent of any decoder/file path. */
void      radio_beep(void);

/* Start playing a URI. The player auto-detects MP3/AAC/FLAC/etc.
   For HTTPS streams the IDF default cert bundle must be enabled. */
esp_err_t radio_play(const char *uri);

/* Stop playback. Safe to call when already stopped. */
esp_err_t radio_stop(void);

/* True iff the player is currently in the RUNNING state. */
bool      radio_is_playing(void);

/* Curated station list. Indices are stable; see components/radio/stations.h. */
int          radio_station_count(void);
const char  *radio_station_name(int idx);
const char  *radio_station_genre(int idx);
const char  *radio_station_url(int idx);

/* Start a station from the curated list. Stops any current stream first. */
esp_err_t    radio_play_index(int idx);

/* Index of the most recently requested station, -1 if none yet. */
int          radio_current_index(void);

/* Last URI handed to radio_play (NULL if never). Stays set across
   radio_stop so callers can resume by passing it back to radio_play. */
const char  *radio_current_uri(void);

/* Output-side VU: peak abs sample per channel seen in the most
   recently decoded PCM chunk. Reading also resets, so successive
   calls return per-poll-interval levels. Use to drive a playback VU
   meter in the UI. Returns 0/0 when nothing is decoding. */
#include <stdint.h>
void         radio_out_peak(uint16_t *out_l, uint16_t *out_r);

/* Output volume 0..100. Applied through the ES8311 codec, so it's a real
   analog gain change (not just a digital scale). */
void         radio_set_volume(int vol_0_100);
int          radio_get_volume(void);

/* Current sample rate of the shared TDM clock. Both record and playback
   devices run at this rate to avoid "sample_rate conflict" errors. */
uint32_t     radio_get_sample_rate(void);

/* For the recorder: hand it the RX i2s channel + codec interfaces we
   already created. ES8311 is one codec, one I2S port; allocating a
   second instance from the recorder fights for the same I2C device
   and the same I2S controller. Returns NULL/false until radio_init
   has run. The handles are owned by radio.c. */
struct i2s_chan_obj_t;
typedef struct i2s_chan_obj_t *i2s_chan_handle_t_fwd;  /* opaque */
void  *radio_get_i2s_rx_handle(void);
void  *radio_get_codec_ctrl_if(void);
void  *radio_get_codec_data_if_in(void);  /* IN-direction data_if */
void  *radio_get_codec_if(void);          /* shared ES8311 codec_if */
void  *radio_get_record_dev(void);        /* preconfigured ES7210 esp_codec_dev_handle_t */
void  *radio_get_play_dev(void);          /* ES8311 play dev */

#ifdef __cplusplus
}
#endif
