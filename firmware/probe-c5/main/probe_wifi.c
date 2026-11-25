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

static bool ready, scanning, scanned;
static portMUX_TYPE state_lock=portMUX_INITIALIZER_UNLOCKED;
static uint64_t retry_at, scan_at;
static unsigned retry_ms=1000;
static char ip[16], identity[24];
static esp_netif_t *netif;

static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&state_lock);
        ready=false; ip[0]=0;
        retry_at=(uint64_t)esp_timer_get_time()/1000+retry_ms;
        unsigned wait=retry_ms;
        if (retry_ms<30000) retry_ms=retry_ms*2>30000?30000:retry_ms*2;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGW("wifi","Disconnected (reason=%u); retry after %u ms",((wifi_event_sta_disconnected_t *)data)->reason,wait);
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        wifi_phy_mode_t mode;
        if (esp_wifi_sta_get_negotiated_phymode(&mode)!=ESP_OK || mode!=WIFI_PHY_MODE_HE20) {
            ESP_LOGE("wifi","Access point did not negotiate Wi-Fi 6; connection rejected");
            portENTER_CRITICAL(&state_lock); retry_ms=30000; portEXIT_CRITICAL(&state_lock);
            esp_wifi_disconnect(); return;
        }
        ip_event_got_ip_t *got=data;
        char address[16]; snprintf(address,sizeof address,IPSTR,IP2STR(&got->ip_info.ip));
        portENTER_CRITICAL(&state_lock);
        memcpy(ip,address,sizeof ip);
        ready=true; retry_ms=1000; retry_at=0;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGI("wifi","%s connected, IP=%s, PHY=HE20",identity,address);
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {
        portENTER_CRITICAL(&state_lock);
        scanning=false; scanned=true;
        portEXIT_CRITICAL(&state_lock);
    } else if (TB_WIFI_AP && base==WIFI_EVENT && id==WIFI_EVENT_AP_STACONNECTED) {
        wifi_sta_list_t stations;
        if (esp_wifi_ap_get_sta_list(&stations)==ESP_OK) {
            for (unsigned i=0;i<stations.num;i++) {
                if (stations.sta[i].phy_11ax) continue;
                uint16_t aid=0;
                if (esp_wifi_ap_get_sta_aid(stations.sta[i].mac,&aid)==ESP_OK) esp_wifi_deauth_sta(aid);
                ESP_LOGW("wifi","Rejected station without Wi-Fi 6 support");
            }
        }
    }
}
esp_err_t tb_wifi_init(void) {
    esp_err_t err=esp_netif_init(); if (err!=ESP_OK) return err;
    err=esp_event_loop_create_default(); if (err!=ESP_OK) return err;
    netif=TB_WIFI_AP?esp_netif_create_default_wifi_ap():esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    if (TB_WIFI_AP && !esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init); if (err!=ESP_OK) return err;
    err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL); if (err!=ESP_OK) return err;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL); if (err!=ESP_OK) return err;
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(identity,sizeof identity,"c5-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    esp_netif_set_hostname(netif,identity);
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err!=ESP_OK) return err;
    err=esp_wifi_set_mode(TB_WIFI_AP?WIFI_MODE_APSTA:WIFI_MODE_STA); if (err!=ESP_OK) return err;
    wifi_config_t config={0};
    if (TB_WIFI_AP) {
        memcpy(config.ap.ssid,TB_WIFI_SSID,sizeof TB_WIFI_SSID-1);
        memcpy(config.ap.password,TB_WIFI_PASSWORD,sizeof TB_WIFI_PASSWORD-1);
        config.ap.ssid_len=sizeof TB_WIFI_SSID-1; config.ap.channel=6;
        config.ap.max_connection=4; config.ap.authmode=WIFI_AUTH_WPA2_WPA3_PSK;
        config.ap.pmf_cfg.required=true;
        err=esp_wifi_set_config(WIFI_IF_AP,&config);
    } else {
        memcpy(config.sta.ssid,TB_WIFI_SSID,sizeof TB_WIFI_SSID-1);
        memcpy(config.sta.password,TB_WIFI_PASSWORD,sizeof TB_WIFI_PASSWORD-1);
        config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        config.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
        config.sta.failure_retry_cnt=2;
        err=esp_wifi_set_config(WIFI_IF_STA,&config);
    }
    if (err!=ESP_OK) return err;
    err=esp_wifi_start(); if (err!=ESP_OK) return err;
    // C5 band-mode configuration requires a started Wi-Fi driver.
    err=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO); if (err!=ESP_OK) return err;
    wifi_protocols_t protocols={.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AX,
        .ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AX};
    err=esp_wifi_set_protocols(WIFI_IF_STA,&protocols); if (err!=ESP_OK) return err;
    if (TB_WIFI_AP) { err=esp_wifi_set_protocols(WIFI_IF_AP,&protocols); if (err!=ESP_OK) return err; }
    err=esp_wifi_set_ps(WIFI_PS_NONE); if (err!=ESP_OK) return err;
    if (TB_WIFI_AP) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif,&info)!=ESP_OK) return ESP_FAIL;
        snprintf(ip,sizeof ip,IPSTR,IP2STR(&info.ip)); ready=true;
        ESP_LOGI("wifi","%s AP started, IP=%s",identity,ip);
    } else err=esp_wifi_connect();
    return err;
}
void tb_wifi_tick(uint64_t now) {
    portENTER_CRITICAL(&state_lock);
    bool retry=!TB_WIFI_AP && !ready && retry_at && now>=retry_at && !scanning;
    if (retry) retry_at=now+30000;
    portEXIT_CRITICAL(&state_lock);
    if (retry) esp_wifi_connect();
}
bool tb_wifi_ready(void) {
    portENTER_CRITICAL(&state_lock); bool value=ready; portEXIT_CRITICAL(&state_lock); return value;
}
bool tb_wifi_is_ap(void) { return TB_WIFI_AP; }
void tb_wifi_ip(char address[16]) {
    portENTER_CRITICAL(&state_lock); memcpy(address,ip,16); portEXIT_CRITICAL(&state_lock);
}
const char *tb_wifi_id(void) { return identity; }
uint8_t tb_wifi_channel(void) { uint8_t channel=0; wifi_second_chan_t secondary; esp_wifi_get_channel(&channel,&secondary); return channel; }
int tb_wifi_rssi(void) { wifi_ap_record_t ap; return esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:-127; }
int tb_wifi_phy(void) { wifi_phy_mode_t mode; return esp_wifi_sta_get_negotiated_phymode(&mode)==ESP_OK?(int)mode:-1; }
enum tb_status tb_wifi_scan(uint8_t *out, size_t *size, uint64_t now) {
    portENTER_CRITICAL(&state_lock);
    bool busy=scanning, fresh=scanned && now-scan_at<=30000;
    portEXIT_CRITICAL(&state_lock);
    if (busy) return TB_BUSY;
    if (!fresh) {
        wifi_scan_config_t config={.ssid=(uint8_t *)TB_WIFI_SSID,.show_hidden=false,
            .scan_type=WIFI_SCAN_TYPE_ACTIVE,.scan_time.active={.min=20,.max=50},.home_chan_dwell_time=30};
        portENTER_CRITICAL(&state_lock); scanning=true; scanned=false; scan_at=now; portEXIT_CRITICAL(&state_lock);
        if (esp_wifi_scan_start(&config,false)!=ESP_OK) {
            portENTER_CRITICAL(&state_lock); scanning=false; portEXIT_CRITICAL(&state_lock); return TB_IO;
        }
        return TB_BUSY;
    }
    wifi_ap_record_t records[16]; uint16_t count=16;
    if (esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK) return TB_IO;
    *size=4; tb_put32(out,count);
    for (unsigned i=0;i<count;i++) {
        uint8_t *p=out+*size;
        memcpy(p,records[i].bssid,6); p[6]=records[i].primary; p[7]=(uint8_t)records[i].rssi;
        p[8]=records[i].phy_11ax; p[9]=(uint8_t)records[i].authmode;
        memset(p+10,0,2); *size+=12;
    }
    // The driver frees its records after retrieval; the next call starts a fresh bounded scan.
    portENTER_CRITICAL(&state_lock); scanned=false; portEXIT_CRITICAL(&state_lock);
    return TB_OK;
}
enum tb_status tb_wifi_switch(uint8_t channel) {
    if (TB_WIFI_AP) {
        return esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)==ESP_OK?TB_OK:TB_IO;
    }
    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA,&config)!=ESP_OK) return TB_IO;
    config.sta.channel=channel;
    if (esp_wifi_disconnect()!=ESP_OK || esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA,&config)!=ESP_OK || esp_wifi_connect()!=ESP_OK) return TB_IO;
    return TB_OK;
}
