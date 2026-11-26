#pragma once
#include "probe_core.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tb_ota_phase
{
    TB_OTA_EMPTY,
    TB_OTA_RECEIVING,
    TB_OTA_VERIFIED,
    TB_OTA_ACTIVATED,
    TB_OTA_CONFIRMED,
    TB_OTA_FAILED
};
esp_err_t tb_ota_init(void);
bool tb_ota_busy(void);
bool tb_ota_pending_boot(void);
enum tb_status tb_ota_begin(const uint8_t *manifest, size_t size);
enum tb_status tb_ota_write(const uint8_t *data, size_t size);
enum tb_status tb_ota_finish(uint64_t id);
enum tb_status tb_ota_activate(uint64_t id);
enum tb_status tb_ota_abort(uint64_t id);
enum tb_status tb_ota_confirm(void);
size_t tb_ota_status(uint8_t *out);
// Checks the signed manifest structure; target identity and capabilities require a HELLO exchange.
enum tb_status tb_ota_validate_target_manifest(const uint8_t *manifest, size_t size);

#ifdef __cplusplus
}
#endif
