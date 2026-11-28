idf_component_register(
    SRCS "probe.c" "probe_service.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../.local"
    REQUIRES esp_common probe_core probe_dap probe_io probe_link probe_ota probe_target_ota probe_wifi
    PRIV_REQUIRES app_update esp_app_format esp_hw_support esp_https_server esp_system esp_timer freertos lwip mbedtls nvs_flash
    EMBED_TXTFILES "../../.local/server.crt" "../../.local/server.key"
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
