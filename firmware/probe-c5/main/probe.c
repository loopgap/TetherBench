#include "probe.h"
#include "probe_service.h"
#include "probe_wifi.h"
#include "nvs_flash.h"
#include "esp_err.h"

void probe_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(tb_wifi_init());
    ESP_ERROR_CHECK(tb_service_start());
}
