idf_component_register(SRCS "probe_core.c" INCLUDE_DIRS "include")
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_11)
