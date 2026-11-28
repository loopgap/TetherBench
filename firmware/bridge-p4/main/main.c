#include "bridge_frame.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

static const char *TAG = "tb-p4";
static QueueHandle_t request_queue;
static struct tb_bridge_stream cdc_streams[TB_BRIDGE_STREAM_INTERFACES];
static uint8_t cdc_input[TB_BRIDGE_STREAM_INTERFACES][CONFIG_TINYUSB_CDC_RX_BUFSIZE];

static void send_frame(const struct tb_bridge_frame *frame)
{
    uint8_t wire[TB_BRIDGE_FRAME_BYTES];
    tb_bridge_encode(frame, wire);
    (void)tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, wire, sizeof(wire));
    (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

static bool consume_wire(const uint8_t wire[TB_BRIDGE_FRAME_BYTES], void *context)
{
    (void)context;
    struct tb_bridge_frame frame;
    if (!tb_bridge_decode(wire, &frame)) {
        struct tb_bridge_frame error = {.opcode = TB_BRIDGE_ERROR, .length = 1, .payload = {TB_BRIDGE_BAD_LENGTH}};
        send_frame(&error); return true;
    }
    if (xQueueSend(request_queue, &frame, 0) != pdTRUE) {
        struct tb_bridge_frame error = {.opcode = TB_BRIDGE_ERROR, .length = 1, .payload = {TB_BRIDGE_QUEUE_FULL}, .sequence = frame.sequence};
        send_frame(&error);
    }
    return true;
}

static void cdc_rx(int itf, cdcacm_event_t *event)
{
    (void)event;
    size_t got = 0;
    if (itf < 0 || (unsigned)itf >= TB_BRIDGE_STREAM_INTERFACES ||
        tinyusb_cdcacm_read(itf, cdc_input[itf], sizeof(cdc_input[itf]), &got) != ESP_OK || !got) return;
    if (!tb_bridge_stream_feed(&cdc_streams[itf], cdc_input[itf], got, consume_wire, NULL)) {
        struct tb_bridge_frame error = {.opcode = TB_BRIDGE_ERROR, .length = 1, .payload = {TB_BRIDGE_BAD_LENGTH}};
        send_frame(&error);
    }
}

void app_main(void)
{
    request_queue = xQueueCreate(4, sizeof(struct tb_bridge_frame));
    if (!request_queue) return;
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
#if defined(TB_BRIDGE_USB_FULL_SPEED)
    tusb_cfg.port = TINYUSB_PORT_FULL_SPEED_0;
#else
    tusb_cfg.port = TINYUSB_PORT_HIGH_SPEED_0;
#endif
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&(tinyusb_config_cdcacm_t){
        .cdc_port = TINYUSB_CDC_ACM_0, .callback_rx = cdc_rx}));
    ESP_LOGI(TAG, "P4 bridge online; external SPI/C5 pins remain disabled until a board profile is selected");
    while (true) {
        struct tb_bridge_frame request;
        if (xQueueReceive(request_queue, &request, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        struct tb_bridge_frame response = {.opcode = request.opcode == TB_BRIDGE_PING ? TB_BRIDGE_PING : TB_BRIDGE_ERROR,
            .sequence = request.sequence, .length = 1, .payload = {request.opcode == TB_BRIDGE_PING ? TB_BRIDGE_OK : TB_BRIDGE_LINK_OFFLINE}};
        send_frame(&response);
    }
}
