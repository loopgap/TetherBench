#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "probe_target_ota.h"
#include "probe_ota.h"
#include "probe_io.h"

struct tb_pins tb_pin_config[TB_PROTOCOL_COUNT];
static uint8_t mock_mode, mock_status, mock_response[TB_TARGET_UART_MAX];
static size_t mock_response_size, mock_response_offset;
static uint8_t hello[TB_TARGET_HELLO_SIZE], target_status[TB_TARGET_STATUS_SIZE];
static uint8_t target_manifest[TB_TARGET_MANIFEST_SIZE], target_image[128 * 1024];
static uint8_t pending_spi[TB_TARGET_SPI_SIZE];
static uint32_t request_sequence, random_value = 0x12345678;
static unsigned busy_polls, target_calls;
static int64_t mock_time;
static uint8_t durable[256];
static size_t durable_size;
static uint8_t staged[256];
static size_t staged_size;
static bool persisted, staged_write, staged_erase;
static bool valid_manifest = true, fail_save, fail_erase, fail_commit;
static bool fail_target_begin;
static bool defer_checkpoint;

esp_err_t nvs_flash_init_partition(const char *part)
{
    assert(!strcmp(part, "ota_journal"));
    return ESP_OK;
}
esp_err_t nvs_open_from_partition(const char *part, const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(part, "ota_journal") && !strcmp(name, "target") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    assert(handle == 1 && !strcmp(key, "job"));
    if (!persisted)
        return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= durable_size);
    memcpy(out, durable, durable_size);
    *size = durable_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(handle == 1 && !strcmp(key, "job") && size <= sizeof durable);
    if (fail_save)
        return ESP_FAIL;
    memcpy(staged, data, size);
    staged_size = size;
    staged_write = true;
    staged_erase = false;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    if (fail_commit)
        return ESP_FAIL;
    if (staged_erase) {
        persisted = false;
        durable_size = 0;
    } else if (staged_write) {
        memcpy(durable, staged, staged_size);
        durable_size = staged_size;
        persisted = true;
    }
    staged_write = false;
    staged_erase = false;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1 && !strcmp(key, "job"));
    if (fail_erase)
        return ESP_FAIL;
    staged_erase = true;
    staged_write = false;
    return ESP_OK;
}
enum tb_status tb_ota_validate_target_manifest(const uint8_t *manifest, size_t size)
{
    assert(manifest && size == TB_TARGET_MANIFEST_SIZE);
    return valid_manifest ? TB_OK : TB_INTEGRITY;
}

uint32_t esp_random(void)
{
    return random_value++;
}
int64_t esp_timer_get_time(void)
{
    return mock_time;
}

static void fill_hello(void)
{
    memset(hello, 0, sizeof hello);
    tb_put16(hello, 1);
    tb_put32(hello + 4, TB_TARGET_CAPABILITY_MASK);
    tb_put32(hello + 8, 128 * 1024);
    tb_put16(hello + 12, 983);
    memcpy(hello + 16, "test-mcu", 9);
    memcpy(hello + 48, "1.2", 4);
    memcpy(hello + 80, "boot-1", 7);
    memset(hello + 96, 0x5a, 12);
    tb_put64(hello + 108, 4);
}

static void refresh_target_status(void)
{
    memcpy(hello + 116, target_status, sizeof target_status);
}

