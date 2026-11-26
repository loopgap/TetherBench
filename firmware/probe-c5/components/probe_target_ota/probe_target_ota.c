#include "probe_target_ota.h"
#include "probe_io.h"
#include "probe_ota.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define TB_TARGET_TIMEOUT_MS 100u
#define TB_TARGET_JOURNAL_SIZE 208u
#define TB_TARGET_JOURNAL_MANIFEST 8u
#define TB_TARGET_JOURNAL_IDENTITY 192u
#define TB_TARGET_JOURNAL_CRC 204u
#define TB_TARGET_JOURNAL_MAGIC 0x314f4254u
#define TB_TARGET_JOURNAL_VERSION 1u
#define TB_TARGET_UPDATE_CAPABILITIES TB_TARGET_CAPABILITY_MASK

static uint32_t sequence;
static uint8_t journal[TB_TARGET_JOURNAL_SIZE];
static nvs_handle_t storage;
static bool storage_fault = true;

static bool journal_locked(void)
{
    return tb_u32(journal) == TB_TARGET_JOURNAL_MAGIC && journal[4] == TB_TARGET_JOURNAL_VERSION;
}

static bool journal_valid(const uint8_t data[TB_TARGET_JOURNAL_SIZE])
{
    uint8_t identity = 0;
    for (size_t i = 0; i < 12; i++)
        identity |= data[TB_TARGET_JOURNAL_IDENTITY + i];
    return identity && tb_u32(data) == TB_TARGET_JOURNAL_MAGIC && data[4] == TB_TARGET_JOURNAL_VERSION &&
           (data[5] == TB_UART || data[5] == TB_SPI) && tb_u16(data + 6) &&
           tb_u16(data + 6) <= TB_TARGET_PAYLOAD_MAX - 14 &&
           tb_u32(data + TB_TARGET_JOURNAL_CRC) == tb_crc32(data, TB_TARGET_JOURNAL_CRC) &&
           tb_ota_validate_target_manifest(data + TB_TARGET_JOURNAL_MANIFEST, TB_TARGET_MANIFEST_SIZE) == TB_OK;
}

static const uint8_t *journal_manifest(void)
{
    return journal + TB_TARGET_JOURNAL_MANIFEST;
}

enum tb_status tb_target_ota_init(void)
{
    memset(journal, 0, sizeof journal);
    storage_fault = true;
    if (nvs_flash_init_partition("ota_journal") != ESP_OK ||
        nvs_open_from_partition("ota_journal", "target", NVS_READWRITE, &storage) != ESP_OK)
        return TB_IO;

    uint8_t loaded[TB_TARGET_JOURNAL_SIZE];
    size_t size = sizeof loaded;
    esp_err_t result = nvs_get_blob(storage, "job", loaded, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        storage_fault = false;
        return TB_OK;
    }
    if (result != ESP_OK || size != sizeof loaded || !journal_valid(loaded))
        return result == ESP_OK ? TB_INTEGRITY : TB_IO;

    memcpy(journal, loaded, sizeof journal);
    storage_fault = false;
    return TB_OK;
}

bool tb_target_ota_busy(void)
{
    return storage_fault || journal_locked();
}

uint8_t tb_target_ota_protocol(void)
{
    return !storage_fault && journal_locked() ? journal[5] : 0;
}

static enum tb_status save_lock(uint8_t protocol, uint16_t max_chunk, const uint8_t *manifest,
                                const uint8_t identity[12])
{
    uint8_t data[TB_TARGET_JOURNAL_SIZE] = {0};
    tb_put32(data, TB_TARGET_JOURNAL_MAGIC);
    data[4] = TB_TARGET_JOURNAL_VERSION;
    data[5] = protocol;
    tb_put16(data + 6, max_chunk);
    memcpy(data + TB_TARGET_JOURNAL_MANIFEST, manifest, TB_TARGET_MANIFEST_SIZE);
    memcpy(data + TB_TARGET_JOURNAL_IDENTITY, identity, 12);
    tb_put32(data + TB_TARGET_JOURNAL_CRC, tb_crc32(data, TB_TARGET_JOURNAL_CRC));
    if (nvs_set_blob(storage, "job", data, sizeof data) != ESP_OK || nvs_commit(storage) != ESP_OK) {
        storage_fault = true;
        return TB_IO;
    }
    memcpy(journal, data, sizeof journal);
    return TB_OK;
}

static enum tb_status clear_lock(void)
{
    esp_err_t erased = nvs_erase_key(storage, "job");
    if (erased != ESP_OK && erased != ESP_ERR_NVS_NOT_FOUND)
        return TB_IO;
    if (erased == ESP_OK && nvs_commit(storage) != ESP_OK)
        return TB_IO;
    memset(journal, 0, sizeof journal);
    return TB_OK;
}

