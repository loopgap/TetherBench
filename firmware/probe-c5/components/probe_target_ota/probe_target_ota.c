#include "probe_target_ota.h"
#include "probe_io.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>

#define TB_TARGET_TIMEOUT_MS 100u

static uint32_t sequence;

static uint32_t next_sequence(void)
{
    if (!sequence) {
        sequence = esp_random();
        if (!sequence)
            sequence = 1;
    }
    uint32_t current = sequence++;
    if (!sequence)
        sequence = 1;
    return current;
}

static enum tb_status bus_call(uint8_t protocol, const uint8_t *tx, size_t tx_size, uint8_t *rx, size_t rx_size,
                               uint32_t timeout_ms, size_t *received)
{
    if (!received || tx_size > TB_TARGET_UART_MAX || rx_size > TB_TARGET_UART_MAX || (!tx_size && !rx_size) ||
        (tx_size && !tx) || (rx_size && !rx) || !timeout_ms || timeout_ms > 100)
        return TB_INVALID;
    uint8_t input[12 + TB_TARGET_UART_MAX] = {0};
    input[0] = protocol;
    tb_put16(input + 4, (uint16_t)tx_size);
    tb_put16(input + 6, (uint16_t)rx_size);
    tb_put16(input + 8, (uint16_t)timeout_ms);
    if (tx_size)
        memcpy(input + 12, tx, tx_size);
    return tb_io_bus(input, 12 + tx_size, rx, received);
}

static enum tb_status target_status(enum tb_target_status status)
{
    switch (status) {
    case TB_TARGET_OK:
        return TB_OK;
    case TB_TARGET_INVALID:
        return TB_INVALID;
    case TB_TARGET_BUSY:
        return TB_BUSY;
    case TB_TARGET_UNSUPPORTED:
        return TB_UNSUPPORTED;
    case TB_TARGET_IO:
        return TB_IO;
    case TB_TARGET_INTEGRITY:
        return TB_INTEGRITY;
    case TB_TARGET_STATE:
        return TB_STATE;
    case TB_TARGET_CONFLICT:
        return TB_CONFLICT;
    case TB_TARGET_TIMEOUT:
        return TB_TIMEOUT;
    default:
        return TB_INVALID;
    }
}

static enum tb_status check_response(const struct tb_target_frame *response, uint32_t expected_sequence,
                                     uint8_t expected_op)
{
    if (!response)
        return TB_INVALID;
    if (response->sequence != expected_sequence)
        return TB_CONFLICT;
    if (response->op != expected_op)
        return TB_INVALID;
    if (response->status != TB_TARGET_OK && response->size)
        return TB_INVALID;
    return target_status((enum tb_target_status)response->status);
}

static uint32_t remaining_ms(uint64_t deadline)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (now >= deadline)
        return 0;
    uint64_t remaining = (deadline - now) / 1000;
    return remaining > TB_TARGET_TIMEOUT_MS ? TB_TARGET_TIMEOUT_MS : (uint32_t)remaining;
}

static enum tb_status uart_exchange(const struct tb_target_frame *request, struct tb_target_frame *response,
                                    uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    uint8_t tx[TB_TARGET_UART_MAX], rx[TB_TARGET_UART_MAX];
    size_t tx_size = tb_target_uart_encode(request, tx, sizeof tx), received = 0;
    if (!tx_size)
        return TB_INVALID;
    uint64_t deadline = (uint64_t)esp_timer_get_time() + TB_TARGET_TIMEOUT_MS * 1000u;
    uint32_t timeout = remaining_ms(deadline);
    if (!timeout)
        return TB_TIMEOUT;
    enum tb_status status = bus_call(TB_UART, tx, tx_size, NULL, 0, timeout, &received);
    if (status != TB_OK)
        return status;
    if (!remaining_ms(deadline))
        return TB_TIMEOUT;
    while (received < sizeof rx) {
        timeout = remaining_ms(deadline);
        if (!timeout)
            return TB_TIMEOUT;
        size_t count = 0;
        status = bus_call(TB_UART, NULL, 0, rx + received, sizeof rx - received, timeout, &count);
        if (status != TB_OK)
            return status;
        if (!remaining_ms(deadline))
            return TB_TIMEOUT;
        if (count > sizeof rx - received)
            return TB_IO;
        received += count;
        uint8_t *delimiter = memchr(rx, 0, received);
        if (delimiter) {
            if (delimiter != rx + received - 1)
                return TB_INVALID;
            status = tb_target_uart_decode(rx, received, response, payload);
            return status == TB_OK ? check_response(response, request->sequence, request->op) : status;
        }
    }
    return TB_INVALID;
}

