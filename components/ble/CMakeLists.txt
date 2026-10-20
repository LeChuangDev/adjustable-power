idf_component_register(
    SRCS
        "ble_transport.c"
        "ble_protocol.c"
        "ble_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        bt
        log
    PRIV_REQUIRES
        freertos
)