#include "esp_log.h"
#include "probe.h"

extern "C" void app_main(void)
{
    probe_init();
    ESP_LOGI("tetherbench", "Probe C5 services started; use authenticated control to assign target pins.");
}