static void model_target(const struct tb_target_frame *request, const uint8_t **data, size_t *size)
{
    *data = NULL;
    *size = 0;
    target_calls++;
    switch (request->op) {
    case TB_TARGET_HELLO:
        assert(!request->size);
        refresh_target_status();
        *data = hello;
        *size = sizeof hello;
        return;
    case TB_TARGET_BEGIN:
        assert(request->size == TB_TARGET_MANIFEST_SIZE && persisted);
        memcpy(target_manifest, request->data, request->size);
        memset(target_status, 0, sizeof target_status);
        memcpy(target_status, target_manifest, 8);
        target_status[8] = TB_TARGET_RECEIVING;
        tb_put32(target_status + 20, tb_u32(target_manifest + 12));
        memcpy(hello + 116, target_status, sizeof target_status);
        *data = target_status;
        *size = sizeof target_status;
        return;
    case TB_TARGET_WRITE: {
        assert(request->size >= 15 && tb_u64(request->data) == tb_u64(target_manifest));
        uint32_t offset = tb_u32(request->data + 8);
        size_t count = tb_u16(request->data + 12);
        assert(count == request->size - 14 && offset + count <= sizeof target_image);
        uint32_t received = tb_u32(target_status + 12);
        if (offset == received) {
            memcpy(target_image + offset, request->data + 14, count);
            received += (uint32_t)count;
        } else {
            assert(offset + count <= received && !memcmp(target_image + offset, request->data + 14, count));
        }
        tb_put32(target_status + 12, received);
        if (!defer_checkpoint)
            tb_put32(target_status + 16, received);
        defer_checkpoint = false;
        *data = target_status;
        *size = sizeof target_status;
        return;
    }
    case TB_TARGET_FINISH:
        assert(request->size == 8 && tb_u64(request->data) == tb_u64(target_manifest));
        assert(tb_u32(target_status + 12) == tb_u32(target_manifest + 12));
        target_status[8] = TB_TARGET_VERIFIED;
        *data = target_status;
        *size = sizeof target_status;
        return;
    case TB_TARGET_ACTIVATE:
        assert(request->size == 8 && target_status[8] == TB_TARGET_VERIFIED);
        target_status[8] = TB_TARGET_TRIAL;
        target_status[9] = TB_TARGET_BOOT_TRIAL_PENDING;
        *data = target_status;
        *size = sizeof target_status;
        return;
    case TB_TARGET_STATUS:
        assert(request->size == 8 && tb_u64(request->data) == tb_u64(target_manifest));
        *data = target_status;
        *size = sizeof target_status;
        return;
    case TB_TARGET_ABORT:
        assert(request->size == 8 && target_status[8] < TB_TARGET_TRIAL);
        target_status[8] = TB_TARGET_ABORTED;
        *data = target_status;
        *size = sizeof target_status;
        return;
    default:
        assert(false);
    }
}

static size_t make_response(const struct tb_target_frame *request, uint8_t status, const uint8_t *data, size_t size,
                            uint8_t *wire, size_t capacity, bool spi)
{
    if (status != TB_TARGET_OK && mock_mode != 4) {
        data = NULL;
        size = 0;
    }
    if (mock_mode == 4) {
        data = hello;
        size = sizeof hello;
    }
    struct tb_target_frame response = {
        .op = request->op, .status = status, .sequence = request->sequence, .data = data, .size = size};
    if (spi)
        return tb_target_spi_encode(&response, wire);
    return tb_target_uart_encode(&response, wire, capacity);
}

enum tb_status tb_io_bus(const uint8_t *input, size_t size, uint8_t *output, size_t *length)
{
    assert(input && length && size >= 12);
    uint8_t protocol = input[0];
    size_t tx_size = tb_u16(input + 4), rx_size = tb_u16(input + 6);
    uint32_t timeout = tb_u16(input + 8);
    assert(!input[1] && !tb_u16(input + 2) && !tb_u16(input + 10));
    assert(size == 12 + tx_size && timeout && timeout <= 100);
    *length = 0;

    if (protocol == TB_UART) {
        if (tx_size) {
            struct tb_target_frame request = {0};
            uint8_t payload[TB_TARGET_PAYLOAD_MAX];
            assert(tb_target_uart_decode(input + 12, tx_size, &request, payload) == TB_OK);
            if (request.op == TB_TARGET_HELLO)
                assert(!request.size);
            if (mock_mode == 5) {
                mock_time += 100000;
                return TB_OK;
            }
            if (mock_mode == 1)
                request.sequence++;
            request_sequence = request.sequence;
            if (mock_mode == 3)
                tb_put32(hello + 4, TB_TARGET_CAPABILITY_MASK | 0x100);
            const uint8_t *data = NULL;
            size_t data_size = 0;
            uint8_t response_status = mock_status;
            if (response_status == TB_TARGET_OK && fail_target_begin && request.op == TB_TARGET_BEGIN)
                response_status = TB_TARGET_IO;
            if (response_status == TB_TARGET_OK)
                model_target(&request, &data, &data_size);
            mock_response_size =
                make_response(&request, response_status, data, data_size, mock_response, sizeof mock_response, false);
            mock_response_offset = 0;
            return TB_OK;
        }
        if (mock_mode == 2) {
            mock_time += (int64_t)timeout * 1000;
            return TB_OK;
        }
        assert(output && rx_size);
        size_t available = mock_response_size - mock_response_offset;
        size_t count = available < 5 ? available : 5;
        if (count > rx_size)
            count = rx_size;
        memcpy(output, mock_response + mock_response_offset, count);
        mock_response_offset += count;
        *length = count;
        mock_time += 500;
        return TB_OK;
    }

