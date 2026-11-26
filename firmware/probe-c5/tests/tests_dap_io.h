#pragma once
#include "probe_core.h"
#include <stdbool.h>
#include <stdint.h>
static inline void tb_delay_cycles(uint32_t count) { (void)count; }
static inline uint32_t tb_timestamp(void) { static uint32_t tick; return ++tick; }
static inline uint32_t tb_gpio_read(unsigned role) { (void)role; return 1; }
static inline void tb_gpio_write(unsigned role, uint32_t value) { (void)role; (void)value; }
static inline void tb_swd_direction(bool value) { (void)value; }
static inline void tb_swd_on(void) {}
static inline void tb_swd_off(void) {}
#ifdef TB_HOST_EXPIRE_TEST
bool tb_dap_expired(void);
#else
static inline bool tb_dap_expired(void) { return false; }
#endif
