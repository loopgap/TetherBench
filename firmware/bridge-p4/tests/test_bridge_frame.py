import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class BridgeFrameTests(unittest.TestCase):
    def test_c_implementation_rejects_bad_version_crc_and_length(self):
        harness = r'''
#include "bridge_frame.h"
#include <string.h>
int main(void) {
    struct tb_bridge_frame frame = {.opcode = TB_BRIDGE_PING, .sequence = 7, .length = 1, .payload = {TB_BRIDGE_OK}};
    unsigned char wire[TB_BRIDGE_FRAME_BYTES];
    tb_bridge_encode(&frame, wire);
    struct tb_bridge_frame out;
    if (!tb_bridge_decode(wire, &out) || out.sequence != 7 || out.payload[0] != TB_BRIDGE_OK) return 1;
    wire[0] = 2; if (tb_bridge_decode(wire, &out)) return 2;
    tb_bridge_encode(&frame, wire); wire[7] ^= 1; if (tb_bridge_decode(wire, &out)) return 3;
    tb_bridge_encode(&frame, wire); wire[4] = 25; wire[5] = 0;
    unsigned short crc = tb_bridge_crc16(wire, TB_BRIDGE_FRAME_BYTES - 2);
    wire[30] = (unsigned char)crc; wire[31] = (unsigned char)(crc >> 8);
    if (tb_bridge_decode(wire, &out)) return 4;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "test.c"
            binary = pathlib.Path(directory) / "test.exe"
            source.write_text(harness, encoding="ascii")
            compiler = "gcc"
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "main"),
                            str(ROOT / "main" / "bridge_frame.c"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_c_stream_helper_handles_split_and_consecutive_frames(self):
        harness = r'''
#include "bridge_frame.h"
#include <string.h>
static int count;
static bool take(const unsigned char wire[TB_BRIDGE_FRAME_BYTES], void *context) {
    (void)context;
    struct tb_bridge_frame frame;
    if (!tb_bridge_decode(wire, &frame)) return false;
    count += frame.sequence == 1 || frame.sequence == 2;
    return true;
}
static bool reject(const unsigned char wire[TB_BRIDGE_FRAME_BYTES], void *context) {
    (void)wire; (void)context;
    return false;
}
int main(void) {
    struct tb_bridge_frame one = {.opcode = TB_BRIDGE_PING, .sequence = 1, .length = 1};
    struct tb_bridge_frame two = {.opcode = TB_BRIDGE_PING, .sequence = 2, .length = 1};
    unsigned char a[TB_BRIDGE_FRAME_BYTES], b[TB_BRIDGE_FRAME_BYTES], stream[64];
    tb_bridge_encode(&one, a); tb_bridge_encode(&two, b); memcpy(stream, a, 32); memcpy(stream + 32, b, 32);
    struct tb_bridge_stream state = {0};
    if (!tb_bridge_stream_feed(&state, stream, 7, take, NULL) || state.used != 7) return 1;
    if (!tb_bridge_stream_feed(&state, stream + 7, 57, take, NULL) || state.used != 0 || count != 2) return 2;
    state = (struct tb_bridge_stream){0};
    if (tb_bridge_stream_feed(&state, a, TB_BRIDGE_FRAME_BYTES, reject, NULL) || state.used != 0) return 3;
    if (!tb_bridge_stream_feed(&state, b, TB_BRIDGE_FRAME_BYTES, take, NULL) || state.used != 0 || count != 3) return 4;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "stream_test.c"
            binary = pathlib.Path(directory) / "stream_test.exe"
            source.write_text(harness, encoding="ascii")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "main"),
                            str(ROOT / "main" / "bridge_frame.c"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
