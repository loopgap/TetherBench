idf_component_register(
    SRCS "probe_target_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES probe_core probe_io probe_ota
    PRIV_REQUIRES esp_hw_support esp_timer nvs_flash
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
