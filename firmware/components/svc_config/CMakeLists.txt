idf_component_register(SRCS "src/config_codec.c" "src/config_service.c" "src/config_worker.c"
    "src/provision.c" "src/config_console.c" "src/config_remote.c"
    INCLUDE_DIRS "include" REQUIRES "contracts" PRIV_REQUIRES "storage" "espressif__cjson")
