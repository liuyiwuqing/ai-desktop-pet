idf_component_register(
    SRCS
        "src/servo.c"
        "src/servo_engine.c"
        "src/servo_esp.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "contracts"
    PRIV_REQUIRES
        "log"
        "pet_hal" "storage" "esp_driver_ledc" "esp_timer"
)
