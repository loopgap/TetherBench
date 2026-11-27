#include "probe_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void frames(void)
{
    uint8_t wire[TB_HEADER_SIZE + TB_MAX_PAYLOAD] = {0};
    struct tb_frame f = {.op = TB_DAP, .request = 0x12345678, .lease = 0x123456789abcdefULL}, decoded;
    size_t n = tb_encode(wire, &f, TB_OK, 2);
    assert(n == 26 && wire[6] == 0 && wire[7] == 0x80);
    // A response cannot be replayed as a request.
    assert(tb_decode(wire, n, &decoded) == TB_INVALID);
    tb_put16(wire + 6, 0);
    assert(tb_decode(wire, n, &decoded) == TB_OK);
    assert(decoded.request == f.request && decoded.lease == f.lease && decoded.size == 2);
    assert(tb_decode(NULL, n, &decoded) == TB_INVALID);
    assert(tb_decode(wire, n, NULL) == TB_INVALID);
    for (size_t i = 0; i < n; i++)
        assert(tb_decode(wire, i, &decoded) == TB_INVALID);
    assert(tb_decode(wire, n + 1, &decoded) == TB_INVALID);
    wire[4] = 2;
    assert(tb_decode(wire, n, &decoded) == TB_INVALID);
    wire[4] = 1;
    tb_put32(wire + 12, TB_MAX_PAYLOAD + 1);
    assert(tb_decode(wire, sizeof wire, &decoded) == TB_INVALID);
    assert(tb_encode(wire, &f, TB_OK, TB_MAX_PAYLOAD + 1) == 0);
    assert(tb_encode(NULL, &f, TB_OK, 0) == 0);
    assert(tb_encode(wire, NULL, TB_OK, 0) == 0);
    assert(tb_encode(wire, &f, (enum tb_status) - 1, 0) == 0);
    assert(tb_encode(wire, &f, (enum tb_status)11, 0) == 0);
}
static void leases(void)
{
    struct tb_lease l = {0};
    assert(tb_acquire(&l, 100, 0, 10000) == TB_INVALID);
    assert(tb_acquire(&l, 100, 42, 10000) == TB_OK);
    assert(tb_acquire(&l, 101, 43, 10000) == TB_BUSY);
    assert(!tb_owned(&l, 102, 43));
    assert(tb_renew(&l, 102, 43, 10000) == TB_LEASE);
    assert(tb_renew(&l, 102, 42, 100) == TB_INVALID);
    assert(tb_renew(&l, 102, 42, 5000) == TB_OK);
    assert(!tb_owned(&l, 5102, 42));
    assert(tb_acquire(&l, 5102, 43, 5000) == TB_OK);
    assert(tb_acquire(&l, UINT64_MAX - 10, 43, 5000) == TB_INVALID);
}
static void pins(void)
{
    struct tb_pins p = {.protocol = TB_SWD, .count = 3, .pin = {2, 3, 4, -1}, .hz = 4000000};
    uint32_t allowed = (1u << 2) | (1u << 3) | (1u << 4);
    assert(tb_validate_pins(&p, allowed, 0) == TB_OK);
    assert(tb_validate_pins(&p, allowed, 1u << 4) == TB_CONFLICT);
    p.pin[1] = 2;
    assert(tb_validate_pins(&p, allowed, 0) == TB_CONFLICT);
    p.pin[1] = 16;
    assert(tb_validate_pins(&p, allowed, 0) == TB_CONFLICT);
    p.pin[1] = -1;
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
    p.pin[1] = 3;
    p.hz = 4000001;
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
    p.hz = 0;
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
    p.protocol = TB_I2C;
    p.hz = 400000;
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
    p = (struct tb_pins){.protocol = TB_UART, .count = 3, .pin = {2, 3, 4, -1}, .hz = 115200};
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
    p = (struct tb_pins){.protocol = TB_UART, .pin = {-1, -1, -1, -1}};
    assert(tb_validate_pins(&p, 0, UINT32_MAX) == TB_OK);
    p.pin[0] = 2;
    assert(tb_validate_pins(&p, allowed, 0) == TB_INVALID);
}
static void pin_request_decode(void)
{
    uint8_t request[12] = {TB_SWD, 3, 0, 0, 0, 0, 0, 0, 2, 3, 4, 0xff};
    tb_put32(request + 4, 4000000);
    struct tb_pins pins = {0};
    assert(tb_pins_decode(request, sizeof request, &pins) == TB_OK);
    assert(pins.protocol == TB_SWD && pins.count == 3 && pins.hz == 4000000);
    assert(pins.pin[0] == 2 && pins.pin[1] == 3 && pins.pin[2] == 4 && pins.pin[3] == -1);
    for (size_t size = 0; size < sizeof request; size++)
        assert(tb_pins_decode(request, size, &pins) == TB_INVALID);
    assert(tb_pins_decode(request, sizeof request + 1, &pins) == TB_INVALID);
    request[2] = 1;
    assert(tb_pins_decode(request, sizeof request, &pins) == TB_INVALID);
    assert(tb_pins_decode(NULL, sizeof request, &pins) == TB_INVALID);
    assert(tb_pins_decode(request, sizeof request, NULL) == TB_INVALID);
}
static void dap(void)
{
    const uint8_t transfer[] = {5, 0, 2, 2, 0, 1, 2, 3, 4};
    assert(tb_dap_validate(transfer, sizeof transfer, 4000000));
    for (size_t n = 0; n < sizeof transfer; n++)
        assert(!tb_dap_validate(transfer, n, 4000000));
    const uint8_t pyocd_default_configure[] = {4, 2, 150, 0, 0, 0};
    const uint8_t maximum_configure[] = {4, 32, 255, 255, 255, 255};
    assert(tb_dap_validate(pyocd_default_configure, sizeof pyocd_default_configure, 4000000));
    assert(tb_dap_validate(maximum_configure, sizeof maximum_configure, 4000000));
    uint8_t max_transfer_read[3 + 255] = {5, 0, 255};
    memset(max_transfer_read + 3, 2, 255);
    assert(tb_dap_validate(max_transfer_read, sizeof max_transfer_read, 4000000));
    uint8_t max_transfer_write[3 + 5 * 204] = {5, 0, 204};
    for (unsigned i = 0; i < 204; i++)
        max_transfer_write[3 + 5 * i] = 0;
    assert(tb_dap_validate(max_transfer_write, sizeof max_transfer_write, 4000000));
    uint8_t max_block_read[] = {6, 0, 255, 0, 2};
    assert(tb_dap_validate(max_block_read, sizeof max_block_read, 4000000));
    uint8_t max_block_write[5 + 4 * 254] = {6, 0, 254, 0, 0};
    assert(tb_dap_validate(max_block_write, sizeof max_block_write, 4000000));
    uint8_t oversized_block[TB_DAP_PACKET] = {6, 0, 255, 0, 0};
    assert(!tb_dap_validate(oversized_block, sizeof oversized_block, 4000000));
    const uint8_t batch[] = {0x7f, 2, 0, 0xf0, 3};
    assert(tb_dap_validate(batch, sizeof batch, 4000000));
    const uint8_t recursive[] = {0x7f, 1, 0x7f, 1, 3};
    assert(!tb_dap_validate(recursive, sizeof recursive, 4000000));
    const uint8_t huge_read[] = {6, 0, 255, 255, 2};
    assert(!tb_dap_validate(huge_read, sizeof huge_read, 4000000));
    const uint8_t sequence[] = {0x1d, 2, 0x80, 8, 0xff};
    assert(tb_dap_validate(sequence, sizeof sequence, 4000000));
    const uint8_t clock[] = {0x11, 0, 0, 0, 0};
    assert(!tb_dap_validate(clock, sizeof clock, 4000000));
    uint32_t seed = 1234;
    uint8_t fuzz[1024];
    for (unsigned i = 0; i < 30000; i++) {
        size_t n = (seed >> 16) % sizeof fuzz;
        for (size_t k = 0; k < n; k++) {
            seed = seed * 1664525u + 1013904223u;
            fuzz[k] = (uint8_t)(seed >> 24);
        }
        (void)tb_dap_validate(fuzz, n, 4000000);
    }
}
static void bands(void)
{
    struct tb_band b = {0};
    struct tb_link old[2] = {{-80, 20, 15, 10, 1000}, {-78, 10, 10, 10, 1000}};
    struct tb_link next[2] = {{-55, 5, UINT16_MAX, 5, 1000}, {-58, 10, UINT16_MAX, 5, 1000}};
    next[0].samples = 4;
    assert(tb_band_prepare(&b, 1100, 1, 40, 6, old, next) == TB_INVALID);
    next[0].samples = 5;
    next[0].rtt_ms = 5001;
    assert(tb_band_prepare(&b, 1100, 1, 40, 6, old, next) == TB_INVALID);
    next[0].rtt_ms = UINT16_MAX;
    assert(tb_band_prepare(&b, 1100, 1, 40, 6, old, next) == TB_OK);
    assert(tb_band_commit(&b, 1200, 2, 1000) == TB_STATE);
    assert(tb_band_commit(&b, 1200, 1, 100) == TB_INVALID);
    assert(tb_band_commit(&b, 1200, 1, 1000) == TB_OK);
    assert(tb_band_tick(&b, 2199) == 0);
    assert(tb_band_tick(&b, 2200) == 1);
    assert(tb_band_tick(&b, 17200) == -1);
    assert(b.stage == TB_BAND_IDLE);
    assert(tb_band_prepare(&b, 32001, 2, 40, 6, old, next) == TB_INVALID);
    old[0].measured = old[1].measured = next[0].measured = next[1].measured = 32001;
    old[0].rtt_ms = UINT16_MAX;
    assert(tb_band_prepare(&b, 32001, 2, 40, 6, old, next) == TB_INVALID);
    old[0].rtt_ms = 15;
    assert(tb_band_prepare(&b, 32001, 2, 40, 6, old, next) == TB_OK);
    assert(tb_band_commit(&b, 32100, 2, 500) == TB_OK);
    assert(tb_band_tick(&b, 32600) == 1);
    assert(tb_band_confirm(&b, 33000, 2, true, 40, -81, true) == TB_STATE);
    assert(tb_band_confirm(&b, 33000, 2, true, 40, -80, false) == TB_STATE);
    assert(tb_band_confirm(&b, 33000, 2, true, 6, -70, true) == TB_STATE);
    assert(tb_band_confirm(&b, 33000, 2, true, 40, -80, true) == TB_OK);
    assert(b.stage == TB_BAND_CONFIRMED);
    struct tb_band released = b;
    bool release_restore = false;
    assert(tb_band_lease_end(&released, &release_restore) == TB_OK);
    assert(release_restore && released.stage == TB_BAND_IDLE && !released.confirmed);
    struct tb_band prepared_lease = {.stage = TB_BAND_PREPARED, .id = 3};
    assert(tb_band_lease_end(&prepared_lease, &release_restore) == TB_OK);
    assert(!release_restore && prepared_lease.stage == TB_BAND_IDLE);
    struct tb_band committed_lease = {.stage = TB_BAND_COMMITTED, .id = 4};
    assert(tb_band_lease_end(&committed_lease, &release_restore) == TB_OK);
    assert(!release_restore && committed_lease.stage == TB_BAND_IDLE);
    struct tb_band switched_lease = {.stage = TB_BAND_SWITCHED, .id = 5, .previous = 6};
    assert(tb_band_lease_end(&switched_lease, &release_restore) == TB_OK);
    assert(release_restore && switched_lease.stage == TB_BAND_IDLE);
    assert(tb_band_finalize(&b, 33001, 2) == TB_OK);
    assert(b.stage == TB_BAND_IDLE && b.confirmed);
    struct tb_band finalized_release = b;
    assert(tb_band_lease_end(&finalized_release, &release_restore) == TB_OK && !release_restore);
    assert(finalized_release.confirmed && finalized_release.rollback_until == b.rollback_until);
    struct tb_band expired = b;
    bool expired_restore = true;
    assert(tb_band_tick(&expired, 48001) == 0 && !expired.confirmed);
    assert(tb_band_cancel(&expired, 48001, 2, &expired_restore) == TB_OK && !expired_restore);
    assert(tb_band_prepare(&b, 33002, 3, 6, 40, old, next) == TB_STATE);
    next[1].loss_permille = 1001;
    assert(tb_band_prepare(&b, 33002, 3, 6, 40, old, next) == TB_INVALID);
    bool restore = false;
    assert(tb_band_cancel(&b, 33002, 2, &restore) == TB_OK && restore);
    assert(b.stage == TB_BAND_IDLE && !b.confirmed && b.last_switch == 0);
    assert(tb_band_cancel(&b, 33003, 2, &restore) == TB_OK && !restore);
    assert(tb_band_prepare(&b, 33003, 3, 6, 40, old, next) == TB_INVALID);
    next[1].loss_permille = 10;
    assert(tb_band_prepare(&b, 33003, 3, 6, 40, old, next) == TB_OK);
    assert(tb_band_cancel(&b, 33004, 3, &restore) == TB_OK && !restore);
    assert(b.stage == TB_BAND_IDLE);
    struct tb_band switched = {.stage = TB_BAND_SWITCHED, .id = 9, .previous = 6};
    assert(tb_band_cancel(&switched, 500, 8, &restore) == TB_INVALID);
    assert(tb_band_cancel(&switched, 500, 9, &restore) == TB_OK && restore);
    assert(tb_band_cancel(&switched, 501, 9, &restore) == TB_OK && !restore);
}
static void wireless_rssi(void)
{
    const int8_t samples[] = {-38, -71, -55};
    assert(tb_worst_rssi(samples, 3) == -71);
    assert(tb_worst_rssi(samples, 0) == -127);
    assert(tb_worst_rssi(NULL, 3) == -127);
    struct tb_scan_ap records[2] = {0};
    size_t count = 0;
    struct tb_scan_ap sample = {
        .bssid = {1, 2, 3, 4, 5, 6}, .channel = 36, .wifi6 = 1, .sightings = 1, .rssi = {-60}, .first_seen = 100};
    assert(tb_scan_merge(records, &count, 2, &sample) && count == 1);
    sample.first_seen = 200;
    sample.rssi[0] = -70;
    assert(tb_scan_merge(records, &count, 2, &sample));
    sample.rssi[0] = -50;
    sample.wifi6 = 0;
    assert(tb_scan_merge(records, &count, 2, &sample));
    assert(records[0].sightings == 3 && !records[0].wifi6 && records[0].first_seen == 100 &&
           tb_scan_median_rssi(&records[0]) == -60);
    sample.bssid[0] = 9;
    assert(tb_scan_merge(records, &count, 2, &sample) && count == 2);
    sample.bssid[0] = 10;
    assert(!tb_scan_merge(records, &count, 2, &sample) && count == 2);
}
static void target_frames(void)
{
    static const uint8_t crc_input[] = "123456789";
    assert(tb_crc32(crc_input, sizeof crc_input - 1) == 0xcbf43926);
    uint8_t data[TB_TARGET_PAYLOAD_MAX];
    for (size_t i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i * 13u);
    struct tb_target_frame frame = {.op = TB_TARGET_WRITE, .sequence = 0x12345678, .data = data, .size = sizeof data};
    uint8_t uart[TB_TARGET_UART_MAX], uart_payload[TB_TARGET_PAYLOAD_MAX];
    size_t uart_size = tb_target_uart_encode(&frame, uart, sizeof uart);
    assert(uart_size <= sizeof uart && uart_size > 1 && uart[uart_size - 1] == 0);
    for (size_t i = 0; i + 1 < uart_size; i++)
        assert(uart[i]);
    struct tb_target_frame decoded = {0};
    assert(tb_target_uart_decode(uart, uart_size, &decoded, uart_payload) == TB_OK);
    assert(decoded.op == frame.op && decoded.sequence == frame.sequence && decoded.size == sizeof data &&
           !memcmp(decoded.data, data, sizeof data));
    memset(data, 0, sizeof data);
    uart_size = tb_target_uart_encode(&frame, uart, sizeof uart);
    assert(uart_size <= sizeof uart && uart_size > sizeof data);
    assert(tb_target_uart_decode(uart, uart_size, &decoded, uart_payload) == TB_OK);
    assert(decoded.size == sizeof data && !memcmp(decoded.data, data, sizeof data));
    uint8_t guarded[TB_TARGET_UART_MAX + 8];
    for (size_t capacity = 0; capacity < uart_size; capacity++) {
        memset(guarded, 0xa5, sizeof guarded);
        assert(tb_target_uart_encode(&frame, guarded, capacity) == 0);
        for (size_t i = capacity; i < capacity + 8; i++)
            assert(guarded[i] == 0xa5);
    }
    uint8_t spi[TB_TARGET_SPI_SIZE], spi_payload[TB_TARGET_PAYLOAD_MAX];
    assert(tb_target_spi_encode(&frame, spi) == 20 + sizeof data);
    assert(tb_target_spi_decode(spi, sizeof spi, &decoded, spi_payload) == TB_OK);
    assert(decoded.op == frame.op && decoded.sequence == frame.sequence && decoded.size == sizeof data &&
           !memcmp(decoded.data, data, sizeof data));
    spi[TB_TARGET_SPI_SIZE - 1] = 1;
    assert(tb_target_spi_decode(spi, sizeof spi, &decoded, spi_payload) == TB_INVALID);
    spi[TB_TARGET_SPI_SIZE - 1] = 0;
    spi[20] ^= 1;
    assert(tb_target_spi_decode(spi, sizeof spi, &decoded, spi_payload) == TB_INTEGRITY);
    uart[uart_size - 2] ^= 1;
    assert(tb_target_uart_decode(uart, uart_size, &decoded, uart_payload) != TB_OK);
    assert(tb_target_uart_decode(uart, uart_size - 1, &decoded, uart_payload) == TB_INVALID);
    frame.sequence = 0;
    assert(tb_target_uart_encode(&frame, uart, sizeof uart) == 0);
}
static void target_records(void)
{
    uint8_t hello_wire[TB_TARGET_HELLO_SIZE] = {0};
    tb_put16(hello_wire, 1);
    tb_put32(hello_wire + 4, TB_TARGET_CAPABILITY_MASK);
    tb_put32(hello_wire + 8, 65536);
    tb_put16(hello_wire + 12, 983);
    memcpy(hello_wire + 16, "test-mcu", 9);
    memcpy(hello_wire + 48, "1.0", 4);
    memcpy(hello_wire + 80, "boot-1", 7);
    memset(hello_wire + 96, 0x5a, 12);
    tb_put64(hello_wire + 108, 9);
    struct tb_target_hello hello;
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire, &hello) == TB_OK);
    assert(hello.protocol_major == 1 && hello.max_data_chunk == 983 && hello.rollback_index == 9 &&
           !strcmp(hello.board_id, "test-mcu") && hello.update.phase == TB_TARGET_EMPTY);
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire - 1, &hello) == TB_INVALID);
    tb_put32(hello_wire + 4, TB_TARGET_CAPABILITY_MASK | 0x100);
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire, &hello) == TB_INVALID);
    tb_put32(hello_wire + 4, TB_TARGET_CAPABILITY_MASK);
    tb_put16(hello_wire + 14, 1);
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire, &hello) == TB_INVALID);
    tb_put16(hello_wire + 14, 0);
    tb_put16(hello_wire, 2);
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire, &hello) == TB_UNSUPPORTED);
    tb_put16(hello_wire, 1);
    memset(hello_wire + 16, 'x', 32);
    assert(tb_target_hello_decode(hello_wire, sizeof hello_wire, &hello) == TB_INVALID);

    uint8_t status_wire[TB_TARGET_STATUS_SIZE] = {0};
    struct tb_target_status_record status;
    tb_put64(status_wire, 7);
    status_wire[8] = TB_TARGET_RECEIVING;
    tb_put32(status_wire + 12, 2048);
    tb_put32(status_wire + 16, 1024);
    tb_put32(status_wire + 20, 4096);
    tb_put64(status_wire + 24, 8);
    memcpy(status_wire + 32, "1.0", 4);
    assert(tb_target_status_decode(status_wire, sizeof status_wire, &status) == TB_OK);
    assert(status.job_id == 7 && status.checkpoint == 1024 && status.rollback_index == 8);
    tb_put32(status_wire + 16, 4096);
    assert(tb_target_status_decode(status_wire, sizeof status_wire, &status) == TB_INVALID);
    memset(status_wire, 0, sizeof status_wire);
    tb_put64(status_wire, 7);
    assert(tb_target_status_decode(status_wire, sizeof status_wire, &status) == TB_INVALID);
}
int main(void)
{
    frames();
    leases();
    pins();
    pin_request_decode();
    dap();
    bands();
    wireless_rssi();
    target_frames();
    target_records();
    puts("core: frame, lease, pin, DAP, band, RSSI and target OTA contract checks passed");
}
