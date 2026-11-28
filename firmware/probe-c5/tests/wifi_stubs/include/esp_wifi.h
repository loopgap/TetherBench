#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define ESP_ERR_WIFI_CONN (-2)

typedef enum
{
    WIFI_PHY_MODE_HT20 = 3,
    WIFI_PHY_MODE_HE20 = 6
} wifi_phy_mode_t;
typedef int wifi_second_chan_t;
typedef int wifi_interface_t;
typedef int wifi_storage_t;
typedef int wifi_mode_t;
typedef int wifi_band_mode_t;
typedef int wifi_auth_mode_t;
typedef int wifi_scan_type_t;
typedef int wifi_ps_type_t;

enum
{
    WIFI_IF_STA,
    WIFI_IF_AP,
    WIFI_STORAGE_RAM,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_BAND_MODE_AUTO,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_ALL_CHANNEL_SCAN,
    WIFI_CONNECT_AP_BY_SIGNAL,
    WIFI_SCAN_TYPE_ACTIVE,
    WIFI_BW20,
    WIFI_PS_NONE,
    WIFI_SECOND_CHAN_NONE
};

#define WIFI_PROTOCOL_11A (1u << 0)
#define WIFI_PROTOCOL_11B (1u << 1)
#define WIFI_PROTOCOL_11G (1u << 2)
#define WIFI_PROTOCOL_11N (1u << 3)
#define WIFI_PROTOCOL_11AX (1u << 4)

typedef struct
{
    uint32_t placeholder;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})

typedef struct
{
    struct
    {
        uint8_t ssid[32];
        uint8_t password[64];
        uint8_t ssid_len;
        uint8_t channel;
        uint8_t max_connection;
        wifi_auth_mode_t authmode;
        struct
        {
            bool required;
        } pmf_cfg;
    } ap;
    struct
    {
        uint8_t ssid[32];
        uint8_t password[64];
        struct
        {
            wifi_auth_mode_t authmode;
        } threshold;
        int scan_method;
        int sort_method;
        uint8_t failure_retry_cnt;
        uint8_t channel;
    } sta;
} wifi_config_t;

typedef struct
{
    uint32_t ghz_2g;
    uint32_t ghz_5g;
} wifi_protocols_t;
typedef struct
{
    int ghz_2g;
    int ghz_5g;
} wifi_bandwidths_t;
typedef struct
{
    uint16_t min;
    uint16_t max;
} wifi_scan_active_time_t;
typedef struct
{
    const uint8_t *ssid;
    bool show_hidden;
    wifi_scan_type_t scan_type;
    union
    {
        struct
        {
            wifi_scan_active_time_t active;
        } scan_time;
    };
    uint16_t home_chan_dwell_time;
} wifi_scan_config_t;
typedef struct
{
    uint8_t bssid[6];
    uint8_t primary;
    bool phy_11ax;
    wifi_auth_mode_t authmode;
    int8_t rssi;
} wifi_ap_record_t;
typedef struct
{
    uint8_t mac[6];
    bool phy_11ax;
    int8_t rssi;
} wifi_sta_info_t;
typedef struct
{
    int num;
    wifi_sta_info_t sta[4];
} wifi_sta_list_t;

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t mode);
esp_err_t esp_wifi_set_protocols(wifi_interface_t interface, const wifi_protocols_t *protocols);
esp_err_t esp_wifi_set_bandwidths(wifi_interface_t interface, const wifi_bandwidths_t *bandwidths);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *mode);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *stations);
esp_err_t esp_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid);
esp_err_t esp_wifi_deauth_sta(uint16_t aid);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t secondary);
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *config);
