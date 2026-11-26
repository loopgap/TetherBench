#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct
{
    uint32_t address, size;
} esp_partition_t;
typedef const esp_partition_t *esp_partition_iterator_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_ANY 255
const esp_partition_t *esp_ota_get_running_partition(void);
esp_partition_iterator_t esp_partition_find(int type, int subtype, const char *name);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it);
void esp_partition_iterator_release(esp_partition_iterator_t it);
esp_err_t esp_partition_read(const esp_partition_t *p, size_t offset, void *out, size_t size);
