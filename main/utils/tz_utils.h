#ifndef TZ_UTILS_H
#define TZ_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

void tz_apply_current(void);
const char *tz_current_city_name(void);

#ifdef __cplusplus
}
#endif

#endif /* TZ_UTILS_H */
