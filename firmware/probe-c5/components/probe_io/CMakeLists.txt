idf_component_register(
    SRCS "probe_io.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private" "../../.local"
    REQUIRES probe_core soc esp_driver_gpio
    PRIV_REQUIRES esp_driver_i2c esp_driver_spi esp_driver_uart freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
