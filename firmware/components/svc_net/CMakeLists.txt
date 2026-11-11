set(net_srcs "src/net_policy.c")
if(CONFIG_PET_MODULE_NET_ENABLED)
    list(APPEND net_srcs "src/net_service.c")

    if(CONFIG_PET_NET_FAKE)
        list(APPEND net_srcs "src/net_fake.c")
    else()
        list(APPEND net_srcs "src/net_esp.c")
    endif()
endif()

# REQUIRES 必须在早期展开阶段保持无条件，否则 IDF 依赖图会漏掉仅由本组件
# 引入的 esp_wifi/esp_netif，导致编译时找不到头文件。
idf_component_register(
    SRCS
        ${net_srcs}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "private_include"
    REQUIRES
        "contracts"
        "pet_hal"
        "core"
        "esp_timer"
        "esp_wifi"
        "esp_netif"
        "esp_event"
        "esp_app_format"
        "mqtt"
        "mbedtls"
)
