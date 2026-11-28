#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "probe_link.h"

static spi_bus_config_t saved_bus;
static spi_slave_interface_config_t saved_slave;
static uint8_t mock_rx[TB_TARGET_SPI_SIZE];
static uint8_t captured_tx[TB_TARGET_SPI_SIZE];
static esp_err_t mock_init_error, mock_free_error, mock_transmit_error;
static size_t mock_trans_len;
static unsigned mock_init_calls, mock_free_calls, mock_transmit_calls;
static int mock_irq_level, mock_reset_pin;

esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config);
    assert(config->pin_bit_mask == 1ULL << TB_PROBE_LINK_IRQ_GPIO);
    return ESP_OK;
}
esp_err_t gpio_set_level(int gpio_num, uint32_t level)
{
    assert(gpio_num == TB_PROBE_LINK_IRQ_GPIO);
    mock_irq_level = (int)level;
    return ESP_OK;
}
esp_err_t gpio_reset_pin(int gpio_num)
{
    mock_reset_pin = gpio_num;
    return ESP_OK;
}
esp_err_t spi_slave_initialize(spi_host_device_t host, const spi_bus_config_t *bus_config,
                               const spi_slave_interface_config_t *slave_config, spi_dma_chan_t dma_chan)
{
    assert(host == SPI2_HOST && dma_chan == SPI_DMA_CH_AUTO);
    saved_bus = *bus_config;
    saved_slave = *slave_config;
    mock_init_calls++;
    return mock_init_error;
}
esp_err_t spi_slave_free(spi_host_device_t host)
{
    assert(host == SPI2_HOST);
    mock_free_calls++;
    return mock_free_error;
}
esp_err_t spi_slave_transmit(spi_host_device_t host, spi_slave_transaction_t *transaction, uint32_t ticks_to_wait)
{
    assert(host == SPI2_HOST && transaction && transaction->length == TB_TARGET_SPI_SIZE * 8u && ticks_to_wait);
    mock_transmit_calls++;
    memcpy(captured_tx, transaction->tx_buffer, sizeof captured_tx);
    memcpy(transaction->rx_buffer, mock_rx, sizeof mock_rx);
    transaction->trans_len = mock_trans_len;
    return mock_transmit_error;
}

static void reset_mock(void)
{
    memset(&saved_bus, 0, sizeof saved_bus);
    memset(&saved_slave, 0, sizeof saved_slave);
    memset(mock_rx, 0, sizeof mock_rx);
    memset(captured_tx, 0, sizeof captured_tx);
    mock_init_error = ESP_OK;
    mock_free_error = ESP_OK;
    mock_transmit_error = ESP_OK;
    mock_trans_len = TB_TARGET_SPI_SIZE * 8u;
    mock_init_calls = mock_free_calls = mock_transmit_calls = 0;
    mock_irq_level = -1;
    mock_reset_pin = -1;
}

static void make_frame(uint8_t op, uint32_t sequence, struct tb_target_frame *frame, uint8_t payload[4])
{
    payload[0] = 1;
    payload[1] = 2;
    payload[2] = 3;
    payload[3] = 4;
    *frame = (struct tb_target_frame){.op = op, .sequence = sequence, .data = payload, .size = 4};
}

