#pragma once
#include "esp_err.h"
#include "probe_link.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tb_service_start(void);
esp_err_t tb_service_start_with_link(enum tb_probe_link_profile profile);

#ifdef __cplusplus
}
#endif
