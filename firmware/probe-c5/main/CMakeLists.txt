idf_component_register(
    SRCS "app_main.cpp" "probe.c"
    INCLUDE_DIRS "."
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11 cxx_std_17)
