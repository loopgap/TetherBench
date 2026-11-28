#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TB_BRIDGE_FRAME_BYTES 32u
#define TB_BRIDGE_PAYLOAD_BYTES 24u
#define TB_BRIDGE_VERSION 1u
#define TB_BRIDGE_STREAM_INTERFACES 1u

enum tb_bridge_opcode {
    TB_BRIDGE_DAP_REQUEST = 1,
    TB_BRIDGE_DAP_RESPONSE = 2,
    TB_BRIDGE_PING = 3,
    TB_BRIDGE_ERROR = 0x7f,
};

enum tb_bridge_status {
    TB_BRIDGE_OK = 0,
    TB_BRIDGE_BAD_VERSION = 1,
    TB_BRIDGE_BAD_LENGTH = 2,
    TB_BRIDGE_QUEUE_FULL = 3,
    TB_BRIDGE_LINK_OFFLINE = 4,
};

struct tb_bridge_frame {
    uint8_t version;
    uint8_t opcode;
    uint16_t sequence;
    uint16_t length;
    uint8_t payload[TB_BRIDGE_PAYLOAD_BYTES];
    uint16_t crc16;
};

struct tb_bridge_stream {
    uint8_t bytes[TB_BRIDGE_FRAME_BYTES];
    size_t used;
};

typedef bool (*tb_bridge_frame_sink)(const uint8_t wire[TB_BRIDGE_FRAME_BYTES], void *context);

bool tb_bridge_decode(const uint8_t wire[TB_BRIDGE_FRAME_BYTES], struct tb_bridge_frame *out);
void tb_bridge_encode(const struct tb_bridge_frame *frame, uint8_t wire[TB_BRIDGE_FRAME_BYTES]);
uint16_t tb_bridge_crc16(const uint8_t *data, size_t length);
bool tb_bridge_stream_feed(struct tb_bridge_stream *stream, const uint8_t *data, size_t length,
                           tb_bridge_frame_sink sink, void *context);
