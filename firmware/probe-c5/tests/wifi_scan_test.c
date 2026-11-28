#include "esp_event.h"
#include "esp_wifi.h"
#include "probe_wifi.h"
#include <assert.h>
#include <string.h>

#include "../components/probe_wifi/probe_wifi.c"

const int wifi_event_base = 1;
const int ip_event_base = 2;

struct esp_netif
{
    int unused;
};
static struct esp_netif test_netif;
static unsigned disconnect_calls, connect_calls;
static esp_err_t connect_result;

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}
esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}
esp_err_t esp_event_handler_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t handler,
                                     void *event_handler_arg)
{
    (void)event_base;
    (void)event_id;
    (void)handler;
    (void)event_handler_arg;
    return ESP_OK;
}
esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    return &test_netif;
}
esp_netif_t *esp_netif_create_default_wifi_ap(void)
{
    return &test_netif;
}
esp_err_t esp_netif_set_hostname(esp_netif_t *netif, const char *hostname)
{
    (void)netif;
    (void)hostname;
    return ESP_OK;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    (void)netif;
    memset(info, 0, sizeof *info);
    return ESP_OK;
}
esp_err_t esp_read_mac(uint8_t mac[6], int type)
{
    (void)type;
    memset(mac, 0, 6);
    return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    (void)config;
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(wifi_storage_t storage)
{
    (void)storage;
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
    (void)mode;
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *config)
{
    (void)interface;
    (void)config;
    return ESP_OK;
}
esp_err_t esp_wifi_start(void)
{
    return ESP_OK;
}
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t mode)
{
    (void)mode;
    return ESP_OK;
}
esp_err_t esp_wifi_set_protocols(wifi_interface_t interface, const wifi_protocols_t *protocols)
{
    (void)interface;
    (void)protocols;
    return ESP_OK;
}
esp_err_t esp_wifi_set_bandwidths(wifi_interface_t interface, const wifi_bandwidths_t *bandwidths)
{
    (void)interface;
    (void)bandwidths;
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type)
{
    (void)type;
    return ESP_OK;
}
esp_err_t esp_wifi_connect(void)
{
    connect_calls++;
    return connect_result;
}
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary)
{
    *primary = 1;
    *secondary = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *record)
{
    memset(record, 0, sizeof *record);
    return ESP_FAIL;
}
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t secondary)
{
    (void)primary;
    (void)secondary;
    return ESP_OK;
}
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *config)
{
    (void)interface;
    memset(config, 0, sizeof *config);
    return ESP_OK;
}

static bool ap_list_allocated;
static unsigned scan_starts, scan_stops, record_reads, list_clears;
static esp_err_t start_result, stop_result, records_result, clear_result;
static esp_err_t phymode_result;
static wifi_phy_mode_t phymode_value;
static uint32_t event_status;
static uint64_t timer_us;

static void reset_test_state(void)
{
    ready = scanning = scan_done = scan_failed = scanned = associated = false;
    associated_at = 0;
    retry_at = scan_at = scan_started_at = 0;
    retry_ms = 1000;
    scan_round = 0;
    scan_count = 0;
    memset(scan_records, 0, sizeof scan_records);
    ap_list_allocated = false;
    scan_starts = scan_stops = record_reads = list_clears = 0;
    disconnect_calls = connect_calls = 0;
    connect_result = ESP_OK;
    start_result = stop_result = records_result = clear_result = ESP_OK;
    phymode_result = ESP_FAIL;
    phymode_value = WIFI_PHY_MODE_HT20;
    event_status = 0;
    timer_us = 0;
}

static void finish_scan(uint32_t status, uint64_t now)
{
    event_status = status;
    wifi_event_sta_scan_done_t event_data = {.status = status};
    event(NULL, WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &event_data);
    tb_wifi_tick(now);
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    (void)config;
    assert(!block);
    scan_starts++;
    if (start_result == ESP_OK)
        ap_list_allocated = true;
    return start_result;
}

