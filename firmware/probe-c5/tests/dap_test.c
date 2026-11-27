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

#define SWD_MEMORY_BASE 0x20000000u
#define SWD_MEMORY_WORDS 64u

static struct
{
    bool clock_high, output_enabled, active, read, write_phase;
    uint8_t swdio, request;
    unsigned request_bits, response_bits, write_bits;
    uint8_t ack, override_ack, write_parity;
    unsigned override_count;
    uint32_t response_data, write_data;
    uint32_t csw, tar, rdbuff, ctrl_stat, select;
    uint32_t memory[SWD_MEMORY_WORDS];
} swd;

static bool memory_index(uint32_t address, size_t *index)
{
    if (address < SWD_MEMORY_BASE || address - SWD_MEMORY_BASE >= SWD_MEMORY_WORDS * 4 ||
        ((address - SWD_MEMORY_BASE) & 3))
        return false;
    *index = (address - SWD_MEMORY_BASE) / 4;
    return true;
}

uint32_t tb_dap_model_memory_read(uint32_t address)
{
    size_t index;
    assert(memory_index(address, &index));
    return swd.memory[index];
}

static bool ap_read(uint8_t address, uint32_t *value)
{
    switch (address) {
    case 0x00:
        *value = swd.csw;
        return true;
    case 0x04:
        *value = swd.tar;
        return true;
    case 0x0c: {
        size_t index;
        if (!memory_index(swd.tar, &index))
            return false;
        *value = swd.memory[index];
        if (((swd.csw >> 4) & 3u) == 1u)
            swd.tar += 4;
        return true;
    }
    default:
        return false;
    }
}

static void ap_write(uint8_t address, uint32_t value)
{
    switch (address) {
    case 0x00:
        swd.csw = value;
        break;
    case 0x04:
        swd.tar = value;
        break;
    case 0x0c: {
        size_t index;
        if (memory_index(swd.tar, &index))
            swd.memory[index] = value;
        if (((swd.csw >> 4) & 3u) == 1u)
            swd.tar += 4;
        break;
    }
    default:
        break;
    }
}

static void decode_request(void)
{
    uint8_t request = swd.request;
    uint8_t address = (uint8_t)(((request >> 3) & 3u) << 2);
    bool ap = (request & 2u) != 0;
    bool read = (request & 4u) != 0;
    unsigned parity = 0;
    for (unsigned bit = 1; bit <= 4; bit++)
        parity += (request >> bit) & 1u;

    swd.active = true;
    swd.read = read;
    swd.response_bits = 0;
    swd.write_bits = 0;
    swd.ack =
        (request & 1u) && !(request & 0x40u) && (request & 0x80u) && (((request >> 5) & 1u) == (parity & 1u)) ? 1u : 4u;
    if (swd.ack == 1u && swd.override_count) {
        swd.ack = swd.override_ack;
        swd.override_count--;
    }
    swd.response_data = 0;
    if (swd.ack != 1u)
        return;
    if (ap) {
        if (read) {
            uint32_t value;
            if (!ap_read(address, &value)) {
                swd.ack = 4u;
                return;
            }
            swd.response_data = swd.rdbuff;
            swd.rdbuff = value;
        } else if (address != 0x00 && address != 0x04 && address != 0x0c) {
            swd.ack = 4u;
        } else if (address == 0x0c) {
            size_t index;
            if (!memory_index(swd.tar, &index))
                swd.ack = 4u;
        }
    } else if (read) {
        switch (address) {
        case 0x00:
            swd.response_data = 0x2ba01477u;
            break;
        case 0x04:
            swd.response_data = swd.ctrl_stat;
            break;
        case 0x08:
            swd.response_data = swd.select;
            break;
        case 0x0c:
            swd.response_data = swd.rdbuff;
            break;
        default:
            swd.ack = 4u;
            break;
        }
    } else if (address != 0x00 && address != 0x04 && address != 0x08) {
        swd.ack = 4u;
    }
}

void tb_dap_model_reset(void)
{
    memset(&swd, 0, sizeof swd);
    swd.output_enabled = true;
    swd.csw = 0x23000052u;
}

void tb_dap_model_set_ack(unsigned ack, unsigned count)
{
    assert(ack == 1u || ack == 2u || ack == 4u);
    swd.override_ack = (uint8_t)ack;
    swd.override_count = count;
}

