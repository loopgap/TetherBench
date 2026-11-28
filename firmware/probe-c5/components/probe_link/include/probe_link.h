#pragma once

#include "esp_err.h"
#include "probe_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_PROBE_LINK_SCLK_GPIO 0
#define TB_PROBE_LINK_MOSI_GPIO 1
#define TB_PROBE_LINK_MISO_GPIO 8
#define TB_PROBE_LINK_CS_GPIO 9
#define TB_PROBE_LINK_IRQ_GPIO 24
#define TB_PROBE_LINK_MAX_TIMEOUT_MS 1000u

enum tb_probe_link_profile
{
    TB_PROBE_LINK_PROFILE_DISABLED,
    TB_PROBE_LINK_PROFILE_WT_TINY_P4_C5_SPI_V1
};

struct tb_probe_link_config
{
    bool enabled;
    enum tb_probe_link_profile profile;
};

/* A disabled configuration leaves SPI and all link pins untouched. */
esp_err_t tb_probe_link_init(const struct tb_probe_link_config *config);
esp_err_t tb_probe_link_stop(void);
bool tb_probe_link_active(void);
enum tb_probe_link_profile tb_probe_link_get_profile(void);
bool tb_probe_link_uses_gpio(int gpio);

/*
 * Send one fixed-size response and receive one fixed-size request. The caller owns
 * request_payload; frame data points at that buffer only after a successful call.
 * A NULL response sends a zero-filled first response.
 */
enum tb_status tb_probe_link_xfer(const struct tb_target_frame *response, struct tb_target_frame *request,
                                  uint8_t request_payload[TB_TARGET_PAYLOAD_MAX], uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
