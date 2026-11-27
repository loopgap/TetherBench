#pragma once
#include "probe_core.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum tb_status tb_target_ota_init(void);
bool tb_target_ota_busy(void);
uint8_t tb_target_ota_protocol(void);
enum tb_status tb_target_ota_info(uint8_t protocol, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_begin(uint8_t protocol, const uint8_t *data, size_t data_size, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_write(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_finish(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_activate(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_status(uint8_t protocol, uint8_t *out, size_t *size);
enum tb_status tb_target_ota_abort(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size);

#ifdef __cplusplus
}
#endif
