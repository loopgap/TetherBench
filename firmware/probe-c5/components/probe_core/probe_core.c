#include "probe_core.h"
#include <limits.h>
#include <string.h>

uint16_t tb_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
uint32_t tb_u32(const uint8_t *p)
{
    return (uint32_t)tb_u16(p) | (uint32_t)tb_u16(p + 2) << 16;
}
uint64_t tb_u64(const uint8_t *p)
{
    return (uint64_t)tb_u32(p) | (uint64_t)tb_u32(p + 4) << 32;
}
void tb_put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
void tb_put32(uint8_t *p, uint32_t n)
{
    tb_put16(p, (uint16_t)n);
    tb_put16(p + 2, (uint16_t)(n >> 16));
}
void tb_put64(uint8_t *p, uint64_t n)
{
    tb_put32(p, (uint32_t)n);
    tb_put32(p + 4, (uint32_t)(n >> 32));
}
enum tb_status tb_decode(const uint8_t *p, size_t n, struct tb_frame *f)
{
    if (n < TB_HEADER_SIZE || memcmp(p, "TBC5", 4) || p[4] != 1 || tb_u16(p + 6) != 0 ||
        tb_u32(p + 12) > TB_MAX_PAYLOAD || tb_u32(p + 12) != n - TB_HEADER_SIZE)
        return TB_INVALID;
    *f = (struct tb_frame){.op = p[5],
                           .request = tb_u32(p + 8),
                           .lease = tb_u64(p + 16),
                           .data = p + TB_HEADER_SIZE,
                           .size = n - TB_HEADER_SIZE};
    return TB_OK;
}
size_t tb_encode(uint8_t *p, const struct tb_frame *f, enum tb_status status, size_t size)
{
    if (size > TB_MAX_PAYLOAD)
        return 0;
    memcpy(p, "TBC5", 4);
    p[4] = 1;
    p[5] = f->op;
    tb_put16(p + 6, (uint16_t)(0x8000u | status));
    tb_put32(p + 8, f->request);
    tb_put32(p + 12, (uint32_t)size);
    tb_put64(p + 16, f->lease);
    return TB_HEADER_SIZE + size;
}
uint32_t tb_crc32(const uint8_t *data, size_t size)
{
    static const uint32_t nibble[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                        0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble[crc & 0xf];
        crc = (crc >> 4) ^ nibble[crc & 0xf];
    }
    return ~crc;
}
static size_t target_pack(const struct tb_target_frame *frame, uint8_t *packet, size_t capacity)
{
    if (!frame || !packet || frame->op > TB_TARGET_ABORT || frame->status > TB_TARGET_TIMEOUT || !frame->sequence ||
        frame->size > TB_TARGET_PAYLOAD_MAX || (frame->size && !frame->data) || capacity < 20 + frame->size)
        return 0;
    memcpy(packet, "TBUP", 4);
    packet[4] = 1;
    packet[5] = frame->op;
    packet[6] = frame->status;
    packet[7] = 0;
    tb_put32(packet + 8, frame->sequence);
    tb_put16(packet + 12, (uint16_t)frame->size);
    tb_put16(packet + 14, 0);
    if (frame->size)
        memcpy(packet + 16, frame->data, frame->size);
    tb_put32(packet + 16 + frame->size, tb_crc32(packet, 16 + frame->size));
    return 20 + frame->size;
}
static bool target_string_valid(const uint8_t *text, size_t size)
{
    const uint8_t *end = memchr(text, 0, size);
    if (!end)
        return false;
    for (const uint8_t *p = end + 1; p < text + size; p++)
        if (*p)
            return false;
    return true;
}
enum tb_status tb_target_status_decode(const uint8_t *wire, size_t size, struct tb_target_status_record *status)
{
    if (!wire || !status || size != TB_TARGET_STATUS_SIZE || tb_u16(wire + 10) || wire[8] > TB_TARGET_FAILED ||
        wire[9] > TB_TARGET_BOOT_ROLLED_BACK)
        return TB_INVALID;
    uint64_t job_id = tb_u64(wire);
    uint32_t received = tb_u32(wire + 12), checkpoint = tb_u32(wire + 16), total = tb_u32(wire + 20);
    if (!job_id) {
        for (size_t i = 0; i < size; i++)
            if (wire[i])
                return TB_INVALID;
    } else if (wire[8] == TB_TARGET_EMPTY || !total || received > total || checkpoint > received ||
               !target_string_valid(wire + 32, 32)) {
        return TB_INVALID;
    }
    *status = (struct tb_target_status_record){.job_id = job_id,
                                               .phase = wire[8],
                                               .boot_result = wire[9],
                                               .received = received,
                                               .checkpoint = checkpoint,
                                               .total = total,
                                               .rollback_index = tb_u64(wire + 24)};
    memcpy(status->active_version, wire + 32, sizeof status->active_version);
    return TB_OK;
}
enum tb_status tb_target_hello_decode(const uint8_t *wire, size_t size, struct tb_target_hello *hello)
{
    if (!wire || !hello || size != TB_TARGET_HELLO_SIZE || tb_u16(wire + 14) ||
        (tb_u32(wire + 4) & ~TB_TARGET_CAPABILITY_MASK) || !tb_u32(wire + 8) || !tb_u16(wire + 12) ||
        tb_u16(wire + 12) > 983 || !target_string_valid(wire + 16, 32) || !target_string_valid(wire + 48, 32) ||
        !target_string_valid(wire + 80, 16))
        return TB_INVALID;
    if (tb_u16(wire) != 1)
        return TB_UNSUPPORTED;
    struct tb_target_hello result = {.protocol_major = tb_u16(wire),
                                     .protocol_minor = tb_u16(wire + 2),
                                     .capabilities = tb_u32(wire + 4),
                                     .max_image_length = tb_u32(wire + 8),
                                     .max_data_chunk = tb_u16(wire + 12),
                                     .rollback_index = tb_u64(wire + 108)};
    memcpy(result.board_id, wire + 16, sizeof result.board_id);
    memcpy(result.active_version, wire + 48, sizeof result.active_version);
    memcpy(result.bootloader_version, wire + 80, sizeof result.bootloader_version);
    memcpy(result.unique_id, wire + 96, sizeof result.unique_id);
    enum tb_status status = tb_target_status_decode(wire + 116, TB_TARGET_STATUS_SIZE, &result.update);
    if (status != TB_OK)
        return status;
    *hello = result;
    return TB_OK;
}
static enum tb_status target_parse(const uint8_t *packet, size_t size, struct tb_target_frame *frame,
                                   uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    if (!packet || !frame || !payload || size < 20 || size > 20 + TB_TARGET_PAYLOAD_MAX || memcmp(packet, "TBUP", 4) ||
        packet[4] != 1 || packet[5] > TB_TARGET_ABORT || packet[6] > TB_TARGET_TIMEOUT || packet[7] ||
        tb_u16(packet + 14))
        return TB_INVALID;
    size_t payload_size = tb_u16(packet + 12);
    if (payload_size > TB_TARGET_PAYLOAD_MAX || size != 20 + payload_size || !tb_u32(packet + 8))
        return TB_INVALID;
    if (tb_u32(packet + 16 + payload_size) != tb_crc32(packet, 16 + payload_size))
        return TB_INTEGRITY;
    if (payload_size)
        memcpy(payload, packet + 16, payload_size);
    *frame = (struct tb_target_frame){
        .op = packet[5], .status = packet[6], .sequence = tb_u32(packet + 8), .data = payload, .size = payload_size};
    return TB_OK;
}
static size_t cobs_encode(const uint8_t *input, size_t size, uint8_t *output, size_t capacity)
{
    if (!capacity)
        return 0;
    size_t code_index = 0, write = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < size; i++) {
        if (!input[i]) {
            output[code_index] = code;
            code_index = write++;
            code = 1;
        } else {
            if (write >= capacity)
                return 0;
            output[write++] = input[i];
            if (++code == 0xff) {
                output[code_index] = code;
                code_index = write++;
                code = 1;
            }
        }
        if (write > capacity)
            return 0;
    }
    if (write >= capacity)
        return 0;
    output[code_index] = code;
    output[write++] = 0;
    return write;
}
static size_t cobs_decode(const uint8_t *input, size_t size, uint8_t *output, size_t capacity)
{
    size_t read = 0, write = 0;
    while (read < size) {
        uint8_t code = input[read++];
        if (!code || (size_t)(code - 1) > size - read || (size_t)(code - 1) > capacity - write)
            return 0;
        for (unsigned i = 1; i < code; i++) {
            uint8_t value = input[read++];
            if (!value)
                return 0;
            output[write++] = value;
        }
        if (code != 0xff && read < size) {
            if (write == capacity)
                return 0;
            output[write++] = 0;
        }
    }
    return write;
}
size_t tb_target_uart_encode(const struct tb_target_frame *frame, uint8_t *wire, size_t capacity)
{
    uint8_t packet[20 + TB_TARGET_PAYLOAD_MAX];
    size_t packet_size = target_pack(frame, packet, sizeof packet);
    if (!packet_size || capacity > TB_TARGET_UART_MAX)
        capacity = TB_TARGET_UART_MAX;
    if (!packet_size || !wire)
        return 0;
    return cobs_encode(packet, packet_size, wire, capacity);
}
enum tb_status tb_target_uart_decode(const uint8_t *wire, size_t size, struct tb_target_frame *frame,
                                     uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    if (!wire || !size || size > TB_TARGET_UART_MAX || wire[size - 1])
        return TB_INVALID;
    uint8_t packet[20 + TB_TARGET_PAYLOAD_MAX];
    size_t packet_size = cobs_decode(wire, size - 1, packet, sizeof packet);
    return packet_size ? target_parse(packet, packet_size, frame, payload) : TB_INVALID;
}
size_t tb_target_spi_encode(const struct tb_target_frame *frame, uint8_t wire[TB_TARGET_SPI_SIZE])
{
    if (!wire)
        return 0;
    memset(wire, 0, TB_TARGET_SPI_SIZE);
    return target_pack(frame, wire, TB_TARGET_SPI_SIZE);
}
enum tb_status tb_target_spi_decode(const uint8_t *wire, size_t size, struct tb_target_frame *frame,
                                    uint8_t payload[TB_TARGET_PAYLOAD_MAX])
{
    if (!wire || size != TB_TARGET_SPI_SIZE || !frame)
        return TB_INVALID;
    size_t payload_size = tb_u16(wire + 12);
    size_t packet_size = 20 + payload_size;
    if (payload_size > TB_TARGET_PAYLOAD_MAX || packet_size > size)
        return TB_INVALID;
    for (size_t i = packet_size; i < size; i++)
        if (wire[i])
            return TB_INVALID;
    return target_parse(wire, packet_size, frame, payload);
}
bool tb_owned(const struct tb_lease *l, uint64_t now, uint64_t id)
{
    return id && id == l->id && now < l->deadline;
}
enum tb_status tb_acquire(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl)
{
    if (!id || ttl < 5000 || ttl > 60000 || now > UINT64_MAX - ttl)
        return TB_INVALID;
    if (l->id && now < l->deadline)
        return TB_BUSY;
    l->id = id;
    l->deadline = now + ttl;
    return TB_OK;
}
enum tb_status tb_renew(struct tb_lease *l, uint64_t now, uint64_t id, uint32_t ttl)
{
    if (!tb_owned(l, now, id))
        return TB_LEASE;
    if (ttl < 5000 || ttl > 60000 || now > UINT64_MAX - ttl)
        return TB_INVALID;
    l->deadline = now + ttl;
    return TB_OK;
}
enum tb_status tb_validate_pins(const struct tb_pins *p, uint32_t allowed, uint32_t occupied)
{
    static const uint32_t maximum[] = {0, 4000000, 5000000, 20000000, 1000000};
    static const uint8_t minimum[] = {0, 2, 2, 4, 2};
    if (p->protocol < 1 || p->protocol >= TB_PROTOCOL_COUNT)
        return TB_INVALID;
    if (!p->count) {
        if (p->hz)
            return TB_INVALID;
        for (unsigned i = 0; i < 4; i++)
            if (p->pin[i] != -1)
                return TB_INVALID;
        return TB_OK;
    }
    if (p->count < minimum[p->protocol] || p->count > 4 || !p->hz || p->hz > maximum[p->protocol] ||
        (p->protocol == TB_SWD && p->hz < 1000) || (p->protocol == TB_UART && p->count != 2 && p->count != 4) ||
        (p->protocol == TB_I2C && p->count != 2))
        return TB_INVALID;
    uint32_t used = occupied;
    for (unsigned i = 0; i < 4; i++) {
        if (i >= p->count) {
            if (p->pin[i] != -1)
                return TB_INVALID;
            continue;
        }
        if (p->pin[i] < 0 || p->pin[i] > 28)
            return TB_INVALID;
        uint32_t bit = 1u << (unsigned)p->pin[i];
        if (!(allowed & bit) || (used & bit))
            return TB_CONFLICT;
        used |= bit;
    }
    if (p->protocol == TB_SWD && p->count > 3)
        return TB_INVALID;
    return TB_OK;
}

