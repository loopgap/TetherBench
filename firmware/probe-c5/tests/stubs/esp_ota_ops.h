#pragma once
#include "esp_partition.h"
#include "esp_app_desc.h"
typedef unsigned esp_ota_handle_t;
typedef int esp_ota_img_states_t;
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define OTA_WITH_SEQUENTIAL_WRITES ((size_t)-1)
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *state);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p);
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_resume(const esp_partition_t *p, size_t erase, size_t offset, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *app);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
