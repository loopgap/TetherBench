#include "DAP_config.h"
#include "DAP.h"
#include "probe_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned expiry_calls, expiry_after;
bool tb_dap_expired(void)
{
    return expiry_after && ++expiry_calls >= expiry_after;
}

static void execute(const uint8_t *input, size_t size)
{
    uint8_t guarded[TB_DAP_PACKET + 32];
    memset(guarded, 0xa5, sizeof guarded);
    assert(tb_dap_validate(input, size, 4000000));
    uint32_t result = DAP_ExecuteCommand(input, guarded);
    assert((result & 0xffff) <= TB_DAP_PACKET);
    for (unsigned i = TB_DAP_PACKET; i < sizeof guarded; i++)
        assert(guarded[i] == 0xa5);
}
static void sequence_deadline(void)
{
    uint8_t response[TB_DAP_PACKET];
    const uint8_t read_sequence[] = {0x1d, 1, 0x80, 64};
    const uint8_t write_sequence[] = {0x1d, 1, 64, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    expiry_calls = 0;
    expiry_after = 3;
    DAP_ExecuteCommand(read_sequence, response);
    assert(expiry_calls == 3);
    expiry_calls = 0;
    expiry_after = 3;
    DAP_ExecuteCommand(write_sequence, response);
    assert(expiry_calls == 3);
    expiry_calls = 0;
    expiry_after = 0;
}
int main(void)
{
    DAP_Setup();
    const uint8_t connect[] = {2, 1};
    execute(connect, sizeof connect);
    const uint8_t batch[] = {0x7f, 5, 0, 0xf0, 0, 0xff, 4, 0, 2, 0, 2, 0, 0x13, 0, 5, 0, 1, 2};
    execute(batch, sizeof batch);
    const uint8_t read_block[] = {6, 0, 128, 0, 2};
    execute(read_block, sizeof read_block);
    const uint8_t max_block_read[] = {6, 0, 255, 0, 2};
    execute(max_block_read, sizeof max_block_read);
    uint8_t max_block_write[5 + 4 * 254] = {6, 0, 254, 0, 0};
    execute(max_block_write, sizeof max_block_write);
    uint8_t max_transfer_read[3 + 255] = {5, 0, 255};
    memset(max_transfer_read + 3, 2, 255);
    execute(max_transfer_read, sizeof max_transfer_read);
    uint8_t max_transfer_write[3 + 5 * 204] = {5, 0, 204};
    execute(max_transfer_write, sizeof max_transfer_write);
    const uint8_t swd_sequence[] = {0x1d, 2, 0x80, 8, 0xa5};
    execute(swd_sequence, sizeof swd_sequence);
    sequence_deadline();
    uint8_t response[1024];
    const uint8_t info[] = {0, 0xff};
    assert((DAP_ExecuteCommand(info, response) & 0xffff) == 4);
    assert(tb_u16(response + 2) == TB_DAP_PACKET);
    const uint8_t capabilities[] = {0, 0xf0};
    DAP_ExecuteCommand(capabilities, response);
    assert((response[2] & 1) == 1 && (response[2] & 2) == 0);
    uint32_t random = 749;
    uint8_t input[1024];
    unsigned accepted = 0;
    const uint8_t *seeds[] = {batch, read_block, swd_sequence, connect};
    const size_t sizes[] = {sizeof batch, sizeof read_block, sizeof swd_sequence, sizeof connect};
    for (unsigned trial = 0; trial < 10000; trial++) {
        size_t size = sizes[trial % 4];
        memcpy(input, seeds[trial % 4], size);
        random = random * 1664525u + 1013904223u;
        input[(random >> 16) % size] ^= (uint8_t)(random >> 24);
        if (tb_dap_validate(input, size, 4000000)) {
            execute(input, size);
            accepted++;
        }
    }
    for (unsigned trial = 0; trial < 40000; trial++) {
        random = random * 1664525u + 1013904223u;
        size_t size = (random >> 16) % sizeof input;
        for (size_t k = 0; k < size; k++) {
            random = random * 1664525u + 1013904223u;
            input[k] = (uint8_t)(random >> 24);
        }
        if (tb_dap_validate(input, size, 4000000)) {
            execute(input, size);
            accepted++;
        }
    }
    printf("DAP: pinned Arm parser, bounded responses and %u accepted fuzz inputs passed\n", accepted);
}
