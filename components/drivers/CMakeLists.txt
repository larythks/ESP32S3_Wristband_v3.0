idf_component_register(
    SRCS
        "common/i2c_bus.c"
        "mpu6050/mpu6050.c"
        "sh1106/sh1106.c"
        "max30102/max30102.c"
        "ds18b20/onewire.c"
        "ds18b20/ds18b20.c"
        "button/button.c"
    INCLUDE_DIRS
        "common/include"
        "mpu6050/include"
        "sh1106/include"
        "max30102/include"
        "ds18b20/include"
        "button/include"
    REQUIRES driver
)
