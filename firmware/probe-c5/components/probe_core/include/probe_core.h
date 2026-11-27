#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TB_HEADER_SIZE 24u
#define TB_MAX_PAYLOAD 4096u
#define TB_DAP_PACKET 1024u
#define TB_MANIFEST_SIZE 112u
#define TB_MANIFEST_SIGNED_SIZE 176u
#define TB_TARGET_MANIFEST_SIGNED_SIZE 120u
#define TB_TARGET_MANIFEST_SIZE 184u
#define TB_TARGET_HELLO_SIZE 180u
#define TB_TARGET_STATUS_SIZE 64u
#define TB_TARGET_CAPABILITY_MASK 0xffu
#define TB_BAND_CONFIRM_MIN_RSSI (-80)
#define TB_BOARD_ID "esp32c5-n4"
#define TB_TARGET_PAYLOAD_MAX 997u
#define TB_TARGET_UART_MAX 1024u
#define TB_TARGET_SPI_SIZE 1024u

enum tb_status
{
    TB_OK,
    TB_INVALID,
    TB_UNAUTHORIZED,
    TB_BUSY,
    TB_LEASE,
    TB_UNSUPPORTED,
    TB_IO,
    TB_INTEGRITY,
    TB_STATE,
    TB_CONFLICT,
    TB_TIMEOUT
};
enum tb_op
{
    TB_INFO = 1,
    TB_ACQUIRE,
    TB_RENEW,
    TB_RELEASE,
    TB_PINS,
    TB_CONFIG,
    TB_DAP = 16,
    TB_BUS,
    TB_OTA_BEGIN = 32,
    TB_OTA_WRITE,
    TB_OTA_FINISH,
    TB_OTA_ACTIVATE,
    TB_OTA_STATUS,
    TB_OTA_ABORT,
    TB_BOOT_CONFIRM,
    TB_TARGET_INFO = 39,
    TB_TARGET_OTA_BEGIN,
    TB_TARGET_OTA_WRITE,
    TB_TARGET_OTA_FINISH,
    TB_TARGET_OTA_ACTIVATE,
    TB_TARGET_OTA_STATUS,
    TB_TARGET_OTA_ABORT,
    TB_WIFI_SCAN = 48,
    TB_BAND_PREPARE,
    TB_BAND_COMMIT,
    TB_BAND_CONFIRM,
    TB_BAND_CANCEL,
    TB_BAND_FINALIZE,
    TB_REBOOT = 63
};
enum tb_protocol
{
    TB_SWD = 1,
    TB_UART,
    TB_SPI,
    TB_I2C,
    TB_PROTOCOL_COUNT
};
enum tb_target_op
{
    TB_TARGET_POLL,
    TB_TARGET_HELLO,
    TB_TARGET_BEGIN,
    TB_TARGET_WRITE,
    TB_TARGET_FINISH,
    TB_TARGET_ACTIVATE,
    TB_TARGET_STATUS,
    TB_TARGET_ABORT
};
enum tb_target_status
{
    TB_TARGET_OK,
    TB_TARGET_INVALID,
    TB_TARGET_BUSY,
    TB_TARGET_UNSUPPORTED,
    TB_TARGET_IO,
    TB_TARGET_INTEGRITY,
    TB_TARGET_STATE,
    TB_TARGET_CONFLICT,
    TB_TARGET_TIMEOUT
};
enum tb_target_phase
{
    TB_TARGET_EMPTY,
    TB_TARGET_RECEIVING,
    TB_TARGET_VERIFIED,
    TB_TARGET_TRIAL,
    TB_TARGET_CONFIRMED,
    TB_TARGET_ABORTED,
    TB_TARGET_ROLLED_BACK,
    TB_TARGET_FAILED
};
enum tb_target_boot_result
{
    TB_TARGET_BOOT_NONE,
    TB_TARGET_BOOT_TRIAL_PENDING,
    TB_TARGET_BOOT_CONFIRMED,
    TB_TARGET_BOOT_ROLLED_BACK
};
struct tb_frame
{
    uint8_t op;
    uint32_t request;
    uint64_t lease;
    const uint8_t *data;
    size_t size;
};
struct tb_target_frame
{
    uint8_t op, status;
    uint32_t sequence;
    const uint8_t *data;
    size_t size;
};
struct tb_target_status_record
{
    uint64_t job_id, rollback_index;
    uint8_t phase, boot_result;
    uint32_t received, checkpoint, total;
    char active_version[32];
};
struct tb_target_hello
{
    uint16_t protocol_major, protocol_minor, max_data_chunk;
    uint32_t capabilities, max_image_length;
    char board_id[32], active_version[32], bootloader_version[16];
    uint8_t unique_id[12];
    uint64_t rollback_index;
    struct tb_target_status_record update;
};
struct tb_pins
{
    uint8_t protocol, count;
    int8_t pin[4];
    uint32_t hz;
};
struct tb_lease
{
    uint64_t id, deadline;
};
struct tb_link
{
    int16_t rssi;
    uint16_t loss_permille, rtt_ms, samples;
    uint64_t measured;
};
struct tb_scan_ap
{
    uint8_t bssid[6], channel, wifi6, authmode, sightings;
    int8_t rssi[5];
    uint64_t first_seen;
};
enum tb_band_stage
{
    TB_BAND_IDLE,
    TB_BAND_PREPARED,
    TB_BAND_COMMITTED,
    TB_BAND_SWITCHED,
    TB_BAND_CONFIRMED
};
struct tb_band
{
    enum tb_band_stage stage;
    uint64_t id, deadline, last_switch, previous_last_switch, rollback_until;
    uint8_t channel, previous;
    bool confirmed;
};

