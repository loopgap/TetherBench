#include "probe_service.h"
#include "probe_core.h"
#include "probe_dap.h"
#include "probe_io.h"
#include "probe_ota.h"
#include "probe_target_ota.h"
#include "probe_wifi.h"
#include "device_config.h"
#include "esp_app_desc.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "psa/crypto.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern const unsigned char server_cert_start[] asm("_binary_server_crt_start");
extern const unsigned char server_cert_end[] asm("_binary_server_crt_end");
extern const unsigned char server_key_start[] asm("_binary_server_key_start");
extern const unsigned char server_key_end[] asm("_binary_server_key_end");
static struct tb_lease lease;
static struct tb_band band;
static SemaphoreHandle_t mutex;
static SemaphoreHandle_t rpc_mutex;
static uint64_t restart_at, boot_deadline;
static bool ota_available;
static uint8_t request_buffer[TB_HEADER_SIZE + TB_MAX_PAYLOAD];
static uint8_t response_buffer[TB_HEADER_SIZE + TB_MAX_PAYLOAD];
static uint8_t previous_request[TB_HEADER_SIZE + TB_MAX_PAYLOAD], previous_response[TB_HEADER_SIZE + TB_MAX_PAYLOAD];
static size_t previous_size, previous_response_size;
static uint64_t previous_at;
static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

