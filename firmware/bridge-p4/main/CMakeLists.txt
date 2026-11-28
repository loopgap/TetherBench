idf_component_register(SRCS "main.c" "bridge_frame.c"
    INCLUDE_DIRS "."
    REQUIRES esp_tinyusb driver freertos esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