uint32_t tb_gpio_read(unsigned role)
{
    if (role != 1 || swd.output_enabled || !swd.active)
        return 1;
    unsigned bit = swd.response_bits++;
    if (bit < 3)
        return (swd.ack >> bit) & 1u;
    if (swd.ack != 1u || !swd.read)
        return 1;
    if (bit < 35)
        return (swd.response_data >> (bit - 3)) & 1u;
    if (bit == 35) {
        uint32_t parity = swd.response_data;
        parity ^= parity >> 16;
        parity ^= parity >> 8;
        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;
        return parity & 1u;
    }
    return 1;
}

void tb_gpio_write(unsigned role, uint32_t value)
{
    if (role == 1) {
        swd.swdio = (uint8_t)(value & 1u);
        return;
    }
    if (role != 0)
        return;
    bool high = (value & 1u) != 0;
    if (high && !swd.clock_high && swd.output_enabled) {
        if (swd.write_phase) {
            if (swd.write_bits < 32)
                swd.write_data |= (uint32_t)swd.swdio << swd.write_bits;
            else
                swd.write_parity = swd.swdio;
            if (++swd.write_bits == 33) {
                uint32_t data = swd.write_data;
                unsigned parity = data;
                parity ^= parity >> 16;
                parity ^= parity >> 8;
                parity ^= parity >> 4;
                parity ^= parity >> 2;
                parity ^= parity >> 1;
                if ((parity & 1u) == swd.write_parity) {
                    uint8_t address = (uint8_t)(((swd.request >> 3) & 3u) << 2);
                    bool ap = (swd.request & 2u) != 0;
                    if (ap)
                        ap_write(address, data);
                    else if (address == 0x04)
                        swd.ctrl_stat = data;
                    else if (address == 0x08)
                        swd.select = data;
                }
                swd.active = false;
                swd.write_phase = false;
                swd.request_bits = 0;
                swd.request = 0;
            }
        } else if (!swd.active && swd.request_bits < 8) {
            swd.request |= (uint8_t)(swd.swdio << swd.request_bits);
            if (++swd.request_bits == 8)
                decode_request();
        }
    }
    swd.clock_high = high;
}

void tb_swd_direction(bool output)
{
    if (!output) {
        swd.output_enabled = false;
        return;
    }
    swd.output_enabled = true;
    if (swd.active && swd.ack == 1u && !swd.read) {
        swd.write_phase = true;
        swd.write_data = 0;
        swd.write_bits = 0;
        swd.write_parity = 0;
        return;
    }
    swd.active = false;
    swd.request_bits = 0;
    swd.request = 0;
    swd.response_bits = 0;
}

void tb_swd_on(void)
{
    swd.output_enabled = true;
    swd.active = false;
    swd.request_bits = 0;
    swd.request = 0;
}