    assert(protocol == TB_SPI && tx_size == TB_TARGET_SPI_SIZE && rx_size == TB_TARGET_SPI_SIZE && output);
    struct tb_target_frame request = {0};
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    assert(tb_target_spi_decode(input + 12, tx_size, &request, payload) == TB_OK);
    uint8_t response[TB_TARGET_SPI_SIZE] = {0};
    if (mock_mode == 5) {
        mock_time += 100000;
        memcpy(output, response, sizeof response);
        *length = sizeof response;
        return TB_OK;
    }
    if (request.op == TB_TARGET_HELLO) {
        request_sequence = request.sequence;
        const uint8_t *data = NULL;
        size_t size = 0;
        if (mock_status == TB_TARGET_OK)
            model_target(&request, &data, &size);
        size_t response_size = make_response(&request, mock_status, data, size, pending_spi, sizeof pending_spi, true);
        assert(response_size == 20 + (mock_status == TB_TARGET_OK ? size : mock_mode == 4 ? sizeof hello : 0));
    } else {
        if (request.op == TB_TARGET_POLL) {
            assert(request.sequence == request_sequence);
        } else {
            request_sequence = request.sequence;
            const uint8_t *data = NULL;
            size_t size = 0;
            model_target(&request, &data, &size);
            make_response(&request, TB_TARGET_OK, data, size, pending_spi, sizeof pending_spi, true);
        }
        if (request.op == TB_TARGET_POLL && busy_polls) {
            struct tb_target_frame busy = {
                .op = TB_TARGET_HELLO, .status = TB_TARGET_BUSY, .sequence = request.sequence};
            tb_target_spi_encode(&busy, response);
            busy_polls--;
        } else {
            memcpy(response, pending_spi, sizeof response);
        }
    }
    memcpy(output, response, sizeof response);
    *length = sizeof response;
    mock_time += 1000;
    return TB_OK;
}

static void reset_mock(void)
{
    memset(tb_pin_config, 0, sizeof tb_pin_config);
    tb_pin_config[TB_UART].count = 2;
    tb_pin_config[TB_SPI].count = 4;
    mock_mode = 0;
    mock_status = TB_TARGET_OK;
    mock_response_size = 0;
    mock_response_offset = 0;
    busy_polls = 0;
    mock_time = 0;
    memset(target_manifest, 0, sizeof target_manifest);
    memset(target_image, 0, sizeof target_image);
    memset(target_status, 0, sizeof target_status);
    target_calls = 0;
    persisted = false;
    durable_size = 0;
    staged_size = 0;
    staged_write = false;
    staged_erase = false;
    valid_manifest = true;
    fail_save = false;
    fail_erase = false;
    fail_commit = false;
    fail_target_begin = false;
    defer_checkpoint = false;
    fill_hello();
    assert(tb_target_ota_init() == TB_OK && !tb_target_ota_busy());
}

static void make_manifest(uint64_t job_id, uint32_t image_size)
{
    memset(target_manifest, 0, sizeof target_manifest);
    tb_put64(target_manifest, job_id);
    target_manifest[8] = 1;
    tb_put32(target_manifest + 12, image_size);
    memset(target_manifest + 16, 0x37, 32);
    memcpy(target_manifest + 48, "2.0", 4);
    memcpy(target_manifest + 80, "test-mcu", 9);
    tb_put64(target_manifest + 112, 5);
    memset(target_manifest + 120, 0xa5, 64);
}

static enum tb_status write_target_chunk(uint64_t job_id, uint32_t offset, const uint8_t *bytes, size_t count,
                                         uint8_t *out, size_t *size)
{
    uint8_t request[12 + 983];
    tb_put64(request, job_id);
    tb_put32(request + 8, offset);
    memcpy(request + 12, bytes, count);
    return tb_target_ota_write(request, 12 + count, out, size);
}