static uint32_t next_sequence(void)
{
    if (!sequence) {
        sequence = esp_random();
        if (!sequence)
            sequence = 1;
    }
    uint32_t current = sequence++;
    if (!sequence)
        sequence = 1;
    return current;
}

static enum tb_status bus_call(uint8_t protocol, const uint8_t *tx, size_t tx_size, uint8_t *rx, size_t rx_size,
                               uint32_t timeout_ms, size_t *received)
{
    if (!received || tx_size > TB_TARGET_UART_MAX || rx_size > TB_TARGET_UART_MAX || (!tx_size && !rx_size) ||
        (tx_size && !tx) || (rx_size && !rx) || !timeout_ms || timeout_ms > 100)
        return TB_INVALID;
    uint8_t input[12 + TB_TARGET_UART_MAX] = {0};
    input[0] = protocol;
    tb_put16(input + 4, (uint16_t)tx_size);
    tb_put16(input + 6, (uint16_t)rx_size);
    tb_put16(input + 8, (uint16_t)timeout_ms);
    if (tx_size)
        memcpy(input + 12, tx, tx_size);
    return tb_io_bus(input, 12 + tx_size, rx, received);
}

static enum tb_status target_status(enum tb_target_status status)
{
    switch (status) {
    case TB_TARGET_OK:
        return TB_OK;
    case TB_TARGET_INVALID:
        return TB_INVALID;
    case TB_TARGET_BUSY:
        return TB_BUSY;
    case TB_TARGET_UNSUPPORTED:
        return TB_UNSUPPORTED;
    case TB_TARGET_IO:
        return TB_IO;
    case TB_TARGET_INTEGRITY:
        return TB_INTEGRITY;
    case TB_TARGET_STATE:
        return TB_STATE;
    case TB_TARGET_CONFLICT:
        return TB_CONFLICT;
    case TB_TARGET_TIMEOUT:
        return TB_TIMEOUT;
    default:
        return TB_INVALID;
    }
}

static enum tb_status check_response(const struct tb_target_frame *response, uint32_t expected_sequence,
                                     uint8_t expected_op)
{
    if (!response)
        return TB_INVALID;
    if (response->sequence != expected_sequence)
        return TB_CONFLICT;
    if (response->op != expected_op)
        return TB_INVALID;
    if (response->status != TB_TARGET_OK && response->size)
        return TB_INVALID;
    return target_status((enum tb_target_status)response->status);
}

static uint32_t remaining_ms(uint64_t deadline)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (now >= deadline)
        return 0;
    uint64_t remaining = (deadline - now) / 1000;
    return remaining > TB_TARGET_TIMEOUT_MS ? TB_TARGET_TIMEOUT_MS : (uint32_t)remaining;
}

static enum tb_status uart_exchange(const struct tb_target_frame *request, struct tb_target_frame *response,
                                    uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    uint8_t tx[TB_TARGET_UART_MAX], rx[TB_TARGET_UART_MAX];
    size_t tx_size = tb_target_uart_encode(request, tx, sizeof tx), received = 0;
    if (!tx_size)
        return TB_INVALID;
    uint64_t deadline = (uint64_t)esp_timer_get_time() + TB_TARGET_TIMEOUT_MS * 1000u;
    uint32_t timeout = remaining_ms(deadline);
    if (!timeout)
        return TB_TIMEOUT;
    enum tb_status status = bus_call(TB_UART, tx, tx_size, NULL, 0, timeout, &received);
    if (status != TB_OK)
        return status;
    if (!remaining_ms(deadline))
        return TB_TIMEOUT;
    while (received < sizeof rx) {
        timeout = remaining_ms(deadline);
        if (!timeout)
            return TB_TIMEOUT;
        size_t count = 0;
        status = bus_call(TB_UART, NULL, 0, rx + received, sizeof rx - received, timeout, &count);
        if (status != TB_OK)
            return status;
        if (!remaining_ms(deadline))
            return TB_TIMEOUT;
        if (count > sizeof rx - received)
            return TB_IO;
        received += count;
        uint8_t *delimiter = memchr(rx, 0, received);
        if (delimiter) {
            if (delimiter != rx + received - 1)
                return TB_INVALID;
            status = tb_target_uart_decode(rx, received, response, payload);
            return status == TB_OK ? check_response(response, request->sequence, request->op) : status;
        }
    }
    return TB_INVALID;
}

