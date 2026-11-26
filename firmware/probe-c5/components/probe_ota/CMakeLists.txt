idf_component_register(
    SRCS "probe_ota.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../.local"
    REQUIRES esp_common probe_core
    PRIV_REQUIRES app_update esp_app_format esp_partition mbedtls nvs_flash
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
