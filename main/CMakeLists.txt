idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "app_task.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_common
        log
    PRIV_REQUIRES
        bsp
        ble
        ota
        freertos
)