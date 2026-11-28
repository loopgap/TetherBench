# C5 SPI slave link

`probe_link` is the disabled-by-default C5 side of the `WT9932C5-TINY` P4
interconnect. An application must call `tb_service_start_with_link()` with
`TB_PROBE_LINK_PROFILE_WT_TINY_P4_C5_SPI_V1` (or call the component directly
with the same profile) before the component claims `SPI2_HOST` or GPIO24. The
normal `tb_service_start()` path selects `TB_PROBE_LINK_PROFILE_DISABLED`.
A disabled configuration returns `ESP_OK` without changing any pins; `xfer`
then returns `TB_STATE`.

The component uses the ESP-IDF 6.1.1 `spi_slave` driver with SPI mode 0 and
one fixed 1024-byte DMA transaction. The board profile is SCLK GPIO0, MOSI
GPIO1, MISO GPIO8, CS GPIO9, and IRQ GPIO24. IRQ is driven high while a
transaction is ready and low after it completes or fails. `xfer` sends the
response for the current transaction and decodes the request received from
the P4 master. Pass `NULL` as the response for the initial zero-filled
transaction.

The transport owns no heap memory and accepts timeouts from 1 through 1000
ms. It is a transport boundary only; it does not dispatch target operations
or start a service task. The caller must process the decoded request and call
`tb_probe_link_xfer()` again with the next response. `SPI2_HOST` is also used
by the existing C5 target-MCU master path, so the two profiles must not be
started at the same time. Once the profile is active, service-side `PINS`,
`BUS`, and target-OTA requests reject SPI2 or any request using the link GPIOs.
