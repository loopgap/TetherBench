#pragma once
#include "probe_core.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t tb_timestamp(void);
bool tb_dap_expired(void);
void tb_dap_init(void);
void tb_dap_reset(void);
void tb_dap_configure(uint32_t hz);
enum tb_status tb_dap_execute(const uint8_t *input, size_t size, uint8_t *output, size_t *length);

#ifdef __cplusplus
}
#endif
