#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TB_HEADER_SIZE 24u
#define TB_MAX_PAYLOAD 4096u
#define TB_DAP_PACKET 1024u
#define TB_MANIFEST_SIZE 112u
#define TB_MANIFEST_SIGNED_SIZE 176u
#define TB_BOARD_ID "esp32c5-n4"

enum tb_status { TB_OK, TB_INVALID, TB_UNAUTHORIZED, TB_BUSY, TB_LEASE, TB_UNSUPPORTED,
                 TB_IO, TB_INTEGRITY, TB_STATE, TB_CONFLICT, TB_TIMEOUT };
enum tb_op { TB_INFO=1, TB_ACQUIRE, TB_RENEW, TB_RELEASE, TB_PINS, TB_CONFIG,
             TB_DAP=16, TB_BUS, TB_OTA_BEGIN=32, TB_OTA_WRITE, TB_OTA_FINISH,
             TB_OTA_ACTIVATE, TB_OTA_STATUS, TB_OTA_ABORT, TB_BOOT_CONFIRM,
             TB_WIFI_SCAN=48, TB_BAND_PREPARE, TB_BAND_COMMIT, TB_BAND_CONFIRM,
             TB_BAND_CANCEL, TB_REBOOT=63 };
enum tb_protocol { TB_SWD=1, TB_UART, TB_SPI, TB_I2C, TB_PROTOCOL_COUNT };
struct tb_frame { uint8_t op; uint32_t request; uint64_t lease; const uint8_t *data; size_t size; };
struct tb_pins { uint8_t protocol, count; int8_t pin[4]; uint32_t hz; };
struct tb_lease { uint64_t id, deadline; };
struct tb_link { int16_t rssi; uint16_t loss_permille, rtt_ms, samples; uint64_t measured; };
enum tb_band_stage { TB_BAND_IDLE, TB_BAND_PREPARED, TB_BAND_COMMITTED, TB_BAND_SWITCHED };
struct tb_band { enum tb_band_stage stage; uint64_t id, deadline, last_switch;
                 uint8_t channel, previous; };

uint16_t tb_u16(const uint8_t *p);
uint32_t tb_u32(const uint8_t *p);
uint64_t tb_u64(const uint8_t *p);
void tb_put16(uint8_t *p, uint16_t n);
void tb_put32(uint8_t *p, uint32_t n);
void tb_put64(uint8_t *p, uint64_t n);
enum tb_status tb_decode(const uint8_t *p, size_t n, struct tb_frame *f);
size_t tb_encode(uint8_t *p, const struct tb_frame *f, enum tb_status status, size_t size);
enum tb_status tb_acquire(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl);
bool tb_owned(const struct tb_lease *l, uint64_t now, uint64_t id);
enum tb_status tb_renew(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl);
enum tb_status tb_validate_pins(const struct tb_pins *p, uint32_t allowed, uint32_t occupied);
bool tb_dap_validate(const uint8_t *p, size_t n, uint32_t max_hz);
int tb_link_score(const struct tb_link *local, const struct tb_link *peer, uint64_t now);
enum tb_status tb_band_prepare(struct tb_band *b, uint64_t now, uint64_t id, uint8_t channel,
                              uint8_t previous, const struct tb_link current[2], const struct tb_link next[2]);
enum tb_status tb_band_commit(struct tb_band *b, uint64_t now, uint64_t id, uint32_t delay_ms);
enum tb_status tb_band_confirm(struct tb_band *b, uint64_t now, uint64_t id);
// Returns 1 to switch, -1 to restore the previous channel, 0 to keep it.
int tb_band_tick(struct tb_band *b, uint64_t now);
