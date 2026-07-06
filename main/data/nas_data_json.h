#ifndef NAS_DATA_JSON_H
#define NAS_DATA_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"
#include "nas_data.h"

cJSON *nas_data_to_json(const NasData *data);
bool nas_json_to_data(cJSON *json, NasData *data);
void nas_data_free_json(cJSON *json);

#ifdef __cplusplus
}
#endif

#endif