static enum tb_status spi_exchange(const struct tb_target_frame *request, struct tb_target_frame *response,
                                   uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    uint8_t tx[TB_TARGET_SPI_SIZE], rx[TB_TARGET_SPI_SIZE];
    if (tb_target_spi_encode(request, tx) != 20 + request->size)
        return TB_INVALID;
    uint64_t deadline = (uint64_t)esp_timer_get_time() + TB_TARGET_TIMEOUT_MS * 1000u;
    uint32_t timeout = remaining_ms(deadline);
    if (!timeout)
        return TB_TIMEOUT;
    size_t received = 0;
    enum tb_status status = bus_call(TB_SPI, tx, sizeof tx, rx, sizeof rx, timeout, &received);
    if (status != TB_OK)
        return status;
    if (!remaining_ms(deadline))
        return TB_TIMEOUT;
    if (received != sizeof rx)
        return TB_IO;

    const struct tb_target_frame poll = {.op = TB_TARGET_POLL, .sequence = request->sequence};
    while (true) {
        timeout = remaining_ms(deadline);
        if (!timeout)
            return TB_TIMEOUT;
        if (tb_target_spi_encode(&poll, tx) != 20)
            return TB_INVALID;
        received = 0;
        status = bus_call(TB_SPI, tx, sizeof tx, rx, sizeof rx, timeout, &received);
        if (status != TB_OK)
            return status;
        if (!remaining_ms(deadline))
            return TB_TIMEOUT;
        if (received != sizeof rx)
            return TB_IO;
        status = tb_target_spi_decode(rx, sizeof rx, response, payload);
        if (status != TB_OK)
            return status;
        status = check_response(response, request->sequence, request->op);
        if (status != TB_BUSY || response->size)
            return status;
    }
}

static enum tb_status exchange(uint8_t protocol, uint8_t op, const uint8_t *data, size_t data_size,
                               uint8_t payload[TB_TARGET_PAYLOAD_MAX], size_t *payload_size)
{
    if (!payload || !payload_size || (data_size && !data) || data_size > TB_TARGET_PAYLOAD_MAX ||
        (protocol != TB_UART && protocol != TB_SPI) || !tb_pin_config[protocol].count)
        return TB_STATE;
    *payload_size = 0;
    struct tb_target_frame request = {.op = op, .sequence = next_sequence(), .data = data, .size = data_size};
    struct tb_target_frame response = {0};
    enum tb_status status =
        protocol == TB_UART ? uart_exchange(&request, &response, payload) : spi_exchange(&request, &response, payload);
    if (response.sequence == request.sequence && response.op == request.op)
        *payload_size = response.size;
    return status;
}

enum tb_status tb_target_ota_info(uint8_t protocol, uint8_t *out, size_t *size)
{
    if (!out || !size || (protocol != TB_UART && protocol != TB_SPI))
        return TB_INVALID;
    *size = 0;
    if (storage_fault || (journal_locked() && protocol != journal[5]))
        return storage_fault ? TB_IO : TB_BUSY;
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    size_t payload_size = 0;
    enum tb_status status = exchange(protocol, TB_TARGET_HELLO, NULL, 0, payload, &payload_size);
    if (status != TB_OK)
        return payload_size ? TB_INVALID : status;
    if (payload_size != TB_TARGET_HELLO_SIZE)
        return TB_INVALID;
    struct tb_target_hello hello;
    status = tb_target_hello_decode(payload, payload_size, &hello);
    if (status != TB_OK)
        return status;
    memcpy(out, payload, payload_size);
    *size = payload_size;
    return TB_OK;
}

static bool terminal_phase(uint8_t phase)
{
    return phase == TB_TARGET_CONFIRMED || phase == TB_TARGET_ABORTED || phase == TB_TARGET_ROLLED_BACK;
}

static uint64_t journal_job_id(void)
{
    return tb_u64(journal_manifest());
}

static enum tb_status check_locked_target_identity(void)
{
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    size_t payload_size = 0;
    enum tb_status status = exchange(journal[5], TB_TARGET_HELLO, NULL, 0, payload, &payload_size);
    if (status != TB_OK)
        return status;
    if (payload_size != TB_TARGET_HELLO_SIZE)
        return TB_INVALID;

    struct tb_target_hello hello;
    status = tb_target_hello_decode(payload, payload_size, &hello);
    if (status != TB_OK)
        return status;
    return memcmp(hello.unique_id, journal + TB_TARGET_JOURNAL_IDENTITY, 12) ? TB_CONFLICT : TB_OK;
}