static void reap(uint64_t now)
{
    if (lease.id && now >= lease.deadline) {
        tb_io_release();
        tb_dap_reset();
        lease = (struct tb_lease){0};
        previous_size = 0;
        if (band.stage == TB_BAND_COMMITTED || band.stage == TB_BAND_SWITCHED)
            tb_wifi_switch(band.previous);
        band.stage = TB_BAND_IDLE;
    }
}
static bool empty(const struct tb_frame *f)
{
    return f->size == 0;
}
static enum tb_status dispatch(const struct tb_frame *f, uint8_t *out, size_t *size, uint64_t now)
{
    *size = 0;
    const uint8_t *p = f->data;
    if (f->op == TB_INFO) {
        if (!empty(f))
            return TB_INVALID;
        char address[16];
        tb_wifi_ip(address);
        int written = snprintf((char *)out, TB_MAX_PAYLOAD,
                               "{\"id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\","
                               "\"protocol\":1,\"maxPayload\":%u,\"dapPacket\":%u,\"leaseActive\":%s,"
                               "\"allowedPins\":%" PRIu32 ",\"freeHeap\":%" PRIu32 ",\"minHeap\":%" PRIu32 ","
                               "\"wifi\":{\"ip\":\"%s\",\"ap\":%s,\"channel\":%u,\"rssi\":%d,\"phy\":%d},"
                               "\"otaAvailable\":%s,\"pendingBoot\":%s,\"bandStage\":%u,"
                               "\"capabilities\":[\"cmsis-dap-swd\",\"uart\",\"spi-mode0\",\"i2c\",\"signed-ota-ab\"]}",
                               tb_wifi_id(), TB_BOARD_ID, esp_app_get_description()->version, esp_get_idf_version(),
                               TB_MAX_PAYLOAD, TB_DAP_PACKET, lease.id ? "true" : "false", tb_allowed_pins(),
                               esp_get_free_heap_size(), esp_get_minimum_free_heap_size(), address,
                               tb_wifi_is_ap() ? "true" : "false", tb_wifi_channel(), tb_wifi_rssi(), tb_wifi_phy(),
                               ota_available ? "true" : "false", tb_ota_pending_boot() ? "true" : "false", band.stage);
        if (written < 0 || written >= TB_MAX_PAYLOAD)
            return TB_IO;
        *size = (size_t)written;
        return TB_OK;
    }
    if (f->op == TB_CONFIG) {
        if (!empty(f))
            return TB_INVALID;
        for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++) {
            const struct tb_pins *pins = &tb_pin_config[i];
            uint8_t *record = out + *size;
            memset(record, 0, 12);
            record[0] = (uint8_t)i;
            record[1] = pins->count;
            tb_put32(record + 4, pins->hz);
            memcpy(record + 8, pins->pin, 4);
            *size += 12;
        }
        return TB_OK;
    }
    if (f->op == TB_OTA_STATUS) {
        if (!empty(f))
            return TB_INVALID;
        *size = tb_ota_status(out);
        return ota_available ? TB_OK : TB_IO;
    }
    if (f->op == TB_ACQUIRE) {
        if (f->size != 4 || f->lease)
            return TB_INVALID;
        uint64_t id;
        do {
            esp_fill_random(&id, sizeof id);
        } while (!id);
        enum tb_status status = tb_acquire(&lease, now, id, tb_u32(p));
        if (status == TB_OK) {
            tb_put64(out, lease.id);
            tb_put32(out + 8, tb_u32(p));
            *size = 12;
        }
        return status;
    }
    if (!tb_owned(&lease, now, f->lease))
        return TB_LEASE;
    switch (f->op) {
    case TB_RENEW:
        return f->size == 4 ? tb_renew(&lease, now, f->lease, tb_u32(p)) : TB_INVALID;
    case TB_RELEASE:
        if (!empty(f))
            return TB_INVALID;
        tb_io_release();
        tb_dap_reset();
        lease = (struct tb_lease){0};
        if (band.stage == TB_BAND_COMMITTED || band.stage == TB_BAND_SWITCHED)
            tb_wifi_switch(band.previous);
        band.stage = TB_BAND_IDLE;
        return TB_OK;
    case TB_PINS: {
        if (f->size != 12 || tb_u16(p + 2) || tb_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_STATE;
        struct tb_pins pins = {.protocol = p[0], .count = p[1], .hz = tb_u32(p + 4)};
        memcpy(pins.pin, p + 8, 4);
        if (tb_target_ota_busy() && pins.protocol != tb_target_ota_protocol())
            return TB_BUSY;
        enum tb_status status = tb_io_configure(&pins);
        if (status == TB_OK && pins.protocol == TB_SWD)
            tb_dap_configure(pins.hz);
        return status;
    }
    case TB_DAP:
        if (tb_ota_busy() || tb_target_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        return tb_dap_execute(p, f->size, out, size);
    case TB_BUS:
        if (tb_ota_busy() || tb_target_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        return tb_io_bus(p, f->size, out, size);
    case TB_TARGET_INFO:
        if (f->size != 1 || (p[0] != TB_UART && p[0] != TB_SPI))
            return TB_INVALID;
        if (tb_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        for (unsigned i = TB_UART; i <= TB_SPI; i++)
            if (i != p[0] && tb_pin_config[i].count)
                return TB_BUSY;
        return tb_target_ota_info(p[0], out, size);
    case TB_TARGET_OTA_BEGIN:
        if (f->size != 1 + TB_TARGET_MANIFEST_SIZE || (p[0] != TB_UART && p[0] != TB_SPI))
            return TB_INVALID;
        if (tb_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++)
            if (i != p[0] && tb_pin_config[i].count)
                return TB_BUSY;
        return tb_target_ota_begin(p[0], p + 1, f->size - 1, out, size);
    case TB_TARGET_OTA_WRITE:
        return tb_target_ota_write(p, f->size, out, size);
    case TB_TARGET_OTA_FINISH:
        return tb_target_ota_finish(p, f->size, out, size);
    case TB_TARGET_OTA_ACTIVATE:
        return tb_target_ota_activate(p, f->size, out, size);
    case TB_TARGET_OTA_STATUS:
        if (!empty(f))
            return TB_INVALID;
        return tb_target_ota_status(out, size);
    case TB_TARGET_OTA_ABORT:
        return tb_target_ota_abort(p, f->size, out, size);
    case TB_OTA_BEGIN: {
        if (!ota_available)
            return TB_IO;
        if (tb_target_ota_busy())
            return TB_BUSY;
        if (band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++)
            if (tb_pin_config[i].count)
                return TB_BUSY;
        enum tb_status status = tb_ota_begin(p, f->size);
        *size = tb_ota_status(out);
        return status;
    }
    case TB_OTA_WRITE: {
        if (!ota_available)
            return TB_IO;
        if (tb_target_ota_busy())
            return TB_BUSY;
        enum tb_status status = tb_ota_write(p, f->size);
        *size = tb_ota_status(out);
        return status;
    }
    case TB_OTA_FINISH:
        if (!ota_available)
            return TB_IO;
        if (tb_target_ota_busy())
            return TB_BUSY;
        return f->size == 8 ? tb_ota_finish(tb_u64(p)) : TB_INVALID;
    case TB_OTA_ACTIVATE: {
        if (!ota_available || f->size != 8)
            return TB_INVALID;
        if (tb_target_ota_busy())
            return TB_BUSY;
        enum tb_status status = tb_ota_activate(tb_u64(p));
        if (status == TB_OK)
            restart_at = now + 500;
        return status;
    }
    case TB_OTA_ABORT:
        if (!ota_available)
            return TB_IO;
        if (tb_target_ota_busy())
            return TB_BUSY;
        return f->size == 8 ? tb_ota_abort(tb_u64(p)) : TB_INVALID;
    case TB_BOOT_CONFIRM:
        if (!empty(f))
            return TB_INVALID;
        if (tb_target_ota_busy())
            return TB_BUSY;
        if (!ota_available || !tb_wifi_ready())
            return TB_STATE;
        return tb_ota_confirm();
    case TB_WIFI_SCAN:
        if (!empty(f))
            return TB_INVALID;
        if (tb_ota_busy() || tb_target_ota_busy() || band.stage != TB_BAND_IDLE)
            return TB_BUSY;
        return tb_wifi_scan(out, size, now);
    case TB_BAND_PREPARE: {
        if (f->size != 60 || tb_u16(p + 10) || p[9] != tb_wifi_channel())
            return TB_INVALID;
        if (tb_ota_busy() || tb_target_ota_busy())
            return TB_BUSY;
        for (unsigned i = 1; i < TB_PROTOCOL_COUNT; i++)
            if (tb_pin_config[i].count)
                return TB_BUSY;
        struct tb_link links[4];
        for (unsigned i = 0; i < 4; i++) {
            const uint8_t *item = p + 12 + 12 * i;
            uint32_t age = tb_u32(item + 8);
            if (age > now || age > 30000)
                return TB_INVALID;
            links[i] = (struct tb_link){.rssi = (int16_t)tb_u16(item),
                                        .loss_permille = tb_u16(item + 2),
                                        .rtt_ms = tb_u16(item + 4),
                                        .samples = tb_u16(item + 6),
                                        .measured = now - age};
        }
        return tb_band_prepare(&band, now, tb_u64(p), p[8], p[9], links, links + 2);
    }
    case TB_BAND_COMMIT:
        return f->size == 12 ? tb_band_commit(&band, now, tb_u64(p), tb_u32(p + 8)) : TB_INVALID;
    case TB_BAND_CONFIRM:
        if (f->size != 8)
            return TB_INVALID;
        return tb_band_confirm(&band, now, tb_u64(p), tb_wifi_ready(), tb_wifi_channel(), tb_wifi_rssi(),
                               tb_wifi_he20());
    case TB_BAND_CANCEL: {
        if (f->size != 8)
            return TB_INVALID;
        struct tb_band previous = band;
        bool restore = false;
        enum tb_status cancelled = tb_band_cancel(&band, now, tb_u64(p), &restore);
        if (cancelled != TB_OK)
            return cancelled;
        if (restore && tb_wifi_switch(previous.previous) != TB_OK) {
            band = previous;
            return TB_IO;
        }
        return TB_OK;
    }
    case TB_BAND_FINALIZE:
        return f->size == 8 ? tb_band_finalize(&band, now, tb_u64(p)) : TB_INVALID;
    case TB_REBOOT:
        if (!empty(f))
            return TB_INVALID;
        tb_io_release();
        tb_dap_reset();
        restart_at = now + 500;
        return TB_OK;
    default:
        return TB_UNSUPPORTED;
    }
}
static bool authorized(httpd_req_t *request)
{
    char value[72];
    if (httpd_req_get_hdr_value_len(request, "Authorization") != 71 ||
        httpd_req_get_hdr_value_str(request, "Authorization", value, sizeof value) != ESP_OK)
        return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 7; i++)
        difference |= (uint8_t)value[i] ^ (uint8_t)"Bearer "[i];
    for (unsigned i = 0; i < 64; i++)
        difference |= (uint8_t)value[7 + i] ^ TB_AUTH_TOKEN[i];
    return difference == 0;
}
static esp_err_t rpc(httpd_req_t *request)
{
    if (!authorized(request)) {
        httpd_resp_set_hdr(request, "Connection", "close");
        httpd_resp_send_err(request, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }
    char content_type[32];
    if (request->content_len < TB_HEADER_SIZE || request->content_len > sizeof request_buffer ||
        httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof content_type) != ESP_OK ||
        strcmp(content_type, "application/octet-stream")) {
        httpd_resp_set_hdr(request, "Connection", "close");
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid frame or content type");
        return ESP_FAIL;
    }
    // The HTTP server can run two handlers, while the bounded frame buffers are shared.
    // Serialize only the buffer lifetime; the control mutex still protects device state.
    if (xSemaphoreTake(rpc_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "RPC busy");
        return ESP_FAIL;
    }
    uint64_t started = now_ms();
    size_t received = 0;
    while (received < request->content_len) {
        if (now_ms() - started > 5000) {
            xSemaphoreGive(rpc_mutex);
            return ESP_FAIL;
        }
        int count = httpd_req_recv(request, (char *)request_buffer + received, request->content_len - received);
        if (count <= 0) {
            xSemaphoreGive(rpc_mutex);
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    struct tb_frame frame;
    if (tb_decode(request_buffer, received, &frame) != TB_OK) {
        xSemaphoreGive(rpc_mutex);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid frame");
        return ESP_FAIL;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        xSemaphoreGive(rpc_mutex);
        return ESP_FAIL;
    }
    uint64_t now = now_ms();
    reap(now);
    size_t response_size;
    if (previous_size == received && now - previous_at < 60000 && !memcmp(previous_request, request_buffer, received)) {
        memcpy(response_buffer, previous_response, previous_response_size);
        response_size = previous_response_size;
    } else if (previous_size && now - previous_at < 60000 && frame.request == tb_u32(previous_request + 8) &&
               frame.op == previous_request[5] && frame.lease == tb_u64(previous_request + 16)) {
        response_size = tb_encode(response_buffer, &frame, TB_CONFLICT, 0);
    } else {
        size_t size = 0;
        enum tb_status status = dispatch(&frame, response_buffer + TB_HEADER_SIZE, &size, now);
        response_size = tb_encode(response_buffer, &frame, status, size);
        if (status == TB_OK && frame.op != TB_INFO && frame.op != TB_CONFIG && frame.op != TB_OTA_STATUS &&
            frame.op != TB_WIFI_SCAN && frame.op != TB_TARGET_INFO && frame.op != TB_TARGET_OTA_STATUS) {
            memcpy(previous_request, request_buffer, received);
            previous_size = received;
            memcpy(previous_response, response_buffer, response_size);
            previous_response_size = response_size;
            previous_at = now;
        }
    }
    xSemaphoreGive(mutex);
    httpd_resp_set_type(request, "application/octet-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send(request, (const char *)response_buffer, response_size);
    xSemaphoreGive(rpc_mutex);
    return result;
}
static void control(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t now = now_ms();
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            reap(now);
            tb_wifi_tick(now);
            int action = tb_band_tick(&band, now);
            if (action) {
                uint8_t channel = action > 0 ? band.channel : band.previous;
                if (tb_wifi_switch(channel) != TB_OK) {
                    tb_wifi_switch(band.previous);
                    band.stage = TB_BAND_IDLE;
                }
            }
            if (restart_at && now >= restart_at) {
                tb_io_release();
                tb_dap_reset();
                esp_restart();
            }
            if (boot_deadline && now >= boot_deadline && tb_ota_pending_boot()) {
                tb_io_release();
                tb_dap_reset();
                ESP_LOGE("ota", "Boot confirmation timed out; rolling back");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
            xSemaphoreGive(mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
static void discover(void *arg)
{
    (void)arg;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(4440), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof address) < 0) {
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }
    uint64_t last = 0;
    for (;;) {
        uint8_t query[17];
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof peer;
        int count = recvfrom(socket_fd, query, sizeof query, 0, (struct sockaddr *)&peer, &peer_size);
        uint64_t now = now_ms();
        if (count != 16 || memcmp(query, "TBC5DISC", 8) || now - last < 1000 || !tb_wifi_ready())
            continue;
        uint8_t reply[124] = {0};
        memcpy(reply, "TBC5HERE", 8);
        memcpy(reply + 8, query + 8, 8);
        snprintf((char *)reply + 16, 24, "%s", tb_wifi_id());
        tb_put16(reply + 40, 443);
        reply[42] = 1;
        reply[43] = tb_wifi_is_ap();
        memcpy(reply + 44, TB_CERT_SHA256, 32);
        // The discovery fingerprint is a hint only; clients verify their provisioned DER certificate pin.
        snprintf((char *)reply + 76, 32, "%s", esp_app_get_description()->version);
        snprintf((char *)reply + 108, 16, "%s", TB_BOARD_ID);
        sendto(socket_fd, reply, sizeof reply, 0, (struct sockaddr *)&peer, peer_size);
        last = now;
    }
}
esp_err_t tb_service_start(void)
{
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    mutex = xSemaphoreCreateMutex();
    rpc_mutex = xSemaphoreCreateMutex();
    if (!mutex || !rpc_mutex)
        return ESP_ERR_NO_MEM;
    tb_io_release();
    tb_dap_init();
    esp_err_t status = tb_ota_init();
    ota_available = status == ESP_OK;
    if (!ota_available)
        ESP_LOGE("ota", "Journal unavailable: %s; updates disabled", esp_err_to_name(status));
    enum tb_status target_status = tb_target_ota_init();
    if (target_status != TB_OK)
        ESP_LOGE("target_ota", "Recovery journal unavailable (%d); target resources locked", target_status);
    if (tb_ota_pending_boot())
        boot_deadline = now_ms() + 45000;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = server_cert_start;
    config.servercert_len = server_cert_end - server_cert_start;
    config.prvtkey_pem = server_key_start;
    config.prvtkey_len = server_key_end - server_key_start;
    config.httpd.max_open_sockets = 2;
    config.httpd.lru_purge_enable = true;
    config.httpd.stack_size = 10240;
    config.httpd.recv_wait_timeout = 2;
    config.httpd.send_wait_timeout = 2;
    config.httpd.max_uri_handlers = 2;
    config.tls_handshake_timeout_ms = 5000;
    httpd_handle_t server = NULL;
    status = httpd_ssl_start(&server, &config);
    if (status != ESP_OK)
        return status;
    httpd_uri_t endpoint = {.uri = "/v1/rpc", .method = HTTP_POST, .handler = rpc};
    status = httpd_register_uri_handler(server, &endpoint);
    if (status != ESP_OK) {
        httpd_ssl_stop(server);
        return status;
    }
    if (xTaskCreate(control, "probe_control", 4096, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(discover, "probe_discovery", 3072, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI("probe", "TLS RPC :443/v1/rpc; discovery UDP :4440; target pins are unassigned");
    return ESP_OK;
}
