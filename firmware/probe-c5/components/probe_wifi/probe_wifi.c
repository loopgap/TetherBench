#include "probe_wifi.h"
#include "device_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static bool ready, scanning, scan_done, scan_failed, scanned;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t retry_at, scan_at, scan_started_at;
static unsigned retry_ms = 1000;
static uint8_t scan_round;
static size_t scan_count;
static struct tb_scan_ap scan_records[16];
static char ip[16], identity[24];
static esp_netif_t *netif;

static void clear_scan_list(const char *reason)
{
    esp_err_t err = esp_wifi_clear_ap_list();
    if (err != ESP_OK)
        ESP_LOGW("wifi", "%s AP list cleanup failed (%d)", reason, (int)err);
}

static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&state_lock);
        ready = false;
        ip[0] = 0;
        retry_at = (uint64_t)esp_timer_get_time() / 1000 + retry_ms;
        unsigned wait = retry_ms;
        if (retry_ms < 30000)
            retry_ms = retry_ms * 2 > 30000 ? 30000 : retry_ms * 2;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGW("wifi", "Disconnected (reason=%u); retry after %u ms", ((wifi_event_sta_disconnected_t *)data)->reason,
                 wait);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_phy_mode_t mode;
        if (esp_wifi_sta_get_negotiated_phymode(&mode) != ESP_OK || mode != WIFI_PHY_MODE_HE20) {
            ESP_LOGE("wifi", "Access point did not negotiate Wi-Fi 6; connection rejected");
            portENTER_CRITICAL(&state_lock);
            retry_ms = 30000;
            portEXIT_CRITICAL(&state_lock);
            esp_wifi_disconnect();
            return;
        }
        ip_event_got_ip_t *got = data;
        char address[16];
        snprintf(address, sizeof address, IPSTR, IP2STR(&got->ip_info.ip));
        portENTER_CRITICAL(&state_lock);
        memcpy(ip, address, sizeof ip);
        ready = true;
        retry_ms = 1000;
        retry_at = 0;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGI("wifi", "%s connected, IP=%s, PHY=HE20", identity, address);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        portENTER_CRITICAL(&state_lock);
        scan_done = true;
        scan_failed = !data || ((wifi_event_sta_scan_done_t *)data)->status != 0;
        portEXIT_CRITICAL(&state_lock);
    } else if (TB_WIFI_AP && base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_sta_list_t stations;
        if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) {
            for (unsigned i = 0; i < stations.num; i++) {
                if (stations.sta[i].phy_11ax)
                    continue;
                uint16_t aid = 0;
                if (esp_wifi_ap_get_sta_aid(stations.sta[i].mac, &aid) == ESP_OK)
                    esp_wifi_deauth_sta(aid);
                ESP_LOGW("wifi", "Rejected station without Wi-Fi 6 support");
            }
        }
    }
}
esp_err_t tb_wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK)
        return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
        return err;
    netif = TB_WIFI_AP ? esp_netif_create_default_wifi_ap() : esp_netif_create_default_wifi_sta();
    if (!netif)
        return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK)
        return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL);
    if (err != ESP_OK)
        return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL);
    if (err != ESP_OK)
        return err;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(identity, sizeof identity, "c5-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    esp_netif_set_hostname(netif, identity);
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK)
        return err;
    err = esp_wifi_set_mode(TB_WIFI_AP ? WIFI_MODE_AP : WIFI_MODE_STA);
    if (err != ESP_OK)
        return err;
    wifi_config_t config = {0};
    if (TB_WIFI_AP) {
        memcpy(config.ap.ssid, TB_WIFI_SSID, sizeof TB_WIFI_SSID - 1);
        memcpy(config.ap.password, TB_WIFI_PASSWORD, sizeof TB_WIFI_PASSWORD - 1);
        config.ap.ssid_len = sizeof TB_WIFI_SSID - 1;
        config.ap.channel = 6;
        config.ap.max_connection = 4;
        config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
        config.ap.pmf_cfg.required = true;
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
    } else {
        memcpy(config.sta.ssid, TB_WIFI_SSID, sizeof TB_WIFI_SSID - 1);
        memcpy(config.sta.password, TB_WIFI_PASSWORD, sizeof TB_WIFI_PASSWORD - 1);
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        config.sta.failure_retry_cnt = 2;
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (err != ESP_OK)
        return err;
    err = esp_wifi_start();
    if (err != ESP_OK)
        return err;
    // C5 band-mode configuration requires a started Wi-Fi driver.
    err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    if (err != ESP_OK)
        return err;
    wifi_protocols_t protocols = {.ghz_2g =
                                      WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX,
                                  .ghz_5g = WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX};
    err = esp_wifi_set_protocols(TB_WIFI_AP ? WIFI_IF_AP : WIFI_IF_STA, &protocols);
    if (err != ESP_OK)
        return err;
    wifi_bandwidths_t bandwidths = {.ghz_2g = WIFI_BW20, .ghz_5g = WIFI_BW20};
    err = esp_wifi_set_bandwidths(TB_WIFI_AP ? WIFI_IF_AP : WIFI_IF_STA, &bandwidths);
    if (err != ESP_OK)
        return err;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK)
        return err;
    if (TB_WIFI_AP) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) != ESP_OK)
            return ESP_FAIL;
        snprintf(ip, sizeof ip, IPSTR, IP2STR(&info.ip));
        ready = true;
        ESP_LOGI("wifi", "%s AP started, IP=%s", identity, ip);
    } else
        err = esp_wifi_connect();
    return err;
}
static esp_err_t start_scan(void)
{
    wifi_scan_config_t config = {.ssid = TB_WIFI_AP ? NULL : (uint8_t *)TB_WIFI_SSID,
                                 .show_hidden = false,
                                 .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                 .scan_time.active = {.min = 20, .max = 50},
                                 .home_chan_dwell_time = 30};
    return esp_wifi_scan_start(&config, false);
}
void tb_wifi_tick(uint64_t now)
{
    portENTER_CRITICAL(&state_lock);
    bool retry = !TB_WIFI_AP && !ready && retry_at && now >= retry_at && !scanning;
    if (retry)
        retry_at = now + 30000;
    bool timed_out = scanning && !scan_done && now >= scan_started_at && now - scan_started_at > 5000;
    if (timed_out)
        scan_started_at = now;
    bool completed = scanning && scan_done;
    bool failed = completed && scan_failed;
    if (completed) {
        scan_done = false;
        scan_failed = false;
    }
    portEXIT_CRITICAL(&state_lock);
    if (timed_out) {
        esp_err_t err = esp_wifi_scan_stop();
        if (err == ESP_OK) {
            clear_scan_list("Timed-out scan");
            portENTER_CRITICAL(&state_lock);
            scanning = false;
            scanned = false;
            scan_done = false;
            scan_failed = false;
            portEXIT_CRITICAL(&state_lock);
            ESP_LOGW("wifi", "SSID scan timed out and stopped");
        } else
            ESP_LOGW("wifi", "SSID scan timeout stop failed (%d)", (int)err);
    }
    if (completed) {
        if (failed) {
            clear_scan_list("Failed scan");
            portENTER_CRITICAL(&state_lock);
            scanning = false;
            scanned = false;
            portEXIT_CRITICAL(&state_lock);
        } else {
            wifi_ap_record_t found[16];
            uint16_t count = 16;
            if (esp_wifi_scan_get_ap_records(&count, found) != ESP_OK) {
                clear_scan_list("Failed result retrieval");
                portENTER_CRITICAL(&state_lock);
                scanning = false;
                scanned = false;
                portEXIT_CRITICAL(&state_lock);
            } else {
                for (unsigned i = 0; i < count; i++) {
                    struct tb_scan_ap sample = {.channel = found[i].primary,
                                                .wifi6 = found[i].phy_11ax,
                                                .authmode = (uint8_t)found[i].authmode,
                                                .sightings = 1,
                                                .rssi = {(int8_t)found[i].rssi}};
                    sample.first_seen = now;
                    memcpy(sample.bssid, found[i].bssid, 6);
                    (void)tb_scan_merge(scan_records, &scan_count, 16, &sample);
                }
                portENTER_CRITICAL(&state_lock);
                scan_round++;
                bool again = scan_round < 5;
                if (!again) {
                    scanning = false;
                    scanned = true;
                } else
                    scan_started_at = now;
                portEXIT_CRITICAL(&state_lock);
                if (again && start_scan() != ESP_OK) {
                    portENTER_CRITICAL(&state_lock);
                    scanning = false;
                    scanned = false;
                    portEXIT_CRITICAL(&state_lock);
                }
            }
        }
    }
    if (retry)
        esp_wifi_connect();
}
bool tb_wifi_ready(void)
{
    portENTER_CRITICAL(&state_lock);
    bool value = ready;
    portEXIT_CRITICAL(&state_lock);
    return value;
}
bool tb_wifi_is_ap(void)
{
    return TB_WIFI_AP;
}
void tb_wifi_ip(char address[16])
{
    portENTER_CRITICAL(&state_lock);
    memcpy(address, ip, 16);
    portEXIT_CRITICAL(&state_lock);
}
const char *tb_wifi_id(void)
{
    return identity;
}
uint8_t tb_wifi_channel(void)
{
    uint8_t channel = 0;
    wifi_second_chan_t secondary;
    esp_wifi_get_channel(&channel, &secondary);
    return channel;
}
int tb_wifi_rssi(void)
{
    if (TB_WIFI_AP) {
        wifi_sta_list_t stations;
        if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK)
            return -127;
        int8_t samples[sizeof stations.sta / sizeof stations.sta[0]];
        if (stations.num < 0 || (size_t)stations.num > sizeof samples)
            return -127;
        for (unsigned i = 0; i < stations.num; i++)
            samples[i] = stations.sta[i].rssi;
        return tb_worst_rssi(samples, stations.num);
    }
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
}
int tb_wifi_phy(void)
{
    wifi_phy_mode_t mode;
    return esp_wifi_sta_get_negotiated_phymode(&mode) == ESP_OK ? (int)mode : -1;
}
bool tb_wifi_he20(void)
{
    if (!TB_WIFI_AP) {
        wifi_phy_mode_t mode;
        return esp_wifi_sta_get_negotiated_phymode(&mode) == ESP_OK && mode == WIFI_PHY_MODE_HE20;
    }
    wifi_sta_list_t stations;
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK || stations.num <= 0 ||
        (size_t)stations.num > sizeof stations.sta / sizeof stations.sta[0])
        return false;
    for (int i = 0; i < stations.num; i++)
        if (!stations.sta[i].phy_11ax)
            return false;
    return true;
}
enum tb_status tb_wifi_scan(uint8_t *out, size_t *size, uint64_t now)
{
    portENTER_CRITICAL(&state_lock);
    bool busy = scanning, fresh = scanned && now - scan_at <= 30000;
    portEXIT_CRITICAL(&state_lock);
    if (busy)
        return TB_BUSY;
    if (!fresh) {
        portENTER_CRITICAL(&state_lock);
        if (scanning) {
            portEXIT_CRITICAL(&state_lock);
            return TB_BUSY;
        }
        scanning = true;
        scan_done = false;
        scan_failed = false;
        scanned = false;
        scan_round = 0;
        scan_count = 0;
        scan_at = now;
        scan_started_at = now;
        portEXIT_CRITICAL(&state_lock);
        memset(scan_records, 0, sizeof scan_records);
        if (start_scan() != ESP_OK) {
            portENTER_CRITICAL(&state_lock);
            scanning = false;
            scan_done = false;
            scan_failed = false;
            portEXIT_CRITICAL(&state_lock);
            return TB_IO;
        }
        return TB_BUSY;
    }
    *size = 4;
    tb_put32(out, scan_count);
    for (unsigned i = 0; i < scan_count; i++) {
        uint8_t *p = out + *size;
        memcpy(p, scan_records[i].bssid, 6);
        p[6] = scan_records[i].channel;
        p[7] = (uint8_t)tb_scan_median_rssi(&scan_records[i]);
        p[8] = scan_records[i].wifi6;
        p[9] = scan_records[i].authmode;
        p[10] = scan_records[i].sightings;
        p[11] = 0;
        tb_put32(p + 12, (uint32_t)(now >= scan_records[i].first_seen ? now - scan_records[i].first_seen : UINT32_MAX));
        *size += 16;
    }
    portENTER_CRITICAL(&state_lock);
    scanned = false;
    portEXIT_CRITICAL(&state_lock);
    return TB_OK;
}
enum tb_status tb_wifi_switch(uint8_t channel)
{
    if (TB_WIFI_AP) {
        return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK ? TB_OK : TB_IO;
    }
    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK)
        return TB_IO;
    config.sta.channel = channel;
    if (esp_wifi_disconnect() != ESP_OK || esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK || esp_wifi_connect() != ESP_OK)
        return TB_IO;
    return TB_OK;
}