esp_err_t esp_wifi_scan_stop(void)
{
    scan_stops++;
    return stop_result;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *records)
{
    record_reads++;
    if (records_result != ESP_OK)
        return records_result;
    assert(ap_list_allocated);
    assert(*number > 0);
    *number = 1;
    memset(records, 0, sizeof *records);
    records[0].bssid[5] = 1;
    records[0].primary = 6;
    records[0].phy_11ax = true;
    records[0].rssi = -42;
    ap_list_allocated = false;
    return ESP_OK;
}

esp_err_t esp_wifi_clear_ap_list(void)
{
    list_clears++;
    if (clear_result == ESP_OK)
        ap_list_allocated = false;
    return clear_result;
}

uint64_t esp_timer_get_time(void)
{
    return timer_us;
}

esp_err_t esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *mode)
{
    if (phymode_result == ESP_OK)
        *mode = phymode_value;
    return phymode_result;
}

esp_err_t esp_wifi_disconnect(void)
{
    disconnect_calls++;
    return ESP_OK;
}

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *stations)
{
    (void)stations;
    return ESP_FAIL;
}

esp_err_t esp_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid)
{
    (void)mac;
    (void)aid;
    return ESP_FAIL;
}

esp_err_t esp_wifi_deauth_sta(uint16_t aid)
{
    (void)aid;
    return ESP_OK;
}

static void test_get_records_failure_clears_list(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    assert(tb_wifi_scan(output, &size, 100) == TB_BUSY);
    records_result = ESP_FAIL;
    finish_scan(0, 101);
    assert(record_reads == 1);
    assert(list_clears == 1);
    assert(!ap_list_allocated);
}

static void test_failed_event_clears_without_reading_records(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    assert(tb_wifi_scan(output, &size, 100) == TB_BUSY);
    finish_scan(1, 101);
    assert(record_reads == 0);
    assert(list_clears == 1);
    assert(!ap_list_allocated);
    assert(scan_count == 0);
}

static void test_timeout_stop_clears_list(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    assert(tb_wifi_scan(output, &size, 100) == TB_BUSY);
    tb_wifi_tick(5101);
    assert(scan_stops == 1);
    assert(list_clears == 1);
    assert(!ap_list_allocated);
}

static void test_timeout_stop_failure_preserves_scan(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    assert(tb_wifi_scan(output, &size, 100) == TB_BUSY);
    stop_result = ESP_FAIL;
    tb_wifi_tick(5101);
    assert(scan_stops == 1);
    assert(list_clears == 0);
    assert(ap_list_allocated);
    finish_scan(0, 5102);
    assert(record_reads == 1);
    assert(scan_starts == 2 && ap_list_allocated);
}

static void test_start_failure_does_not_clear_unallocated_list(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    start_result = ESP_FAIL;
    assert(tb_wifi_scan(output, &size, 100) == TB_IO);
    assert(scan_starts == 1);
    assert(record_reads == 0);
    assert(list_clears == 0);
    assert(!ap_list_allocated);
}

static void test_five_successful_rounds_do_not_clear_twice(void)
{
    reset_test_state();
    uint8_t output[512];
    size_t size = 0;
    assert(tb_wifi_scan(output, &size, 100) == TB_BUSY);
    for (uint64_t round = 0; round < 5; round++)
        finish_scan(0, 101 + round);
    assert(scan_starts == 5);
    assert(record_reads == 5);
    assert(list_clears == 0);
    assert(!ap_list_allocated);
    assert(tb_wifi_scan(output, &size, 110) == TB_OK);
    assert(size == 20);
    assert(output[0] == 1);
    assert(list_clears == 0);
}

