#include "bridge_link.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static spi_device_handle_t device;
static bool bus_initialized;
static bool started;
static DMA_ATTR uint8_t tx_wire[TB_BRIDGE_LINK_TRANSACTION_BYTES];
static DMA_ATTR uint8_t rx_wire[TB_BRIDGE_LINK_TRANSACTION_BYTES];

#define TB_BRIDGE_LINK_RESET_HOLD_MS 10u

static bool gpio_ok(esp_err_t err)
{
    return err == ESP_OK;
}

static void force_safe_pins(void)
{
    /* Set output latches before enabling output to avoid an active-low glitch. */
    (void)gpio_reset_pin(GPIO_NUM_19);
    (void)gpio_set_level(GPIO_NUM_19, 1);
    (void)gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT);

    (void)gpio_reset_pin(GPIO_NUM_21);
    (void)gpio_set_level(GPIO_NUM_21, 0);
    (void)gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT_OD);

    (void)gpio_reset_pin(GPIO_NUM_20);
    (void)gpio_set_direction(GPIO_NUM_20, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(GPIO_NUM_20, GPIO_FLOATING);
    (void)gpio_reset_pin(GPIO_NUM_16);
    (void)gpio_reset_pin(GPIO_NUM_17);
    (void)gpio_reset_pin(GPIO_NUM_18);
}

static bool configure_safe_pins(void)
{
    bool ok = true;
    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_16)) && ok;
    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_17)) && ok;
    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_18)) && ok;

    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_19)) && ok;
    ok = gpio_ok(gpio_set_level(GPIO_NUM_19, 1)) && ok;
    ok = gpio_ok(gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT)) && ok;

    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_20)) && ok;
    ok = gpio_ok(gpio_set_direction(GPIO_NUM_20, GPIO_MODE_INPUT)) && ok;
    ok = gpio_ok(gpio_set_pull_mode(GPIO_NUM_20, GPIO_FLOATING)) && ok;

    ok = gpio_ok(gpio_reset_pin(GPIO_NUM_21)) && ok;
    ok = gpio_ok(gpio_set_level(GPIO_NUM_21, 0)) && ok;
    ok = gpio_ok(gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT_OD)) && ok;
    return ok;
}

static void release_resources(void)
{
    started = false;
    if (device) {
        (void)spi_bus_remove_device(device);
        device = NULL;
    }
    if (bus_initialized) {
        (void)spi_bus_free(SPI2_HOST);
        bus_initialized = false;
    }
    force_safe_pins();
}

bool tb_bridge_link_init(void)
{
    if (started) return true;
    if (!configure_safe_pins()) {
        release_resources();
        return false;
    }

    /* Keep C5 held in reset while the P4 SPI bus and CS route are installed. */
    vTaskDelay(pdMS_TO_TICKS(TB_BRIDGE_LINK_RESET_HOLD_MS));
    spi_bus_config_t bus = {
        .mosi_io_num = TB_BRIDGE_LINK_MOSI_GPIO,
        .miso_io_num = TB_BRIDGE_LINK_MISO_GPIO,
        .sclk_io_num = TB_BRIDGE_LINK_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TB_BRIDGE_LINK_TRANSACTION_BYTES,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        release_resources();
        return false;
    }
    bus_initialized = true;

    spi_device_interface_config_t config = {
        .mode = 0,
        .clock_speed_hz = TB_BRIDGE_LINK_SPI_HZ,
        .spics_io_num = TB_BRIDGE_LINK_CS_GPIO,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &config, &device) != ESP_OK || !device) {
        release_resources();
        return false;
    }
    /* CS must be high before the reset line is released. */
    if (gpio_set_level(GPIO_NUM_19, 1) != ESP_OK || gpio_set_level(GPIO_NUM_21, 1) != ESP_OK) {
        release_resources();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    if (gpio_get_level(GPIO_NUM_19) != 1 || gpio_get_level(GPIO_NUM_21) != 1) {
        release_resources();
        return false;
    }
    started = true;
    return true;
}

void tb_bridge_link_stop(void)
{
    release_resources();
}

bool tb_bridge_link_is_online(void)
{
    return started && device && gpio_get_level(GPIO_NUM_19) == 1 && gpio_get_level(GPIO_NUM_21) == 1 &&
           gpio_get_level(GPIO_NUM_20) == 1;
}

bool tb_bridge_link_transfer(const uint8_t tx[TB_BRIDGE_LINK_TRANSACTION_BYTES],
                             uint8_t rx[TB_BRIDGE_LINK_TRANSACTION_BYTES])
{
    if (!tx || !rx || !started || !device) return false;
    if (gpio_get_level(GPIO_NUM_19) != 1 || gpio_get_level(GPIO_NUM_21) != 1) {
        release_resources();
        return false;
    }
    if (gpio_get_level(GPIO_NUM_20) != 1) return false;

    memcpy(tx_wire, tx, sizeof tx_wire);
    memset(rx_wire, 0, sizeof rx_wire);
    spi_transaction_t transaction = {
        .length = TB_BRIDGE_LINK_TRANSACTION_BYTES * 8u,
        .rxlength = TB_BRIDGE_LINK_TRANSACTION_BYTES * 8u,
        .tx_buffer = tx_wire,
        .rx_buffer = rx_wire,
    };
    if (spi_device_polling_transmit(device, &transaction) != ESP_OK) {
        release_resources();
        return false;
    }
    /* Treat a stuck-low CS as a transaction failure and leave the link reset. */
    if (gpio_get_level(GPIO_NUM_19) != 1) {
        release_resources();
        return false;
    }
    memcpy(rx, rx_wire, sizeof rx_wire);
    return true;
}
