idf_component_register(
    SRCS
        "event_bus/event_bus.c"
        "sensor_service/sensor_service.c"
        "health_monitor/health_monitor.c"
        "pedometer/pedometer.c"
        "fall_detect/fall_detect.c"
        "alarm_manager/alarm_manager.c"
        "voice_cmd/voice_cmd.c"
    INCLUDE_DIRS
        "event_bus/include"
        "sensor_service/include"
        "health_monitor/include"
        "pedometer/include"
        "fall_detect/include"
        "alarm_manager/include"
        "voice_cmd/include"
    REQUIRES freertos driver drivers esp_timer espressif__esp-sr
    PRIV_REQUIRES ble_gatt
)