static bool dap_command(const uint8_t *p, size_t n, size_t *used, size_t *out, uint32_t max_hz)
{
    if (!n)
        return false;
    size_t need = 0, response = 2;
    switch (p[0]) {
    case 0x00:
        need = 2;
        response = 258;
        break;
    case 0x01:
        need = 3;
        break;
    case 0x02:
        need = 2;
        if (n >= 2 && p[1] > 1)
            return false;
        break;
    case 0x03:
        need = 1;
        break;
    case 0x04:
        need = 6;
        if (n >= 6 && p[1] > 32)
            return false;
        break;
    case 0x05: {
        if (n < 3 || p[1] != 0)
            return false;
        need = 3;
        response = 3;
        for (unsigned i = 0; i < p[2]; i++) {
            if (need >= n)
                return false;
            uint8_t r = p[need++];
            if (r & 0x40 || ((r & 0x10) && !(r & 0x02)) || ((r & 0x20) && (r & 0x02)))
                return false;
            if (!(r & 2) || (r & 0x10))
                need += 4;
            if ((r & 2) && !(r & 0x10))
                response += 4;
            if (r & 0x80)
                response += 4;
            if (need > n)
                return false;
        }
        break;
    }
    case 0x06: {
        if (n < 5 || p[1] != 0 || !tb_u16(p + 2) || tb_u16(p + 2) > 255 || p[4] & 0xf0)
            return false;
        size_t count = tb_u16(p + 2);
        need = 5;
        response = 4;
        if (p[4] & 2)
            response += count * 4;
        else
            need += count * 4;
        break;
    }
    case 0x08:
        need = 6;
        break;
    case 0x09:
        need = 3;
        if (n >= 3 && tb_u16(p + 1) > 10000)
            return false;
        break;
    case 0x0a:
        need = 1;
        response = 3;
        break;
    case 0x10:
        need = 7;
        if (n >= 7 && ((p[2] & ~0x83u) || tb_u32(p + 3) > 10000))
            return false;
        break;
    case 0x11:
        need = 5;
        if (n >= 5 && (tb_u32(p + 1) < 1000 || tb_u32(p + 1) > max_hz))
            return false;
        break;
    case 0x12:
        if (n < 2)
            return false;
        need = 2 + ((p[1] ? p[1] : 256) + 7u) / 8;
        break;
    case 0x13:
        need = 2;
        if (n >= 2 && p[1] & 0xf8)
            return false;
        break;
    case 0x1d:
        if (n < 2 || !p[1] || p[1] > 64)
            return false;
        need = 2;
        for (unsigned i = 0; i < p[1]; i++) {
            if (need >= n)
                return false;
            uint8_t info = p[need++];
            if (info & 0x40)
                return false;
            size_t bytes = ((info & 63) ? (info & 63) : 64) + 7u;
            bytes /= 8;
            if (info & 0x80)
                response += bytes;
            else
                need += bytes;
            if (need > n)
                return false;
        }
        break;
    default:
        return false;
    }
    if (need > n || response > TB_DAP_PACKET)
        return false;
    *used = need;
    *out = response;
    return true;
}
bool tb_dap_validate(const uint8_t *p, size_t n, uint32_t max_hz)
{
    if (!n || n > TB_DAP_PACKET)
        return false;
    size_t used = 0, out = 0;
    if (p[0] != 0x7f)
        return dap_command(p, n, &used, &out, max_hz) && used == n;
    if (n < 2 || !p[1] || p[1] > 16)
        return false;
    size_t pos = 2, total = 2;
    for (unsigned i = 0; i < p[1]; i++) {
        if (!dap_command(p + pos, n - pos, &used, &out, max_hz))
            return false;
        pos += used;
        total += out;
        if (total > TB_DAP_PACKET)
            return false;
    }
    return pos == n;
}
int tb_link_score(const struct tb_link *local, const struct tb_link *peer, uint64_t now)
{
    const struct tb_link *links[] = {local, peer};
    int worst = INT_MAX;
    for (unsigned i = 0; i < 2; i++) {
        const struct tb_link *l = links[i];
        if (l->measured > now || now - l->measured > 30000 || l->samples < 5 || l->rssi < -110 || l->rssi > -1 ||
            l->loss_permille > 1000 || (l->rtt_ms != UINT16_MAX && l->rtt_ms > 5000))
            return INT_MIN;
        int rssi = l->rssi < -90 ? 0 : l->rssi > -40 ? 100 : 2 * (l->rssi + 90);
        int score = rssi - (int)l->loss_permille / 10 - (l->rtt_ms == UINT16_MAX ? 0 : (int)l->rtt_ms / 5);
        if (score < worst)
            worst = score;
    }
    return worst;
}
int8_t tb_worst_rssi(const int8_t *samples, size_t count)
{
    if (!samples || !count)
        return -127;
    int8_t worst = samples[0];
    for (size_t i = 1; i < count; i++)
        if (samples[i] < worst)
            worst = samples[i];
    return worst;
}
bool tb_scan_merge(struct tb_scan_ap *records, size_t *count, size_t capacity, const struct tb_scan_ap *sample)
{
    if (!records || !count || !sample || !capacity || *count > capacity || sample->sightings != 1)
        return false;
    size_t index = 0;
    while (index < *count &&
           (records[index].channel != sample->channel || memcmp(records[index].bssid, sample->bssid, 6)))
        index++;
    if (index == *count) {
        if (*count == capacity)
            return false;
        records[index] = *sample;
        (*count)++;
        return true;
    }
    struct tb_scan_ap *record = &records[index];
    if (record->sightings >= 5)
        return true;
    record->rssi[record->sightings++] = sample->rssi[0];
    record->wifi6 = (uint8_t)(record->wifi6 && sample->wifi6);
    return true;
}
int8_t tb_scan_median_rssi(const struct tb_scan_ap *record)
{
    if (!record || !record->sightings || record->sightings > 5)
        return -127;
    int8_t sorted[5];
    memcpy(sorted, record->rssi, record->sightings);
    for (unsigned i = 1; i < record->sightings; i++) {
        int8_t value = sorted[i];
        unsigned j = i;
        while (j && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }
    return sorted[record->sightings / 2];
}
enum tb_status tb_band_prepare(struct tb_band *b, uint64_t now, uint64_t id, uint8_t channel, uint8_t previous,
                               const struct tb_link current[2], const struct tb_link next[2])
{
    if (b->stage != TB_BAND_IDLE)
        return TB_BUSY;
    if (!id || !((channel >= 1 && channel <= 11) || channel == 36 || channel == 40 || channel == 44 || channel == 48) ||
        channel == previous)
        return TB_INVALID;
    if (current[0].rtt_ms == UINT16_MAX || current[1].rtt_ms == UINT16_MAX)
        return TB_INVALID;
    int old = tb_link_score(current, current + 1, now), candidate = tb_link_score(next, next + 1, now);
    if (old == INT_MIN || candidate == INT_MIN)
        return TB_INVALID;
    if (candidate < old + 10 || (b->last_switch && now - b->last_switch < 30000))
        return TB_STATE;
    *b = (struct tb_band){.stage = TB_BAND_PREPARED,
                          .id = id,
                          .deadline = now + 5000,
                          .last_switch = b->last_switch,
                          .previous_last_switch = b->last_switch,
                          .channel = channel,
                          .previous = previous};
    return TB_OK;
}
enum tb_status tb_band_commit(struct tb_band *b, uint64_t now, uint64_t id, uint32_t delay_ms)
{
    if (b->stage != TB_BAND_PREPARED || b->id != id || now >= b->deadline)
        return TB_STATE;
    if (delay_ms < 500 || delay_ms > 3000)
        return TB_INVALID;
    b->stage = TB_BAND_COMMITTED;
    b->deadline = now + delay_ms;
    return TB_OK;
}
enum tb_status tb_band_confirm(struct tb_band *b, uint64_t now, uint64_t id, bool ready, uint8_t channel, int rssi,
                               bool he20)
{
    if (b->stage != TB_BAND_SWITCHED || b->id != id || now >= b->deadline || !ready || channel != b->channel ||
        rssi < TB_BAND_CONFIRM_MIN_RSSI || !he20)
        return TB_STATE;
    b->stage = TB_BAND_CONFIRMED;
    b->previous_last_switch = b->last_switch;
    b->last_switch = now;
    b->deadline = now + 15000;
    b->rollback_until = b->deadline;
    return TB_OK;
}
enum tb_status tb_band_finalize(struct tb_band *b, uint64_t now, uint64_t id)
{
    if (!b || !id || b->id != id)
        return TB_INVALID;
    if (b->stage == TB_BAND_IDLE && b->confirmed)
        return TB_OK;
    if (b->stage != TB_BAND_CONFIRMED || now >= b->deadline)
        return TB_STATE;
    b->stage = TB_BAND_IDLE;
    b->confirmed = true;
    b->rollback_until = now + 15000;
    return TB_OK;
}
enum tb_status tb_band_cancel(struct tb_band *b, uint64_t now, uint64_t id, bool *restore)
{
    if (!b || !restore || !id || b->id != id)
        return TB_INVALID;
    *restore = false;
    if (b->stage == TB_BAND_IDLE && b->confirmed) {
        if (now >= b->rollback_until)
            return TB_STATE;
        *restore = true;
    } else if (b->stage == TB_BAND_SWITCHED || b->stage == TB_BAND_CONFIRMED)
        *restore = true;
    else if (b->stage == TB_BAND_IDLE)
        return TB_OK;
    b->stage = TB_BAND_IDLE;
    b->confirmed = false;
    b->rollback_until = 0;
    if (*restore)
        b->last_switch = b->previous_last_switch;
    return TB_OK;
}
int tb_band_tick(struct tb_band *b, uint64_t now)
{
    if (b->stage == TB_BAND_IDLE && b->confirmed && now >= b->rollback_until) {
        b->confirmed = false;
        b->rollback_until = 0;
    }
    if (b->stage == TB_BAND_IDLE || now < b->deadline)
        return 0;
    if (b->stage == TB_BAND_PREPARED) {
        b->stage = TB_BAND_IDLE;
        return 0;
    }
    if (b->stage == TB_BAND_COMMITTED) {
        b->stage = TB_BAND_SWITCHED;
        b->deadline = now + 15000;
        return 1;
    }
    if (b->stage == TB_BAND_CONFIRMED) {
        b->stage = TB_BAND_IDLE;
        b->last_switch = b->previous_last_switch;
        b->rollback_until = 0;
        return -1;
    }
    b->stage = TB_BAND_IDLE;
    return -1;
}
