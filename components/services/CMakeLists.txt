idf_component_register(
    SRCS
        "event_bus/event_bus.c"
        "sensor_service/sensor_service.c"
        "health_monitor/health_monitor.c"
        "pedometer/pedometer.c"
        "fall_detect/fall_detect.c"
        "alarm_manager/alarm_manager.c"
    INCLUDE_DIRS
        "event_bus/include"
        "sensor_service/include"
        "health_monitor/include"
        "pedometer/include"
        "fall_detect/include"
        "alarm_manager/include"
    REQUIRES freertos driver drivers esp_timer
    PRIV_REQUIRES ble_gatt
)