static void exercise_target_update(void)
{
    uint8_t out[TB_TARGET_HELLO_SIZE], chunk[983];
    size_t size = 0;
    const uint64_t job_id = 0x1122334455667788ULL;
    memset(chunk, 0x5c, sizeof chunk);

    reset_mock();
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_OK && size == TB_TARGET_STATUS_SIZE);
    assert(out[8] == TB_TARGET_EMPTY);
    assert(tb_target_ota_status(0, out, &size) == TB_STATE && size == 0);
    make_manifest(job_id, 2000);
    memset(hello + 96, 0, 12);
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_INVALID);
    assert(!persisted && !tb_target_ota_busy());
    fill_hello();

    fail_save = true;
    unsigned calls = target_calls;
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_IO);
    assert(target_calls == calls + 1 && !persisted && tb_target_ota_busy());
    fail_save = false;
    assert(tb_target_ota_init() == TB_OK && !tb_target_ota_busy());

    reset_mock();
    make_manifest(job_id, 32);
    fail_target_begin = true;
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_IO);
    assert(persisted && tb_target_ota_busy() && !target_status[0]);
    fail_target_begin = false;
    assert(tb_target_ota_init() == TB_OK && tb_target_ota_busy());
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_OK);
    assert(out[8] == TB_TARGET_RECEIVING && persisted && tb_target_ota_busy());

    uint8_t saved_identity[12];
    memcpy(saved_identity, hello + 96, sizeof saved_identity);
    memset(hello + 96, 0x6b, sizeof saved_identity);
    assert(write_target_chunk(job_id, 0, chunk, 16, out, &size) == TB_CONFLICT);
    assert(tb_u32(target_status + 12) == 0 && persisted && tb_target_ota_busy());
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_CONFLICT && size == 0 && tb_target_ota_busy());
    memcpy(hello + 96, saved_identity, sizeof saved_identity);
    assert(write_target_chunk(job_id, 0, chunk, 16, out, &size) == TB_OK);
    assert(tb_target_ota_abort((uint8_t *)&job_id, sizeof job_id, out, &size) == TB_OK);
    assert(out[8] == TB_TARGET_ABORTED && !tb_target_ota_busy());

    reset_mock();
    make_manifest(job_id, 2000);

    valid_manifest = false;
    calls = target_calls;
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_INTEGRITY);
    assert(!persisted && target_calls == calls);
    valid_manifest = true;

    tb_put32(hello + 4, 0);
    enum tb_status rejected = tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size);
    if (rejected != TB_UNSUPPORTED)
        fprintf(stderr, "unsupported capability returned %d\n", rejected);
    assert(rejected == TB_UNSUPPORTED);
    assert(!persisted);
    tb_put32(hello + 4, TB_TARGET_CAPABILITY_MASK);

    fail_commit = true;
    calls = target_calls;
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_IO);
    assert(target_calls == calls + 1 && !persisted && tb_target_ota_busy());
    fail_commit = false;
    assert(tb_target_ota_init() == TB_OK && !tb_target_ota_busy());

    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_OK);
    assert(size == TB_TARGET_STATUS_SIZE && persisted && tb_target_ota_busy());
    assert(tb_target_ota_protocol() == TB_UART && out[8] == TB_TARGET_RECEIVING);
    assert(tb_target_ota_begin(TB_SPI, target_manifest, sizeof target_manifest, out, &size) == TB_BUSY);
    assert(tb_target_ota_init() == TB_OK && tb_target_ota_busy());

    assert(tb_target_ota_status(TB_SPI, out, &size) == TB_BUSY && size == 0 && tb_target_ota_busy());
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_OK && size == TB_TARGET_STATUS_SIZE);
    assert(out[8] == TB_TARGET_RECEIVING);
    defer_checkpoint = true;
    assert(write_target_chunk(job_id, 0, chunk, 512, out, &size) == TB_OK && size == TB_TARGET_STATUS_SIZE);
    assert(tb_u32(out + 12) == 512 && tb_u32(out + 16) == 0);
    assert(write_target_chunk(job_id, 0, chunk, 512, out, &size) == TB_OK);
    assert(tb_u32(out + 12) == 512 && tb_u32(out + 16) == 512);
    assert(write_target_chunk(job_id, 512, chunk, 983, out, &size) == TB_OK);
    assert(write_target_chunk(job_id, 1495, chunk, 505, out, &size) == TB_OK);
    assert(tb_target_ota_finish((uint8_t *)&job_id, sizeof job_id, out, &size) == TB_OK);
    assert(out[8] == TB_TARGET_VERIFIED);
    assert(tb_target_ota_activate((uint8_t *)&job_id, sizeof job_id, out, &size) == TB_OK);
    assert(out[8] == TB_TARGET_TRIAL && out[9] == TB_TARGET_BOOT_TRIAL_PENDING);
    assert(tb_target_ota_status(0, out, &size) == TB_OK && tb_target_ota_busy());

    target_status[8] = TB_TARGET_CONFIRMED;
    target_status[9] = TB_TARGET_BOOT_CONFIRMED;
    tb_put64(target_status + 24, 5);
    refresh_target_status();
    tb_put64(target_status + 24, 4);
    refresh_target_status();
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_CONFLICT && size == 0 && tb_target_ota_busy());
    tb_put64(target_status + 24, 5);
    refresh_target_status();
    fail_commit = true;
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_IO && tb_target_ota_busy() && persisted);
    fail_commit = false;
    fail_erase = true;
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_IO && tb_target_ota_busy() && persisted);
    fail_erase = false;
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_OK && size == TB_TARGET_STATUS_SIZE);
    assert(out[8] == TB_TARGET_CONFIRMED && !persisted && !tb_target_ota_busy());
    assert(tb_target_ota_status(TB_UART, out, &size) == TB_OK && size == TB_TARGET_STATUS_SIZE);
    assert(out[8] == TB_TARGET_CONFIRMED);

    reset_mock();
    make_manifest(job_id + 1, 32);
    assert(tb_target_ota_begin(TB_UART, target_manifest, sizeof target_manifest, out, &size) == TB_OK);
    fail_commit = true;
    assert(tb_target_ota_abort((uint8_t *)&(uint64_t){job_id + 1}, sizeof(uint64_t), out, &size) == TB_IO);
    assert(tb_target_ota_busy() && persisted);
    fail_commit = false;
    assert(tb_target_ota_abort((uint8_t *)&(uint64_t){job_id + 1}, sizeof(uint64_t), out, &size) == TB_OK);
    assert(size == TB_TARGET_STATUS_SIZE && out[8] == TB_TARGET_ABORTED);
    assert(!persisted && !tb_target_ota_busy());
}

