#include "esp_log.h"
#include "probe.h"

extern "C" void app_main(void)
{
    probe_init();
    ESP_LOGI("tetherbench", "Probe C5 starter; no hardware services are configured.");
}
