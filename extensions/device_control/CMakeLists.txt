idf_component_register(
    SRCS
        "src/device_control.c"
        "src/command.c"
        "src/control_esp.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "contracts"
    PRIV_REQUIRES
        "log"
        "storage" "esp_http_client" "esp_timer" "mbedtls" "espressif__cjson"
)
