#pragma once
#include "probe_core.h"
#include "soc/gpio_struct.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const struct tb_pins *const tb_io_swd_config;
void tb_swd_on(void);
void tb_swd_off(void);

#ifdef __cplusplus
}
#endif

static inline void tb_gpio_write(unsigned role, uint32_t value)
{
    int pin = tb_io_swd_config->pin[role];
    if (pin < 0)
        return;
    if (value & 1u)
        GPIO.out_w1ts.val = 1u << pin;
    else
        GPIO.out_w1tc.val = 1u << pin;
}
static inline uint32_t tb_gpio_read(unsigned role)
{
    int pin = tb_io_swd_config->pin[role];
    return pin < 0 ? 1 : (GPIO.in.in_data_next >> pin) & 1u;
}
static inline void tb_swd_direction(bool enabled)
{
    int pin = tb_io_swd_config->pin[1];
    if (pin < 0)
        return;
    if (enabled)
        GPIO.enable_w1ts.enable_w1ts = 1u << pin;
    else
        GPIO.enable_w1tc.enable_w1tc = 1u << pin;
}
static inline uint32_t tb_cycle_count(void)
{
    uint32_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value));
    return value;
}
static inline void tb_delay_cycles(uint32_t count)
{
    uint32_t start = tb_cycle_count();
    while ((uint32_t)(tb_cycle_count() - start) < count)
        __asm__ volatile("nop");
}