static enum tb_status spi_exchange(const struct tb_target_frame *request, struct tb_target_frame *response,
                                   uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    uint8_t tx[TB_TARGET_SPI_SIZE], rx[TB_TARGET_SPI_SIZE];
    if (tb_target_spi_encode(request, tx) != 20 + request->size)
        return TB_INVALID;
    uint64_t deadline = (uint64_t)esp_timer_get_time() + TB_TARGET_TIMEOUT_MS * 1000u;
    uint32_t timeout = remaining_ms(deadline);
    if (!timeout)
        return TB_TIMEOUT;
    size_t received = 0;
    enum tb_status status = bus_call(TB_SPI, tx, sizeof tx, rx, sizeof rx, timeout, &received);
    if (status != TB_OK)
        return status;
    if (!remaining_ms(deadline))
        return TB_TIMEOUT;
    if (received != sizeof rx)
        return TB_IO;

    const struct tb_target_frame poll = {.op = TB_TARGET_POLL, .sequence = request->sequence};
    while (true) {
        timeout = remaining_ms(deadline);
        if (!timeout)
            return TB_TIMEOUT;
        if (tb_target_spi_encode(&poll, tx) != 20)
            return TB_INVALID;
        received = 0;
        status = bus_call(TB_SPI, tx, sizeof tx, rx, sizeof rx, timeout, &received);
        if (status != TB_OK)
            return status;
        if (!remaining_ms(deadline))
            return TB_TIMEOUT;
        if (received != sizeof rx)
            return TB_IO;
        status = tb_target_spi_decode(rx, sizeof rx, response, payload);
        if (status != TB_OK)
            return status;
        status = check_response(response, request->sequence, request->op);
        if (status != TB_BUSY)
            return status;
    }
}

static enum tb_status exchange(uint8_t protocol, uint8_t op, const uint8_t *data, size_t data_size,
                               uint8_t payload[TB_TARGET_PAYLOAD_MAX], size_t *payload_size)
{
    if ((protocol != TB_UART && protocol != TB_SPI) || !tb_pin_config[protocol].count)
        return TB_STATE;
    struct tb_target_frame request = {.op = op, .sequence = next_sequence(), .data = data, .size = data_size};
    struct tb_target_frame response = {0};
    enum tb_status status =
        protocol == TB_UART ? uart_exchange(&request, &response, payload) : spi_exchange(&request, &response, payload);
    if (status == TB_OK)
        *payload_size = response.size;
    return status;
}

enum tb_status tb_target_ota_info(uint8_t protocol, uint8_t *out, size_t *size)
{
    if (!out || !size || (protocol != TB_UART && protocol != TB_SPI))
        return TB_INVALID;
    *size = 0;
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    size_t payload_size = 0;
    enum tb_status status = exchange(protocol, TB_TARGET_HELLO, NULL, 0, payload, &payload_size);
    if (status != TB_OK)
        return status;
    if (payload_size != TB_TARGET_HELLO_SIZE)
        return TB_INVALID;
    struct tb_target_hello hello;
    status = tb_target_hello_decode(payload, payload_size, &hello);
    if (status != TB_OK)
        return status;
    memcpy(out, payload, payload_size);
    *size = payload_size;
    return TB_OK;
}
