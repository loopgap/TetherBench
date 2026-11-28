idf_component_register(
    SRCS "probe_link.c"
    INCLUDE_DIRS "include"
    REQUIRES probe_core esp_driver_gpio esp_driver_spi
    PRIV_REQUIRES freertos
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
