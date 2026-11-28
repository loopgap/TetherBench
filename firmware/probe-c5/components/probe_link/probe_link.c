#include "probe_link.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static bool link_ready;
static enum tb_probe_link_profile link_profile;
static unsigned integrity_errors;
static DMA_ATTR uint8_t tx_wire[TB_TARGET_SPI_SIZE];
static DMA_ATTR uint8_t rx_wire[TB_TARGET_SPI_SIZE];

static void link_reset_state(void)
{
    memset(tx_wire, 0, sizeof tx_wire);
    memset(rx_wire, 0, sizeof rx_wire);
    link_ready = false;
    link_profile = TB_PROBE_LINK_PROFILE_DISABLED;
    integrity_errors = 0;
}

static void link_fail_closed(void)
{
    if (link_ready)
        (void)spi_slave_free(SPI2_HOST);
    (void)gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 0);
    (void)gpio_reset_pin(TB_PROBE_LINK_IRQ_GPIO);
    link_reset_state();
}

esp_err_t tb_probe_link_init(const struct tb_probe_link_config *config)
{
    if (!config)
        return ESP_ERR_INVALID_ARG;
    if (!config->enabled || config->profile == TB_PROBE_LINK_PROFILE_DISABLED)
        return link_ready ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (config->profile != TB_PROBE_LINK_PROFILE_WT_TINY_P4_C5_SPI_V1)
        return ESP_ERR_INVALID_ARG;
    if (link_ready)
        return ESP_ERR_INVALID_STATE;

    gpio_config_t irq = {.pin_bit_mask = 1ULL << TB_PROBE_LINK_IRQ_GPIO,
                         .mode = GPIO_MODE_OUTPUT,
                         .pull_up_en = GPIO_PULLUP_DISABLE,
                         .pull_down_en = GPIO_PULLDOWN_DISABLE,
                         .intr_type = GPIO_INTR_DISABLE};
    esp_err_t err = gpio_config(&irq);
    if (err != ESP_OK)
        return err;
    err = gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 0);
    if (err != ESP_OK)
        goto fail_gpio;

    spi_bus_config_t bus = {.mosi_io_num = TB_PROBE_LINK_MOSI_GPIO,
                            .miso_io_num = TB_PROBE_LINK_MISO_GPIO,
                            .sclk_io_num = TB_PROBE_LINK_SCLK_GPIO,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = TB_TARGET_SPI_SIZE};
    spi_slave_interface_config_t slave = {.spics_io_num = TB_PROBE_LINK_CS_GPIO, .queue_size = 1, .mode = 0};
    err = spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
        goto fail_gpio;

    memset(tx_wire, 0, sizeof tx_wire);
    memset(rx_wire, 0, sizeof rx_wire);
    link_ready = true;
    link_profile = config->profile;
    return ESP_OK;

fail_gpio:
    gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 0);
    gpio_reset_pin(TB_PROBE_LINK_IRQ_GPIO);
    return err;
}

esp_err_t tb_probe_link_stop(void)
{
    if (!link_ready)
        return ESP_OK;
    esp_err_t err = spi_slave_free(SPI2_HOST);
    if (err != ESP_OK)
        return err;
    gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 0);
    gpio_reset_pin(TB_PROBE_LINK_IRQ_GPIO);
    link_reset_state();
    return ESP_OK;
}

bool tb_probe_link_active(void)
{
    return link_ready;
}

enum tb_probe_link_profile tb_probe_link_get_profile(void)
{
    return link_profile;
}

bool tb_probe_link_uses_gpio(int gpio)
{
    if (!link_ready)
        return false;
    return gpio == TB_PROBE_LINK_SCLK_GPIO || gpio == TB_PROBE_LINK_MOSI_GPIO || gpio == TB_PROBE_LINK_MISO_GPIO ||
           gpio == TB_PROBE_LINK_CS_GPIO || gpio == TB_PROBE_LINK_IRQ_GPIO;
}

enum tb_status tb_probe_link_xfer(const struct tb_target_frame *response, struct tb_target_frame *request,
                                  uint8_t request_payload[TB_TARGET_PAYLOAD_MAX], uint32_t timeout_ms)
{
    if (!link_ready)
        return TB_STATE;
    if (!request || !request_payload || !timeout_ms || timeout_ms > TB_PROBE_LINK_MAX_TIMEOUT_MS)
        return TB_INVALID;

    if (response) {
        if (tb_target_spi_encode(response, tx_wire) != 20 + response->size)
            return TB_INVALID;
    } else {
        memset(tx_wire, 0, sizeof tx_wire);
    }
    memset(rx_wire, 0, sizeof rx_wire);

    spi_slave_transaction_t transaction = {
        .length = TB_TARGET_SPI_SIZE * 8u, .tx_buffer = tx_wire, .rx_buffer = rx_wire};
    if (gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 1) != ESP_OK) {
        link_fail_closed();
        return TB_IO;
    }
    esp_err_t err = spi_slave_transmit(SPI2_HOST, &transaction, pdMS_TO_TICKS(timeout_ms));
    gpio_set_level(TB_PROBE_LINK_IRQ_GPIO, 0);
    if (err == ESP_ERR_TIMEOUT) {
        link_fail_closed();
        return TB_TIMEOUT;
    }
    if (err != ESP_OK) {
        link_fail_closed();
        return TB_IO;
    }
    if (transaction.trans_len != TB_TARGET_SPI_SIZE * 8u) {
        link_fail_closed();
        return TB_INVALID;
    }
    enum tb_status status = tb_target_spi_decode(rx_wire, sizeof rx_wire, request, request_payload);
    if (status == TB_OK) {
        integrity_errors = 0;
    } else if (status == TB_INTEGRITY && ++integrity_errors >= 2) {
        link_fail_closed();
    }
    return status;
}
