idf_component_register(
    SRCS "probe_wifi.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../.local"
    REQUIRES esp_common esp_event esp_netif esp_timer esp_wifi probe_core
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
