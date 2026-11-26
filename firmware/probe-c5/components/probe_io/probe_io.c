#include "probe_io.h"
#include "device_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"
#include <string.h>

struct tb_pins tb_pin_config[TB_PROTOCOL_COUNT];
static bool uart_ready, spi_ready;
static spi_device_handle_t spi_device;
static i2c_master_bus_handle_t i2c_bus;

uint32_t tb_allowed_pins(void)
{
    const uint32_t forbidden = (0x7fu << 16) | (3u << 11) | (3u << 13) | (7u << 26);
    return TB_ALLOWED_GPIO_MASK & SOC_GPIO_VALID_GPIO_MASK & ~forbidden;
}
void tb_swd_off(void)
{
    const struct tb_pins *p = &tb_pin_config[TB_SWD];
    for (unsigned i = 0; i < p->count; i++) {
        gpio_set_direction(p->pin[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(p->pin[i], GPIO_FLOATING);
    }
}
void tb_swd_on(void)
{
    const struct tb_pins *p = &tb_pin_config[TB_SWD];
    if (p->count < 2)
        return;
    for (unsigned i = 0; i < p->count; i++) {
        gpio_set_level(p->pin[i], 1);
        gpio_set_direction(p->pin[i], i == 2 ? GPIO_MODE_INPUT_OUTPUT_OD : GPIO_MODE_INPUT_OUTPUT);
        gpio_set_drive_capability(p->pin[i], GPIO_DRIVE_CAP_2);
    }
}
static void release_protocol(unsigned protocol)
{
    if (protocol == TB_UART && uart_ready) {
        uart_driver_delete(UART_NUM_1);
        uart_ready = false;
    }
    if (protocol == TB_SPI && spi_ready) {
        if (spi_device)
            spi_bus_remove_device(spi_device);
        spi_device = NULL;
        spi_bus_free(SPI2_HOST);
        spi_ready = false;
    }
    if (protocol == TB_I2C && i2c_bus) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
    struct tb_pins *p = &tb_pin_config[protocol];
    for (unsigned i = 0; i < p->count; i++) {
        gpio_reset_pin(p->pin[i]);
        gpio_set_direction(p->pin[i], GPIO_MODE_INPUT);
    }
    memset(p, 0, sizeof *p);
    memset(p->pin, -1, sizeof p->pin);
}
void tb_io_release(void)
{
    for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++)
        release_protocol(i);
}
enum tb_status tb_io_configure(const struct tb_pins *p)
{
    uint32_t occupied = 0;
    for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++) {
        if (i == p->protocol)
            continue;
        for (unsigned k = 0; k < tb_pin_config[i].count; k++)
            occupied |= 1u << tb_pin_config[i].pin[k];
    }
    enum tb_status status = tb_validate_pins(p, tb_allowed_pins(), occupied);
    if (status != TB_OK)
        return status;
    release_protocol(p->protocol);
    if (!p->count)
        return TB_OK;
    esp_err_t err = ESP_OK;
    tb_pin_config[p->protocol] = *p;
    if (p->protocol == TB_UART) {
        uart_config_t cfg = {.baud_rate = (int)p->hz,
                             .data_bits = UART_DATA_8_BITS,
                             .parity = UART_PARITY_DISABLE,
                             .stop_bits = UART_STOP_BITS_1,
                             .flow_ctrl = p->count == 4 ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE,
                             .rx_flow_ctrl_thresh = 64,
                             .source_clk = UART_SCLK_DEFAULT};
        err = uart_param_config(UART_NUM_1, &cfg);
        if (err == ESP_OK)
            err = uart_set_pin(UART_NUM_1, p->pin[0], p->pin[1], p->pin[2], p->pin[3]);
        if (err == ESP_OK) {
            err = uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0);
            uart_ready = err == ESP_OK;
        }
    } else if (p->protocol == TB_SPI) {
        spi_bus_config_t bus = {.mosi_io_num = p->pin[1],
                                .miso_io_num = p->pin[2],
                                .sclk_io_num = p->pin[0],
                                .quadwp_io_num = -1,
                                .quadhd_io_num = -1,
                                .max_transfer_sz = 1024};
        err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        spi_ready = err == ESP_OK;
        spi_device_interface_config_t device = {
            .clock_speed_hz = (int)p->hz, .mode = 0, .spics_io_num = p->pin[3], .queue_size = 1};
        if (err == ESP_OK)
            err = spi_bus_add_device(SPI2_HOST, &device, &spi_device);
    } else if (p->protocol == TB_I2C) {
        i2c_master_bus_config_t cfg = {.i2c_port = I2C_NUM_0,
                                       .sda_io_num = p->pin[1],
                                       .scl_io_num = p->pin[0],
                                       .clk_source = I2C_CLK_SRC_DEFAULT,
                                       .glitch_ignore_cnt = 7,
                                       .flags.enable_internal_pullup = false};
        err = i2c_new_master_bus(&cfg, &i2c_bus);
    } else
        tb_swd_off();
    if (err != ESP_OK) {
        release_protocol(p->protocol);
        return TB_IO;
    }
    return TB_OK;
}
enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length)
{
    if (size < 12)
        return TB_INVALID;
    unsigned protocol = input[0], flags = input[1], address = tb_u16(input + 2);
    unsigned tx = tb_u16(input + 4), rx = tb_u16(input + 6), timeout = tb_u16(input + 8);
    if (flags || tb_u16(input + 10) || tx > 1024 || rx > 1024 || size != 12 + tx || !timeout || timeout > 100 ||
        protocol < TB_UART || protocol > TB_I2C || (!tx && !rx))
        return TB_INVALID;
    if (!tb_pin_config[protocol].count)
        return TB_STATE;
    const uint8_t *data = input + 12;
    *length = 0;
    if (protocol == TB_UART) {
        if (address)
            return TB_INVALID;
        if ((uint64_t)tx * 10000 >= (uint64_t)tb_pin_config[protocol].hz * timeout)
            return TB_INVALID;
        if (tx && uart_write_bytes(UART_NUM_1, data, tx) != (int)tx)
            return TB_IO;
        if (tx && uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(timeout)) != ESP_OK) {
            release_protocol(TB_UART);
            return TB_TIMEOUT;
        }
        int count = rx ? uart_read_bytes(UART_NUM_1, output, rx, pdMS_TO_TICKS(timeout)) : 0;
        if (count < 0)
            return TB_IO;
        *length = (size_t)count;
        return TB_OK;
    }
    if (protocol == TB_SPI) {
        if (address || (tx && rx && tx != rx))
            return TB_INVALID;
        if ((uint64_t)(tx ? tx : rx) * 8000 >= (uint64_t)tb_pin_config[protocol].hz * timeout)
            return TB_INVALID;
        spi_transaction_t t = {.length = 8 * (tx ? tx : rx),
                               .rxlength = 8 * rx,
                               .tx_buffer = tx ? data : NULL,
                               .rx_buffer = rx ? output : NULL};
        if (spi_device_transmit(spi_device, &t) != ESP_OK)
            return TB_IO;
        *length = rx;
        return TB_OK;
    }
    if (address < 8 || address > 0x77)
        return TB_INVALID;
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                               .device_address = address,
                               .scl_speed_hz = tb_pin_config[TB_I2C].hz,
                               .scl_wait_us = 1000};
    i2c_master_dev_handle_t device;
    if (i2c_master_bus_add_device(i2c_bus, &cfg, &device) != ESP_OK)
        return TB_IO;
    esp_err_t err = tx && rx ? i2c_master_transmit_receive(device, data, tx, output, rx, timeout)
                    : tx     ? i2c_master_transmit(device, data, tx, timeout)
                             : i2c_master_receive(device, output, rx, timeout);
    i2c_master_bus_rm_device(device);
    if (err != ESP_OK)
        return err == ESP_ERR_TIMEOUT ? TB_TIMEOUT : TB_IO;
    *length = rx;
    return TB_OK;
}
