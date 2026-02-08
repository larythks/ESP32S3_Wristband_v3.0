idf_component_register(
    SRCS
        "event_bus/event_bus.c"
        "sensor_service/sensor_service.c"
        "health_monitor/health_monitor.c"
        "pedometer/pedometer.c"
        "fall_detect/fall_detect.c"
    INCLUDE_DIRS
        "event_bus/include"
        "sensor_service/include"
        "health_monitor/include"
        "pedometer/include"
        "fall_detect/include"
    REQUIRES freertos driver drivers esp_timer
)
