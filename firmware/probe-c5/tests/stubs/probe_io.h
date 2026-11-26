#pragma once
#include "probe_core.h"

extern struct tb_pins tb_pin_config[TB_PROTOCOL_COUNT];
enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length);
