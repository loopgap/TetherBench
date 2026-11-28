idf_component_register(SRCS "main.c" "bridge_frame.c"
    INCLUDE_DIRS "."
    REQUIRES esp_tinyusb driver freertos esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)

if(TB_BRIDGE_USB_PORT STREQUAL "full")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TB_BRIDGE_USB_FULL_SPEED=1)
elseif(TB_BRIDGE_USB_PORT AND NOT TB_BRIDGE_USB_PORT STREQUAL "high")
    message(FATAL_ERROR "TB_BRIDGE_USB_PORT must be 'high' or 'full'")
endif()
