idf_component_register(
    SRCS
        "ble_service.c"
        "ble_security.c"
    INCLUDE_DIRS
        "include"
    REQUIRES bt freertos services drivers
)
