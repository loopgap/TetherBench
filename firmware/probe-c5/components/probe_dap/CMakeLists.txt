idf_component_register(
    SRCS "probe_dap.c" "../../third_party/CMSIS-DAP/Source/DAP.c" "../../third_party/CMSIS-DAP/Source/SW_DP.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private" "../../third_party/CMSIS-DAP/Include" "../probe_io/private"
    REQUIRES probe_core probe_io esp_timer
)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
