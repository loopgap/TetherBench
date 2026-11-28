#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef const void *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);

extern const int wifi_event_base;
extern const int ip_event_base;
#define WIFI_EVENT (&wifi_event_base)
#define IP_EVENT (&ip_event_base)
#define ESP_EVENT_ANY_ID (-1)
#define WIFI_EVENT_SCAN_DONE 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_AP_STACONNECTED 3
#define WIFI_EVENT_STA_CONNECTED 4
#define IP_EVENT_STA_GOT_IP 4

typedef struct
{
    uint8_t reason;
} wifi_event_sta_disconnected_t;

typedef struct
{
    uint32_t status;
} wifi_event_sta_scan_done_t;

typedef struct
{
    struct
    {
        struct
        {
            uint32_t addr;
        } ip;
    } ip_info;
} ip_event_got_ip_t;

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t handler,
                                     void *event_handler_arg);