static enum tb_status update_status(uint8_t op, const uint8_t *data, size_t data_size, uint8_t *out, size_t *size,
                                    struct tb_target_status_record *record)
{
    uint8_t payload[TB_TARGET_PAYLOAD_MAX];
    size_t payload_size = 0;
    enum tb_status status = check_locked_target_identity();
    if (status != TB_OK)
        return status;
    status = exchange(journal[5], op, data, data_size, payload, &payload_size);
    if (status != TB_OK)
        return status;
    if (payload_size != TB_TARGET_STATUS_SIZE)
        return TB_INVALID;

    status = tb_target_status_decode(payload, payload_size, record);
    if (status != TB_OK)
        return status;
    if (record->job_id != journal_job_id())
        return TB_CONFLICT;
    if (record->total != tb_u32(journal_manifest() + 12) || record->rollback_index > tb_u64(journal_manifest() + 112))
        return TB_CONFLICT;
    if (out)
        memcpy(out, payload, payload_size);
    if (size)
        *size = payload_size;
    return TB_OK;
}

static enum tb_status clear_terminal(const struct tb_target_status_record *record)
{
    return terminal_phase(record->phase) ? clear_lock() : TB_OK;
}

static enum tb_status check_target(const uint8_t *manifest, const struct tb_target_hello *hello, bool resume)
{
    uint8_t identity = 0;
    for (size_t i = 0; i < sizeof hello->unique_id; i++)
        identity |= hello->unique_id[i];
    if (!identity)
        return TB_INVALID;
    if ((hello->capabilities & TB_TARGET_UPDATE_CAPABILITIES) != TB_TARGET_UPDATE_CAPABILITIES)
        return TB_UNSUPPORTED;
    if (strcmp((const char *)manifest + 80, hello->board_id) || tb_u32(manifest + 12) > hello->max_image_length)
        return TB_INVALID;
    if (!resume && tb_u64(manifest + 112) <= hello->rollback_index)
        return TB_INTEGRITY;
    return TB_OK;
}

enum tb_status tb_target_ota_begin(uint8_t protocol, const uint8_t *data, size_t data_size, uint8_t *out, size_t *size)
{
    if (!data || !out || !size || data_size != TB_TARGET_MANIFEST_SIZE || (protocol != TB_UART && protocol != TB_SPI))
        return TB_INVALID;
    *size = 0;
    if (storage_fault)
        return TB_IO;
    enum tb_status validation = tb_ota_validate_target_manifest(data, data_size);
    if (validation != TB_OK)
        return validation;

    bool resume = journal_locked();
    if (resume && protocol != journal[5])
        return TB_BUSY;
    if (resume && memcmp(data, journal_manifest(), TB_TARGET_MANIFEST_SIZE))
        return TB_CONFLICT;

    uint8_t wire[TB_TARGET_HELLO_SIZE];
    size_t wire_size = 0;
    enum tb_status status = tb_target_ota_info(protocol, wire, &wire_size);
    if (status != TB_OK)
        return status;
    struct tb_target_hello hello;
    status = tb_target_hello_decode(wire, wire_size, &hello);
    if (status != TB_OK)
        return status;
    if (resume && memcmp(hello.unique_id, journal + TB_TARGET_JOURNAL_IDENTITY, 12))
        return TB_CONFLICT;
    status = check_target(data, &hello, resume && hello.update.job_id != 0);
    if (status != TB_OK)
        return status;

    uint64_t id = tb_u64(data);
    bool active = hello.update.job_id && !terminal_phase(hello.update.phase);
    if (hello.update.phase == TB_TARGET_FAILED || (active && hello.update.job_id != id))
        return TB_BUSY;
    if (!resume && hello.update.job_id == id && terminal_phase(hello.update.phase))
        return TB_CONFLICT;
    if (resume && hello.update.job_id && hello.update.job_id != id)
        return TB_CONFLICT;
    if (resume && terminal_phase(hello.update.phase)) {
        if (hello.update.total != tb_u32(data + 12) || hello.update.rollback_index > tb_u64(data + 112))
            return TB_CONFLICT;
        memcpy(out, wire + 116, TB_TARGET_STATUS_SIZE);
        status = clear_lock();
        if (status == TB_OK)
            *size = TB_TARGET_STATUS_SIZE;
        return status;
    }
    if (resume && hello.update.phase == TB_TARGET_TRIAL)
        return TB_STATE;
    if (!resume && active && hello.update.phase == TB_TARGET_TRIAL)
        return TB_BUSY;

    uint16_t max_chunk = resume ? tb_u16(journal + 6) : hello.max_data_chunk;
    if (hello.max_data_chunk < max_chunk)
        return TB_UNSUPPORTED;
    if (!resume) {
        status = save_lock(protocol, max_chunk, data, hello.unique_id);
        if (status != TB_OK)
            return status;
    }

