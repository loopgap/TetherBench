#pragma once
#include "probe_core.h"

enum tb_status tb_io_get_config(unsigned protocol, struct tb_pins *pins);
enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length);