void tb_swd_off(void)
{
    swd.output_enabled = false;
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

static size_t transfer(uint8_t request, uint32_t *data, uint8_t *response)
{
    uint8_t input[8] = {0x05, 0, 1, request};
    size_t size = 4;
    if (!(request & DAP_TRANSFER_RnW)) {
        assert(data);
        input[4] = (uint8_t)*data;
        input[5] = (uint8_t)(*data >> 8);
        input[6] = (uint8_t)(*data >> 16);
        input[7] = (uint8_t)(*data >> 24);
        size = 8;
    }
    assert(tb_dap_validate(input, size, 4000000));
    return DAP_ExecuteCommand(input, response) & 0xffffu;
}

static void swd_target_model(void)
{
    uint8_t response[TB_DAP_PACKET];
    uint32_t data = 0;
    tb_dap_model_reset();

    size_t transfer_size = transfer(DAP_TRANSFER_RnW, NULL, response);
    if (transfer_size != 7)
        fprintf(stderr, "SWD IDCODE transfer len=%zu count=%u status=%u ack=%u bits=%u\n", transfer_size, response[1],
                response[2], swd.ack, swd.request_bits);
    assert(transfer_size == 7);
    assert(response[0] == 0x05 && response[1] == 1 && response[2] == DAP_TRANSFER_OK);
    uint32_t idcode =
        (uint32_t)response[3] | (uint32_t)response[4] << 8 | (uint32_t)response[5] << 16 | (uint32_t)response[6] << 24;
    assert(idcode == 0x2ba01477u);

    const uint8_t configure[] = {0x04, 0, 2, 0, 0, 0};
    assert(tb_dap_validate(configure, sizeof configure, 4000000));
    DAP_ExecuteCommand(configure, response);
    tb_dap_model_set_ack(DAP_TRANSFER_WAIT, 2);
    assert(transfer(DAP_TRANSFER_RnW, NULL, response) == 7);
    assert(response[0] == 0x05 && response[1] == 1 && response[2] == DAP_TRANSFER_OK);

    tb_dap_model_set_ack(DAP_TRANSFER_WAIT, 3);
    assert(transfer(DAP_TRANSFER_RnW, NULL, response) == 3);
    assert(response[0] == 0x05 && response[1] == 0 && response[2] == DAP_TRANSFER_WAIT);

    tb_dap_model_set_ack(DAP_TRANSFER_FAULT, 1);
    assert(transfer(DAP_TRANSFER_RnW, NULL, response) == 3);
    assert(response[0] == 0x05 && response[1] == 0 && response[2] == DAP_TRANSFER_FAULT);

    expiry_calls = 0;
    expiry_after = 1;
    assert(transfer(DAP_TRANSFER_RnW, NULL, response) == 3);
    assert(expiry_calls == 1);
    assert(response[0] == 0x05 && response[1] == 0 && response[2] == DAP_TRANSFER_ERROR);
    expiry_calls = 0;
    expiry_after = 0;

    data = 0x23000052u;
    assert(transfer(DAP_TRANSFER_APnDP | 0x01u, &data, response) == 3);
    assert(response[0] == 0x05 && response[1] == 1 && response[2] == DAP_TRANSFER_OK);
    data = SWD_MEMORY_BASE;
    assert(transfer(DAP_TRANSFER_APnDP | 0x05u, &data, response) == 3);
    assert(response[0] == 0x05 && response[1] == 1 && response[2] == DAP_TRANSFER_OK);
    data = 0x11223344u;
    assert(transfer(DAP_TRANSFER_APnDP | 0x0du, &data, response) == 3);
    assert(response[0] == 0x05 && response[1] == 1 && response[2] == DAP_TRANSFER_OK);
    assert(tb_dap_model_memory_read(SWD_MEMORY_BASE) == data);

    uint8_t write_block[] = {0x06, 0, 2, 0, DAP_TRANSFER_APnDP | 0x0du, 0x88, 0x77, 0x66, 0x55, 0x00, 0xff, 0xee, 0xdd};
    assert(tb_dap_validate(write_block, sizeof write_block, 4000000));
    assert((DAP_ExecuteCommand(write_block, response) & 0xffffu) == 4);
    assert(response[0] == 0x06 && response[1] == 2 && response[2] == 0 && response[3] == DAP_TRANSFER_OK);
    assert(tb_dap_model_memory_read(SWD_MEMORY_BASE + 4) == 0x55667788u);
    assert(tb_dap_model_memory_read(SWD_MEMORY_BASE + 8) == 0xddeeff00u);

    data = SWD_MEMORY_BASE + 4;
    assert(transfer(DAP_TRANSFER_APnDP | 0x05u, &data, response) == 3);
    uint8_t read_block[] = {0x06, 0, 2, 0, DAP_TRANSFER_APnDP | DAP_TRANSFER_RnW | 0x0du};
    assert(tb_dap_validate(read_block, sizeof read_block, 4000000));
    assert((DAP_ExecuteCommand(read_block, response) & 0xffffu) == 12);
    assert(response[0] == 0x06 && response[1] == 2 && response[2] == 0 && response[3] == DAP_TRANSFER_OK);
    assert(response[4] == 0x88 && response[5] == 0x77 && response[6] == 0x66 && response[7] == 0x55);
    assert(response[8] == 0x00 && response[9] == 0xff && response[10] == 0xee && response[11] == 0xdd);

    data = SWD_MEMORY_BASE + SWD_MEMORY_WORDS * 4;
    assert(transfer(DAP_TRANSFER_APnDP | 0x05u, &data, response) == 3);
    assert(transfer(DAP_TRANSFER_APnDP | DAP_TRANSFER_RnW | 0x0du, NULL, response) == 3);
    assert(response[0] == 0x05 && response[1] == 0 && response[2] == DAP_TRANSFER_FAULT);
}

int main(void)
{
    DAP_Setup();
    tb_dap_model_reset();
    const uint8_t connect[] = {2, 1};
    execute(connect, sizeof connect);
    swd_target_model();
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