uint16_t tb_u16(const uint8_t *p);
uint32_t tb_u32(const uint8_t *p);
uint64_t tb_u64(const uint8_t *p);
void tb_put16(uint8_t *p, uint16_t n);
void tb_put32(uint8_t *p, uint32_t n);
void tb_put64(uint8_t *p, uint64_t n);
enum tb_status tb_decode(const uint8_t *p, size_t n, struct tb_frame *f);
enum tb_status tb_pins_decode(const uint8_t *data, size_t size, struct tb_pins *pins);
size_t tb_encode(uint8_t *p, const struct tb_frame *f, enum tb_status status, size_t size);
uint32_t tb_crc32(const uint8_t *data, size_t size);
size_t tb_target_uart_encode(const struct tb_target_frame *frame, uint8_t *wire, size_t capacity);
enum tb_status tb_target_uart_decode(const uint8_t *wire, size_t size, struct tb_target_frame *frame,
                                     uint8_t payload[TB_TARGET_PAYLOAD_MAX]);
size_t tb_target_spi_encode(const struct tb_target_frame *frame, uint8_t wire[TB_TARGET_SPI_SIZE]);
enum tb_status tb_target_spi_decode(const uint8_t *wire, size_t size, struct tb_target_frame *frame,
                                    uint8_t payload[TB_TARGET_PAYLOAD_MAX]);
enum tb_status tb_target_status_decode(const uint8_t *wire, size_t size, struct tb_target_status_record *status);
enum tb_status tb_target_hello_decode(const uint8_t *wire, size_t size, struct tb_target_hello *hello);
enum tb_status tb_acquire(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl);
bool tb_owned(const struct tb_lease *l, uint64_t now, uint64_t id);
enum tb_status tb_renew(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl);
enum tb_status tb_validate_pins(const struct tb_pins *p, uint32_t allowed, uint32_t occupied);
bool tb_dap_validate(const uint8_t *p, size_t n, uint32_t max_hz);
int tb_link_score(const struct tb_link *local, const struct tb_link *peer, uint64_t now);
int8_t tb_worst_rssi(const int8_t *samples, size_t count);
bool tb_scan_merge(struct tb_scan_ap *records, size_t *count, size_t capacity, const struct tb_scan_ap *sample);
int8_t tb_scan_median_rssi(const struct tb_scan_ap *record);
enum tb_status tb_band_prepare(struct tb_band *b, uint64_t now, uint64_t id, uint8_t channel, uint8_t previous,
                               const struct tb_link current[2], const struct tb_link next[2]);
enum tb_status tb_band_commit(struct tb_band *b, uint64_t now, uint64_t id, uint32_t delay_ms);
enum tb_status tb_band_confirm(struct tb_band *b, uint64_t now, uint64_t id, bool ready, uint8_t channel, int rssi,
                               bool he20);
enum tb_status tb_band_finalize(struct tb_band *b, uint64_t now, uint64_t id);
enum tb_status tb_band_cancel(struct tb_band *b, uint64_t now, uint64_t id, bool *restore);
enum tb_status tb_band_lease_end(struct tb_band *b, bool *restore);
// Returns 1 to switch, -1 to restore the previous channel, 0 to keep it.
int tb_band_tick(struct tb_band *b, uint64_t now);

#ifdef __cplusplus
}
#endif
