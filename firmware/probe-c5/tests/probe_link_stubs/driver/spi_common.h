#pragma once

#include <stddef.h>

typedef int spi_host_device_t;
typedef int spi_dma_chan_t;
typedef struct
{
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

#define SPI2_HOST 1
#define SPI_DMA_CH_AUTO 3