    struct tb_target_status_record result;
    status = update_status(TB_TARGET_BEGIN, data, data_size, out, size, &result);
    if (status != TB_OK)
        return status;
    if (result.phase != TB_TARGET_RECEIVING && result.phase != TB_TARGET_VERIFIED) {
        *size = 0;
        return TB_STATE;
    }
    return TB_OK;
}

enum tb_status tb_target_ota_write(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size)
{
    if (!data || !out || !size || data_size < 13)
        return TB_INVALID;
    *size = 0;
    if (storage_fault || !journal_locked())
        return TB_STATE;
    size_t bytes = data_size - 12;
    uint32_t offset = tb_u32(data + 8), total = tb_u32(journal_manifest() + 12);
    if (bytes > tb_u16(journal + 6) || tb_u64(data) != journal_job_id() || bytes > TB_TARGET_PAYLOAD_MAX - 14 ||
        offset > total || bytes > total - offset)
        return TB_INVALID;

    uint8_t request[TB_TARGET_PAYLOAD_MAX];
    tb_put64(request, journal_job_id());
    tb_put32(request + 8, offset);
    tb_put16(request + 12, (uint16_t)bytes);
    memcpy(request + 14, data + 12, bytes);
    struct tb_target_status_record result;
    enum tb_status status = update_status(TB_TARGET_WRITE, request, 14 + bytes, out, size, &result);
    if (status == TB_OK && (result.phase != TB_TARGET_RECEIVING || result.received < offset + bytes)) {
        *size = 0;
        return TB_STATE;
    }
    return status;
}

static enum tb_status id_operation(uint8_t op, const uint8_t *data, size_t data_size, uint8_t phase, uint8_t *out,
                                   size_t *size)
{
    if (!data || !out || !size || data_size != 8 || tb_u64(data) != journal_job_id())
        return TB_INVALID;
    *size = 0;
    if (storage_fault || !journal_locked())
        return TB_STATE;
    struct tb_target_status_record result;
    enum tb_status status = update_status(op, data, data_size, out, size, &result);
    if (status == TB_OK && result.phase != phase) {
        *size = 0;
        return TB_STATE;
    }
    return status;
}

enum tb_status tb_target_ota_finish(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size)
{
    return id_operation(TB_TARGET_FINISH, data, data_size, TB_TARGET_VERIFIED, out, size);
}

enum tb_status tb_target_ota_activate(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size)
{
    enum tb_status status = id_operation(TB_TARGET_ACTIVATE, data, data_size, TB_TARGET_TRIAL, out, size);
    if (status == TB_OK && out[9] != TB_TARGET_BOOT_TRIAL_PENDING) {
        *size = 0;
        return TB_STATE;
    }
    return status;
}

enum tb_status tb_target_ota_status(uint8_t *out, size_t *size)
{
    if (!out || !size)
        return TB_INVALID;
    *size = 0;
    if (storage_fault || !journal_locked())
        return storage_fault ? TB_IO : TB_STATE;
    uint8_t request[8];
    tb_put64(request, journal_job_id());
    struct tb_target_status_record result;
    enum tb_status status = update_status(TB_TARGET_STATUS, request, sizeof request, out, size, &result);
    if (status != TB_OK)
        return status;
    status = clear_terminal(&result);
    if (status != TB_OK)
        *size = 0;
    return status;
}

enum tb_status tb_target_ota_abort(const uint8_t *data, size_t data_size, uint8_t *out, size_t *size)
{
    if (!data || !out || !size || data_size != 8 || tb_u64(data) != journal_job_id())
        return TB_INVALID;
    *size = 0;
    if (storage_fault || !journal_locked())
        return storage_fault ? TB_IO : TB_STATE;

    uint8_t request[8];
    tb_put64(request, journal_job_id());
    struct tb_target_status_record current;
    enum tb_status status = update_status(TB_TARGET_STATUS, request, sizeof request, out, size, &current);
    if (status != TB_OK)
        return status;
    if (terminal_phase(current.phase)) {
        status = clear_terminal(&current);
        if (status != TB_OK)
            *size = 0;
        return status;
    }
    if (current.phase != TB_TARGET_RECEIVING && current.phase != TB_TARGET_VERIFIED)
        return TB_STATE;

    struct tb_target_status_record result;
    status = update_status(TB_TARGET_ABORT, data, data_size, out, size, &result);
    if (status != TB_OK)
        return status;
    if (result.phase != TB_TARGET_ABORTED) {
        *size = 0;
        return TB_STATE;
    }
    status = clear_terminal(&result);
    if (status != TB_OK)
        *size = 0;
    return status;
}
