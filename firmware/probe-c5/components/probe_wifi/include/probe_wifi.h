#pragma once
#include "probe_core.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tb_wifi_init(void);
void tb_wifi_tick(uint64_t now);
bool tb_wifi_ready(void);
bool tb_wifi_is_ap(void);
uint8_t tb_wifi_channel(void);
int tb_wifi_rssi(void);
int tb_wifi_phy(void);
bool tb_wifi_he20(void);
void tb_wifi_ip(char address[16]);
const char *tb_wifi_id(void);
enum tb_status tb_wifi_scan(uint8_t *out, size_t *size, uint64_t now);
enum tb_status tb_wifi_switch(uint8_t channel);

#ifdef __cplusplus
}
#endif
