idf_component_register(
    SRCS "ds18b20.c" "onewire.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
