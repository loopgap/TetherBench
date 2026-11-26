#include "probe_dap.h"
#include "probe_io.h"
#include "DAP.h"
#include "esp_timer.h"

static uint64_t deadline;

uint32_t tb_timestamp(void)
{
    return (uint32_t)esp_timer_get_time();
}
bool tb_dap_expired(void)
{
    return (uint64_t)esp_timer_get_time() >= deadline;
}

void tb_dap_init(void)
{
    DAP_Setup();
}
void tb_dap_reset(void)
{
    DAP_Setup();
}
void tb_dap_configure(uint32_t hz)
{
    DAP_Setup();
    if (hz) {
        uint8_t request[5] = {0x11}, response[2];
        tb_put32(request + 1, hz);
        DAP_ProcessCommand(request, response);
    }
}
enum tb_status tb_dap_execute(const uint8_t *input, size_t size, uint8_t *output, size_t *length)
{
    uint32_t hz = tb_pin_config[TB_SWD].hz;
    if (!tb_dap_validate(input, size, hz ? hz : 4000000))
        return TB_INVALID;
    if (input[0] != 0 && input[0] != 1 && input[0] != 3 && !tb_pin_config[TB_SWD].count)
        return TB_STATE;
    deadline = (uint64_t)esp_timer_get_time() + 250000;
    DAP_TransferAbort = 0;
    uint32_t result = DAP_ExecuteCommand(input, output);
    *length = result & 0xffffu;
    if (tb_dap_expired()) {
        tb_swd_off();
        tb_dap_reset();
        *length = 0;
        return TB_TIMEOUT;
    }
    return *length <= TB_DAP_PACKET ? TB_OK : TB_IO;
}
