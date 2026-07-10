#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* API usada por el desktop Arielo MiniPC OS: GT911 800x480 fisico. */
int minipc_touch_ok(void);
bool minipc_touch_read(int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