int main(void)
{
    struct tb_probe_link_config disabled = {.enabled = false, .profile = TB_PROBE_LINK_PROFILE_DISABLED};
    struct tb_target_frame request = {0};
    uint8_t request_payload[TB_TARGET_PAYLOAD_MAX];
    reset_mock();
    assert(tb_probe_link_init(&disabled) == ESP_OK);
    assert(!tb_probe_link_active() && tb_probe_link_get_profile() == TB_PROBE_LINK_PROFILE_DISABLED);
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 1) == TB_STATE);
    assert(mock_init_calls == 0);
    assert(tb_probe_link_init(NULL) == ESP_ERR_INVALID_ARG);

    struct tb_probe_link_config invalid = {.enabled = true, .profile = TB_PROBE_LINK_PROFILE_DISABLED};
    assert(tb_probe_link_init(&invalid) == ESP_OK);
    struct tb_probe_link_config enabled = {.enabled = true, .profile = TB_PROBE_LINK_PROFILE_WT_TINY_P4_C5_SPI_V1};
    assert(tb_probe_link_init(&enabled) == ESP_OK);
    assert(tb_probe_link_active() && tb_probe_link_get_profile() == TB_PROBE_LINK_PROFILE_WT_TINY_P4_C5_SPI_V1 &&
           tb_probe_link_uses_gpio(TB_PROBE_LINK_SCLK_GPIO) && tb_probe_link_uses_gpio(TB_PROBE_LINK_IRQ_GPIO) &&
           !tb_probe_link_uses_gpio(10));
    assert(mock_init_calls == 1 && saved_bus.sclk_io_num == 0 && saved_bus.mosi_io_num == 1 &&
           saved_bus.miso_io_num == 8 && saved_bus.max_transfer_sz == TB_TARGET_SPI_SIZE &&
           saved_slave.spics_io_num == 9 && saved_slave.queue_size == 1 && saved_slave.mode == 0 &&
           mock_irq_level == 0);
    assert(tb_probe_link_init(&enabled) == ESP_ERR_INVALID_STATE);

    uint8_t request_wire[TB_TARGET_SPI_SIZE], request_data[4], response_data[4];
    struct tb_target_frame response;
    make_frame(TB_TARGET_WRITE, 9, &request, request_data);
    assert(tb_target_spi_encode(&request, request_wire) == 24);
    memcpy(mock_rx, request_wire, sizeof mock_rx);
    make_frame(TB_TARGET_STATUS, 8, &response, response_data);
    assert(tb_probe_link_xfer(&response, &request, request_payload, 10) == TB_OK);
    assert(mock_transmit_calls == 1 && mock_irq_level == 0 && request.op == TB_TARGET_WRITE && request.sequence == 9 &&
           request.size == 4 && !memcmp(request.data, request_data, 4));
    struct tb_target_frame decoded_response = {0};
    uint8_t response_payload[TB_TARGET_PAYLOAD_MAX];
    assert(tb_target_spi_decode(captured_tx, sizeof captured_tx, &decoded_response, response_payload) == TB_OK);
    assert(decoded_response.op == TB_TARGET_STATUS && decoded_response.sequence == 8 && decoded_response.size == 4 &&
           !memcmp(decoded_response.data, response_data, 4));

    assert(tb_probe_link_xfer(NULL, &request, request_payload, 0) == TB_INVALID);
    mock_transmit_error = ESP_ERR_TIMEOUT;
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 10) == TB_TIMEOUT && mock_irq_level == 0 &&
           !tb_probe_link_active() && mock_free_calls == 1);
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 10) == TB_STATE);
    mock_transmit_error = ESP_OK;
    assert(tb_probe_link_init(&enabled) == ESP_OK);
    mock_transmit_error = ESP_OK;
    mock_trans_len = 8;
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 10) == TB_INVALID && !tb_probe_link_active() &&
           mock_free_calls == 2);
    assert(tb_probe_link_init(&enabled) == ESP_OK);
    mock_trans_len = TB_TARGET_SPI_SIZE * 8u;
    memcpy(mock_rx, request_wire, sizeof mock_rx);
    mock_rx[20] ^= 1;
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 10) == TB_INTEGRITY && tb_probe_link_active());
    assert(tb_probe_link_xfer(NULL, &request, request_payload, 10) == TB_INTEGRITY && !tb_probe_link_active() &&
           mock_free_calls == 3);
    assert(tb_probe_link_stop() == ESP_OK && mock_free_calls == 3 && mock_reset_pin == TB_PROBE_LINK_IRQ_GPIO);
    assert(tb_probe_link_stop() == ESP_OK && mock_free_calls == 3);
    puts("probe link: disabled default, WT9932C5-TINY pins, fixed frame xfer and error bounds passed");
    return 0;
}
