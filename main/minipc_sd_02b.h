#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t minipc_sd_init(void);
bool minipc_sd_is_mounted(void);

#ifdef __cplusplus
}
#endif
