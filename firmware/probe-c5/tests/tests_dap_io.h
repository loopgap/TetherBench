#pragma once
#include "probe_core.h"
#include <stdbool.h>
#include <stdint.h>
static inline void tb_delay_cycles(uint32_t count)
{
    (void)count;
}
static inline uint32_t tb_timestamp(void)
{
    static uint32_t tick;
    return ++tick;
}
uint32_t tb_gpio_read(unsigned role);
void tb_gpio_write(unsigned role, uint32_t value);
void tb_swd_direction(bool value);
void tb_swd_on(void);
void tb_swd_off(void);
void tb_dap_model_reset(void);
void tb_dap_model_set_ack(unsigned ack, unsigned count);
uint32_t tb_dap_model_memory_read(uint32_t address);
#ifdef TB_HOST_EXPIRE_TEST
bool tb_dap_expired(void);
#else
static inline bool tb_dap_expired(void)
{
    return false;
}
#endif
