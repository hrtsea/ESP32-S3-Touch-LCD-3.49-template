#include "tz_utils.h"
#include "tz_cities.h"
#include "app_cfg.h"
#include <stdlib.h>
#include <time.h>

void tz_apply_current(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    setenv("TZ", k_tz_cities[i].posix_tz, 1);
    tzset();
}

const char *tz_current_city_name(void)
{
    uint16_t i = g_cfg.tz_idx;
    if (i >= TZ_CITY_COUNT) i = TZ_DEFAULT_CITY_INDEX;
    return k_tz_cities[i].name;
}