int main(void)
{
    uint8_t out[TB_TARGET_HELLO_SIZE];
    size_t size = 0;
    struct tb_target_hello decoded;
    reset_mock();
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_OK && size == sizeof out);
    assert(tb_target_hello_decode(out, size, &decoded) == TB_OK && !strcmp(decoded.board_id, "test-mcu"));

    const struct
    {
        uint8_t target_status;
        enum tb_status rpc_status;
    } statuses[] = {{TB_TARGET_INVALID, TB_INVALID},         {TB_TARGET_BUSY, TB_BUSY},
                    {TB_TARGET_UNSUPPORTED, TB_UNSUPPORTED}, {TB_TARGET_IO, TB_IO},
                    {TB_TARGET_INTEGRITY, TB_INTEGRITY},     {TB_TARGET_STATE, TB_STATE},
                    {TB_TARGET_CONFLICT, TB_CONFLICT},       {TB_TARGET_TIMEOUT, TB_TIMEOUT}};
    for (size_t i = 0; i < sizeof statuses / sizeof statuses[0]; i++) {
        size = 123;
        mock_status = statuses[i].target_status;
        assert(tb_target_ota_info(TB_UART, out, &size) == statuses[i].rpc_status && size == 0);
    }
    mock_status = TB_TARGET_OK;
    mock_mode = 1;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_CONFLICT);
    mock_mode = 3;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_INVALID);
    mock_mode = 4;
    mock_status = TB_TARGET_BUSY;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_INVALID);
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_INVALID);
    mock_mode = 2;
    mock_status = TB_TARGET_OK;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_TIMEOUT);
    mock_mode = 5;
    assert(tb_target_ota_info(TB_UART, out, &size) == TB_TIMEOUT);

    reset_mock();
    busy_polls = 1;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_OK && size == sizeof out);
    assert(tb_target_hello_decode(out, size, &decoded) == TB_OK && decoded.rollback_index == 4);
    busy_polls = 1000;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_TIMEOUT);
    mock_mode = 5;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_TIMEOUT);
    tb_pin_config[TB_SPI].count = 0;
    assert(tb_target_ota_info(TB_SPI, out, &size) == TB_STATE);
    assert(tb_target_ota_info(TB_I2C, out, &size) == TB_INVALID);
    exercise_target_update();
    puts("target link: UART framing, SPI polling, status mapping and deadlines passed");
}
