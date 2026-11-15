set(setup_sources "")
if(CONFIG_PET_SETUP_PORTAL_ENABLED)
    list(APPEND setup_sources "src/setup_boot.c" "src/setup_service.c" "src/setup_policy.c"
        "src/setup_network.c" "src/setup_http.c" "src/setup_activation.c" "src/setup_activation_policy.c")
endif()
idf_component_register(SRCS ${setup_sources} INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include"
    REQUIRES "contracts" PRIV_REQUIRES "svc_config" "storage" "espressif__cjson" "esp_wifi"
    "esp_netif" "esp_event" "esp_http_server" "esp_http_client" "esp_timer" "esp_app_format" "mbedtls" "heap")
if(CONFIG_PET_SETUP_PORTAL_ENABLED)
    target_add_binary_data(${COMPONENT_LIB} "web/setup.html" TEXT)
endif()
