#pragma once
#include "probe_core.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tb_status tb_target_ota_info(uint8_t protocol, uint8_t *out, size_t *size);

#ifdef __cplusplus
}
#endif
