#pragma once

#include "esp_err.h"
#include "esp_event.h"

typedef struct esp_netif esp_netif_t;
typedef struct
{
    uint32_t addr;
} esp_ip4_addr_t;
typedef struct
{
    esp_ip4_addr_t ip;
} esp_netif_ip_info_t;

#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) \
    (unsigned)(((ip)->addr) & 0xffu), (unsigned)((((ip)->addr) >> 8) & 0xffu), \
        (unsigned)((((ip)->addr) >> 16) & 0xffu), (unsigned)((((ip)->addr) >> 24) & 0xffu)

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
