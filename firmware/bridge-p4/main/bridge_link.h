#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bridge_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_BRIDGE_LINK_SCLK_GPIO 16
#define TB_BRIDGE_LINK_MOSI_GPIO 17
#define TB_BRIDGE_LINK_MISO_GPIO 18
#define TB_BRIDGE_LINK_CS_GPIO 19
#define TB_BRIDGE_LINK_IRQ_GPIO 20
#define TB_BRIDGE_LINK_RESET_GPIO 21
#define TB_BRIDGE_LINK_SPI_HZ 8000000
#define TB_BRIDGE_LINK_TRANSACTION_BYTES 1024u
#define TB_BRIDGE_LINK_TIMEOUT_MS 100u

/* The link starts stopped; no C5 pin is enabled until init succeeds. */
bool tb_bridge_link_init(void);
void tb_bridge_link_stop(void);
bool tb_bridge_link_is_online(void);

/* Transfer exactly one padded TBUP transaction when C5 asserts IRQ. */
bool tb_bridge_link_transfer(const uint8_t tx[TB_BRIDGE_LINK_TRANSACTION_BYTES],
                             uint8_t rx[TB_BRIDGE_LINK_TRANSACTION_BYTES]);

#ifdef __cplusplus
}
#endif
