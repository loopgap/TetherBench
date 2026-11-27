#pragma once
#include "probe_core.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t tb_allowed_pins(void);
enum tb_status tb_io_get_config(unsigned protocol, struct tb_pins *pins);
void tb_io_release(void);
enum tb_status tb_io_configure(const struct tb_pins *pins);
enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length);

#ifdef __cplusplus
}
#endif
