#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "probe_target_ota.h"
#include "probe_io.h"

struct tb_pins tb_pin_config[TB_PROTOCOL_COUNT];
static uint8_t mock_mode, mock_status, mock_response[TB_TARGET_UART_MAX];
static size_t mock_response_size, mock_response_offset;
static uint8_t hello[TB_TARGET_HELLO_SIZE], pending_spi[TB_TARGET_SPI_SIZE];
static uint32_t request_sequence, random_value = 0x12345678;
static unsigned busy_polls;
static int64_t mock_time;

uint32_t esp_random(void)
{
    return random_value++;
}
int64_t esp_timer_get_time(void)
{
    return mock_time;
}

static void fill_hello(void)
{
    memset(hello, 0, sizeof hello);
    tb_put16(hello, 1);
    tb_put32(hello + 4, TB_TARGET_CAPABILITY_MASK);
    tb_put32(hello + 8, 128 * 1024);
    tb_put16(hello + 12, 983);
    memcpy(hello + 16, "test-mcu", 9);
    memcpy(hello + 48, "1.2", 4);
    memcpy(hello + 80, "boot-1", 7);
    memset(hello + 96, 0x5a, 12);
    tb_put64(hello + 108, 4);
}

static size_t make_response(const struct tb_target_frame *request, uint8_t status, uint8_t *wire, size_t capacity,
                            bool spi)
{
    bool include_hello = status == TB_TARGET_OK || mock_mode == 4;
    const uint8_t *data = include_hello ? hello : NULL;
    size_t size = include_hello ? sizeof hello : 0;
    struct tb_target_frame response = {
        .op = request->op, .status = status, .sequence = request->sequence, .data = data, .size = size};
    if (spi)
        return tb_target_spi_encode(&response, wire);
    return tb_target_uart_encode(&response, wire, capacity);
}

enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length)
{
    assert(input && length && size >= 12);
    uint8_t protocol = input[0];
    size_t tx_size = tb_u16(input + 4), rx_size = tb_u16(input + 6);
    uint32_t timeout = tb_u16(input + 8);
    assert(!input[1] && !tb_u16(input + 2) && !tb_u16(input + 10));
    assert(size == 12 + tx_size && timeout && timeout <= 100);
    *length = 0;

    if (protocol == TB_UART) {
        if (tx_size) {
            struct tb_target_frame request = {0};
            uint8_t payload[TB_TARGET_PAYLOAD_MAX];
            assert(tb_target_uart_decode(input + 12, tx_size, &request, payload) == TB_OK);
            assert(request.op == TB_TARGET_HELLO && !request.size);
            if (mock_mode == 5) {
                mock_time += 100000;
                return TB_OK;
            }
            if (mock_mode == 1)
                request.sequence++;
            request_sequence = request.sequence;
            if (mock_mode == 3)
                tb_put32(hello + 4, TB_TARGET_CAPABILITY_MASK | 0x100);
            mock_response_size = make_response(&request, mock_status, mock_response, sizeof mock_response, false);
            mock_response_offset = 0;
            return TB_OK;
        }
        if (mock_mode == 2) {
            mock_time += (int64_t)timeout * 1000;
            return TB_OK;
        }
        assert(output && rx_size);
        size_t available = mock_response_size - mock_response_offset;
        size_t count = available < 5 ? available : 5;
        if (count > rx_size)
            count = rx_size;
        memcpy(output, mock_response + mock_response_offset, count);
        mock_response_offset += count;
        *length = count;
        mock_time += 500;
        return TB_OK;
    }

    assert(protocol == TB_SPI && tx_size == TB_TARGET_SPI_SIZE && rx_size == TB_TARGET_SPI_SIZE && output);
    struct tb_target_frame request = {0};
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    assert(tb_target_spi_decode(input + 12, tx_size, &request, payload) == TB_OK);
    uint8_t response[TB_TARGET_SPI_SIZE] = {0};
    if (mock_mode == 5) {
        mock_time += 100000;
        memcpy(output, response, sizeof response);
        *length = sizeof response;
        return TB_OK;
    }
    if (request.op == TB_TARGET_HELLO) {
        request_sequence = request.sequence;
        assert(make_response(&request, mock_status, pending_spi, sizeof pending_spi, true) ==
               20 + (mock_status == TB_TARGET_OK ? sizeof hello : 0));
    } else {
        assert(request.op == TB_TARGET_POLL && request.sequence == request_sequence);
        if (busy_polls) {
            struct tb_target_frame busy = {
                .op = TB_TARGET_HELLO, .status = TB_TARGET_BUSY, .sequence = request.sequence};
            tb_target_spi_encode(&busy, response);
            busy_polls--;
        } else {
            memcpy(response, pending_spi, sizeof response);
        }
    }
    memcpy(output, response, sizeof response);
    *length = sizeof response;
    mock_time += 1000;
    return TB_OK;
}

static void reset_mock(void)
{
    memset(tb_pin_config, 0, sizeof tb_pin_config);
    tb_pin_config[TB_UART].count = 2;
    tb_pin_config[TB_SPI].count = 4;
    mock_mode = 0;
    mock_status = TB_TARGET_OK;
    mock_response_size = 0;
    mock_response_offset = 0;
    busy_polls = 0;
    mock_time = 0;
    fill_hello();
}

int main(void)
{
    uint8_t out[TB_TARGET_HELLO_SIZE];
    size_t size = 0;
    struct tb_target_hello decoded;
    reset_mock();
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_OK && size == sizeof out);
    assert(tb_target_hello_decode(out, size, &decoded) == TB_OK && !strcmp(decoded.board_id, "test-mcu"));

    const struct
    {
        uint8_t target_status;
        enum tb_status rpc_status;
    } statuses[] = {{TB_TARGET_INVALID, TB_INVALID},         {TB_TARGET_BUSY, TB_BUSY},
                    {TB_TARGET_UNSUPPORTED, TB_UNSUPPORTED}, {TB_TARGET_IO, TB_IO},
                    {TB_TARGET_INTEGRITY, TB_INTEGRITY},     {TB_TARGET_STATE, TB_STATE},
                    {TB_TARGET_CONFLICT, TB_CONFLICT},       {TB_TARGET_TIMEOUT, TB_TIMEOUT}};
    for (size_t i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
        size = 123;
        mock_status = statuses[i].target_status;
        assert(tb_target_ota_info(TB_UART, out, &size) == statuses[i].rpc_status && size == 0);
    }
    mock_status = TB_TARGET_OK;
    mock_mode = 1;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_CONFLICT);
    mock_mode = 3;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_INVALID);
    mock_mode = 4;
    mock_status = TB_TARGET_BUSY;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_INVALID);
    mock_mode = 2;
    mock_status = TB_TARGET_OK;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_TIMEOUT);
    mock_mode = 5;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_TIMEOUT);

    reset_mock();
    busy_polls = 1;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_OK && size == sizeof out);
    assert(tb_target_hello_decode(out, size, &decoded) == TB_OK && decoded.rollback_index == 4);
    busy_polls = 1000;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_TIMEOUT);
    mock_mode = 5;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_TIMEOUT);
    tb_pin_config[TB_SPI].count = 0;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_STATE);
    assert(tb_target_ota_info(TB_I2C, out, &size) == TB_INVALID);
    puts("target link: UART framing, SPI polling, status mapping and deadlines passed");
}