static void test_non_he20_link_is_rejected_before_dhcp(void)
{
    reset_test_state();
    timer_us = 1000000;
    phymode_result = ESP_OK;
    phymode_value = WIFI_PHY_MODE_HT20;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    assert(associated);
    assert(!ready);
    assert(disconnect_calls == 1);

    wifi_event_sta_disconnected_t disconnected = {.reason = 201};
    timer_us = 2000000;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    assert(!associated);
    assert(retry_at == 32000);
    tb_wifi_tick(31999);
    assert(connect_calls == 0);
    tb_wifi_tick(32000);
    assert(connect_calls == 1);
    assert(retry_at == 0);
    tb_wifi_tick(62000);
    assert(connect_calls == 1);

    timer_us = 62000000;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    assert(retry_at == 92000);
    tb_wifi_tick(92000);
    assert(connect_calls == 2);
    assert(retry_at == 0);
}

static void test_associated_sta_without_dhcp_retries_after_deadline(void)
{
    reset_test_state();
    timer_us = 1000000;
    phymode_result = ESP_OK;
    phymode_value = WIFI_PHY_MODE_HE20;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    assert(associated && !ready);
    assert(disconnect_calls == 0);

    tb_wifi_tick(10999);
    assert(disconnect_calls == 0);
    tb_wifi_tick(11000);
    assert(!associated && !ready);
    assert(disconnect_calls == 1);
    assert(retry_at == 12000);

    wifi_event_sta_disconnected_t disconnected = {.reason = 201};
    timer_us = 11000000;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    tb_wifi_tick(11999);
    assert(connect_calls == 0);
    tb_wifi_tick(12000);
    assert(connect_calls == 1);
    assert(retry_at == 0);
    assert(retry_ms == 2000);
    tb_wifi_tick(13999);
    assert(connect_calls == 1);
    timer_us = 12000000;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnected);
    assert(retry_at == 14000);
    assert(retry_ms == 4000);
    tb_wifi_tick(14000);
    assert(connect_calls == 2);
    assert(retry_at == 0);
    tb_wifi_tick(18000);
    assert(connect_calls == 2);
}

static void test_immediate_connect_error_uses_backoff(void)
{
    reset_test_state();
    retry_at = 1000;
    connect_result = ESP_FAIL;
    tb_wifi_tick(1000);
    assert(connect_calls == 1);
    assert(retry_at == 2000);
    assert(retry_ms == 2000);
    tb_wifi_tick(1999);
    assert(connect_calls == 1);
    connect_result = ESP_OK;
    tb_wifi_tick(2000);
    assert(connect_calls == 2);
    assert(retry_at == 0);
    tb_wifi_tick(4000);
    assert(connect_calls == 2);
}

static void test_connect_in_progress_does_not_repeat_request(void)
{
    reset_test_state();
    retry_at = 1000;
    connect_result = ESP_ERR_WIFI_CONN;
    tb_wifi_tick(1000);
    assert(connect_calls == 1);
    assert(retry_at == 0);
    tb_wifi_tick(31000);
    assert(connect_calls == 1);
}

static void test_he20_ip_cancels_association_timeout(void)
{
    reset_test_state();
    timer_us = 1000000;
    phymode_result = ESP_OK;
    phymode_value = WIFI_PHY_MODE_HE20;
    event(NULL, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    ip_event_got_ip_t got_ip = {.ip_info = {.ip = {.addr = 0x0100007f}}};
    timer_us = 3000000;
    event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip);
    assert(ready);
    tb_wifi_tick(11000);
    assert(ready);
    assert(disconnect_calls == 0);
}

int main(void)
{
    test_get_records_failure_clears_list();
    test_failed_event_clears_without_reading_records();
    test_timeout_stop_clears_list();
    test_timeout_stop_failure_preserves_scan();
    test_start_failure_does_not_clear_unallocated_list();
    test_five_successful_rounds_do_not_clear_twice();
    test_non_he20_link_is_rejected_before_dhcp();
    test_associated_sta_without_dhcp_retries_after_deadline();
    test_immediate_connect_error_uses_backoff();
    test_connect_in_progress_does_not_repeat_request();
    test_he20_ip_cancels_association_timeout();
    return 0;
}
