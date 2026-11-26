idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES log probe_service
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11 cxx_std_17)
