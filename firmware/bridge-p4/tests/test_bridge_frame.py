import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class BridgeFrameTests(unittest.TestCase):
    def test_c_link_is_bounded_and_fails_closed(self):
        harness = r'''
#include "bridge_link.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define SPI_DMA_CH_AUTO 1

static int irq_level;
static int cs_level = 1;
static int reset_level;
static int fail_bus;
static int fail_transfer;
static int bus_ready;
static int device_ready;
static int dma_arg;
static int removed;
static int freed;
static unsigned delay_count;

int gpio_reset_pin(int pin) { if (pin == 19) cs_level = 0; if (pin == 21) reset_level = 0; return 0; }
int gpio_set_level(int pin, unsigned level) { if (pin == 19) cs_level = (int)level; if (pin == 21) reset_level = (int)level; return 0; }
int gpio_set_direction(int pin, int mode) { (void)pin; (void)mode; return 0; }
int gpio_set_pull_mode(int pin, int mode) { (void)pin; (void)mode; return 0; }
int gpio_get_level(int pin) { return pin == 20 ? irq_level : pin == 19 ? cs_level : pin == 21 ? reset_level : 0; }
void vTaskDelay(unsigned ticks) { delay_count += ticks; }

int spi_bus_initialize(int host, const void *config, int dma) {
    (void)host; (void)config; (void)dma;
    dma_arg = dma;
    if (fail_bus) return -1;
    bus_ready = 1; return 0;
}
int spi_bus_free(int host) { (void)host; bus_ready = 0; freed++; return 0; }
int spi_bus_add_device(int host, const void *config, void **out) {
    (void)host; (void)config;
    device_ready = 1; *out = (void *)1; return 0;
}
int spi_bus_remove_device(void *handle) { assert(handle == (void *)1); device_ready = 0; removed++; return 0; }
int spi_device_polling_transmit(void *handle, void *transaction) {
    struct tx { unsigned length, rxlength; const uint8_t *tx_buffer; uint8_t *rx_buffer; };
    struct tx *t = transaction;
    assert(handle == (void *)1 && t->length == 8192 && t->rxlength == 8192);
    if (fail_transfer) return -1;
    memcpy(t->rx_buffer, t->tx_buffer, TB_BRIDGE_LINK_TRANSACTION_BYTES); cs_level = 1; return 0;
}

int main(void) {
    uint8_t tx[TB_BRIDGE_LINK_TRANSACTION_BYTES] = {1, 2, 3}, rx[TB_BRIDGE_LINK_TRANSACTION_BYTES] = {0};
    assert(!tb_bridge_link_is_online());
    assert(!tb_bridge_link_transfer(tx, rx));
    fail_bus = 1;
    assert(!tb_bridge_link_init() && !bus_ready && reset_level == 0 && cs_level == 1);
    fail_bus = 0;
    assert(tb_bridge_link_init() && bus_ready && device_ready && dma_arg == SPI_DMA_CH_AUTO && reset_level == 1 && cs_level == 1);
    assert(delay_count >= 11 && !tb_bridge_link_is_online());
    assert(!tb_bridge_link_transfer(tx, rx));
    irq_level = 1;
    assert(tb_bridge_link_is_online() && tb_bridge_link_transfer(tx, rx));
    assert(!memcmp(tx, rx, sizeof(tx)));
    fail_transfer = 1;
    assert(!tb_bridge_link_transfer(tx, rx) && !bus_ready && !device_ready && reset_level == 0 && cs_level == 1);
    tb_bridge_link_stop();
    assert(removed >= 1 && freed >= 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "driver").mkdir()
            (root / "freertos").mkdir()
            (root / "esp_attr.h").write_text("#pragma once\n#define DMA_ATTR\n", encoding="ascii")
            (root / "driver" / "gpio.h").write_text(r'''
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
enum { GPIO_NUM_16 = 16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_21 };
enum { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2, GPIO_MODE_OUTPUT_OD = 3, GPIO_FLOATING = 0 };
esp_err_t gpio_reset_pin(int); esp_err_t gpio_set_level(int, unsigned); esp_err_t gpio_set_direction(int, int);
esp_err_t gpio_set_pull_mode(int, int); int gpio_get_level(int);
''', encoding="ascii")
            (root / "driver" / "spi_master.h").write_text(r'''
#pragma once
#include <stddef.h>
typedef int esp_err_t;
typedef int spi_host_device_t;
typedef void *spi_device_handle_t;
#define ESP_OK 0
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 1
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; } spi_bus_config_t;
typedef struct { int mode, clock_speed_hz, spics_io_num, queue_size; } spi_device_interface_config_t;
typedef struct { unsigned length, rxlength; const void *tx_buffer; void *rx_buffer; } spi_transaction_t;
esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t *, int);
esp_err_t spi_bus_free(spi_host_device_t);
esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t *, spi_device_handle_t *);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t *);
''', encoding="ascii")
            (root / "freertos" / "FreeRTOS.h").write_text("#pragma once\n#define pdMS_TO_TICKS(x) (x)\n", encoding="ascii")
            (root / "freertos" / "task.h").write_text("#pragma once\nvoid vTaskDelay(unsigned);\n", encoding="ascii")
            source = root / "link_test.c"
            binary = root / "link_test.exe"
            source.write_text(harness, encoding="ascii")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root), "-I", str(ROOT / "main"),
                            str(ROOT / "main" / "bridge_link.c"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

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
