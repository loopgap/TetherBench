#pragma once

#include "driver/spi_common.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int spics_io_num;
    uint32_t flags;
    int queue_size;
    uint8_t mode;
    void *post_setup_cb;
    void *post_trans_cb;
} spi_slave_interface_config_t;

typedef struct
{
    uint32_t flags;
    size_t length;
    size_t trans_len;
    const void *tx_buffer;
    void *rx_buffer;
    void *user;
} spi_slave_transaction_t;

esp_err_t spi_slave_initialize(spi_host_device_t host, const spi_bus_config_t *bus_config,
                               const spi_slave_interface_config_t *slave_config, spi_dma_chan_t dma_chan);
esp_err_t spi_slave_free(spi_host_device_t host);
esp_err_t spi_slave_transmit(spi_host_device_t host, spi_slave_transaction_t *trans_desc, uint32_t ticks_to_wait);
