idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES drivers ui_manager services ble_gatt nvs_flash
)
