#pragma once
#include <string.h>
#include "cmsis_compiler.h"
#ifdef TB_HOST_TEST
#include "tests_dap_io.h"
#else
#include "probe_io.h"
#endif

#define CPU_CLOCK 240000000u
#define IO_PORT_WRITE_CYCLES 2u
#define DAP_SWD 1
#define DAP_JTAG 0
#define DAP_JTAG_DEV_CNT 0
#define DAP_DEFAULT_PORT 1u
#define DAP_DEFAULT_SWJ_CLOCK 1000000u
#define DAP_PACKET_SIZE TB_DAP_PACKET
#define DAP_PACKET_COUNT 1u
#define SWO_UART 0
#define SWO_MANCHESTER 0
#define SWO_STREAM 0
#define SWO_BUFFER_SIZE 0
#define TIMESTAMP_CLOCK 1000000u
#define DAP_UART 0
#define DAP_UART_USB_COM_PORT 0
#define TARGET_DEVICE_FIXED 0
#define TARGET_BOARD_FIXED 0
#define PIN_SWCLK_TCK_IN() tb_gpio_read(0)
#define PIN_SWCLK_TCK_SET() tb_gpio_write(0,1)
#define PIN_SWCLK_TCK_CLR() tb_gpio_write(0,0)
#define PIN_SWDIO_TMS_IN() tb_gpio_read(1)
#define PIN_SWDIO_TMS_SET() tb_gpio_write(1,1)
#define PIN_SWDIO_TMS_CLR() tb_gpio_write(1,0)
#define PIN_SWDIO_IN() tb_gpio_read(1)
#define PIN_SWDIO_OUT(n) tb_gpio_write(1,n)
#define PIN_SWDIO_OUT_ENABLE() tb_swd_direction(true)
#define PIN_SWDIO_OUT_DISABLE() tb_swd_direction(false)
#define PIN_nRESET_IN() tb_gpio_read(2)
#define PIN_nRESET_OUT(n) tb_gpio_write(2,n)
#define PIN_TDI_IN() 0u
#define PIN_TDO_IN() 0u
#define PIN_nTRST_IN() 1u
#define PIN_TDI_OUT(n) ((void)(n))
#define PIN_nTRST_OUT(n) ((void)(n))
#define PORT_SWD_SETUP() tb_swd_on()
#define PORT_JTAG_SETUP() ((void)0)
#define PORT_OFF() tb_swd_off()
#define LED_CONNECTED_OUT(n) ((void)(n))
#define LED_RUNNING_OUT(n) ((void)(n))
#define TIMESTAMP_GET() tb_timestamp()
#define DAP_SETUP() ((void)0)
#define RESET_TARGET() 0u

__STATIC_INLINE uint8_t DAP_GetVendorString(char *s) { strcpy(s,"TetherBench"); return 12; }
__STATIC_INLINE uint8_t DAP_GetProductString(char *s) { strcpy(s,"C5 wireless CMSIS-DAP"); return 22; }
__STATIC_INLINE uint8_t DAP_GetSerNumString(char *s) { (void)s; return 0; }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char *s) { (void)s; return 0; }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char *s) { (void)s; return 0; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char *s) { (void)s; return 0; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char *s) { (void)s; return 0; }
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char *s) { (void)s; return 0; }
