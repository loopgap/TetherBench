idf_component_register(
    SRCS "app_main.cpp" "probe.c" "probe_core.c" "probe_io.c" "probe_ota.c" "probe_wifi.c" "probe_service.c"
         "../third_party/CMSIS-DAP/Source/DAP.c" "../third_party/CMSIS-DAP/Source/SW_DP.c"
    INCLUDE_DIRS "." "../.local" "../third_party/CMSIS-DAP/Include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash app_update esp_https_server esp_timer
             esp_driver_gpio esp_driver_uart esp_driver_spi esp_driver_i2c mbedtls esp_hal_gpio
    EMBED_TXTFILES "../.local/server.crt" "../.local/server.key"
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11 cxx_std_17)
