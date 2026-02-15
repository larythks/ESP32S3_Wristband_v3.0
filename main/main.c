#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "mpu6050.h"
#include "sh1106.h"
#include "max30102.h"
#include "ds18b20.h"
#include "button.h"
#include "ui_manager.h"
#include "event_bus.h"
#include "sensor_service.h"
#include "health_monitor.h"
#include "pedometer.h"
#include "fall_detect.h"
#include "ble_service.h"
#include "ble_gatt_defs.h"
#include "nvs_flash.h"

static const char *TAG = "main";

/**
 * @brief 跌倒检测事件处理回调
 */
static void fall_detected_handler(const event_t *event, void *user_data)
{
    const health_alert_t *alert = &event->data.health_alert;
    ESP_LOGW(TAG, "!!! FALL DETECTED !!! Peak SVM: %.1fg, Time: %lu",
             alert->value / 10.0f, (unsigned long)alert->timestamp);
    // TODO: 后续迭代中触发报警状态机
}

/**
 * @brief 传感器数据事件处理回调
 */
static void sensor_data_handler(const event_t *event, void *user_data)
{
    const sensor_data_t *data = &event->data.sensor;

    // 处理跌倒检测（每次 IMU 数据更新时调用）
    if (data->data_valid & SENSOR_IMU) {
        fall_detect_process(data->accel_x, data->accel_y, data->accel_z);
    }

    // 每 5 秒打印一次传感器数据
    static uint32_t last_print = 0;
    if ((data->timestamp - last_print) >= 5000) {
        ESP_LOGI(TAG, "[SENSOR] temp=%.1f ax=%d ay=%d az=%d",
                 data->temperature,
                 data->accel_x, data->accel_y, data->accel_z);
        last_print = data->timestamp;
    }
}

/**
 * @brief SW1 按键回调（报警键）
 */
static void sw1_callback(button_id_t id, button_event_t event)
{
    if (event == BUTTON_EVENT_SHORT_PRESS) {
        ESP_LOGI(TAG, "SW1 short press - Manual alarm triggered");

        /* 递增事件 ID（掉电重置，MVP 可接受） */
        static uint32_t alarm_event_id = 0;
        alarm_event_id++;

        /* 组装 BLE Alarm 数据包 */
        ble_alarm_t alarm = {0};
        alarm.event_id  = alarm_event_id;
        alarm.alarm_type = BLE_ALARM_TYPE_MANUAL;
        alarm.value     = 0;
        alarm.battery   = BLE_BATTERY_DEFAULT;
        alarm.timestamp = ble_get_unix_timestamp();

        /* 发送 BLE Alarm Notify */
        esp_err_t ret = ble_notify_alarm(&alarm);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Alarm notify sent: id=%lu type=%d",
                     (unsigned long)alarm.event_id, alarm.alarm_type);
        } else {
            ESP_LOGW(TAG, "Alarm notify failed: %s (not connected?)",
                     esp_err_to_name(ret));
        }

        /* 发布 EVT_ALARM_STATE 到事件总线 */
        event_data_t evt_data = {0};
        evt_data.health_alert.type      = ALERT_TYPE_NONE;
        evt_data.health_alert.level     = ALERT_LEVEL_ALARM;
        evt_data.health_alert.value     = 0;
        evt_data.health_alert.timestamp = alarm.timestamp;
        event_publish(EVT_ALARM_STATE, &evt_data);

    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        ESP_LOGI(TAG, "SW1 long press - Reserved for cancel alarm");
    }
}

/**
 * @brief SW2 按键回调（交互键）
 */
static void sw2_callback(button_id_t id, button_event_t event)
{
    if (event == BUTTON_EVENT_SHORT_PRESS) {
        ESP_LOGI(TAG, "SW2 short press - Next page");
        ui_next_page();
    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        if (ui_is_manual_measuring()) {
            ESP_LOGI(TAG, "SW2 long press - Exit manual measure");
            ui_exit_manual_measure();
        } else {
            ESP_LOGI(TAG, "SW2 long press - Enter manual measure");
            ui_enter_manual_measure();
            sensor_start_hr_measure();
        }
    }
}

/**
 * @brief BLE 连接状态事件处理回调
 */
static void ble_conn_handler(const event_t *event, void *user_data)
{
    uint32_t state = event->data.raw[0];
    if (state == BLE_CONN_STATE_CONNECTED) {
        ESP_LOGI(TAG, "BLE: Client connected");
    } else {
        ESP_LOGI(TAG, "BLE: Client disconnected");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Wristband v3.0 ===");
    ESP_LOGI(TAG, "Starting system initialization...");

    // 初始化 NVS Flash（BLE bonding 存储依赖）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash erase and re-init");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed!");
        return;
    }

    // 初始化 I2C 总线
    ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed!");
        return;
    }

    // 扫描 I2C 设备
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_bus_scan();

    // 初始化 MPU6050
    ret = mpu6050_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 init failed!");
    }

    // 初始化 SH1106 OLED
    ret = sh1106_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SH1106 init failed!");
    }

    // 初始化 MAX30102
    ret = max30102_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MAX30102 init failed!");
    }

    // 初始化 DS18B20
    ret = ds18b20_init(DS18B20_DEFAULT_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS18B20 init failed!");
    }

    // 初始化按键驱动
    ret = button_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed!");
    } else {
        // 注册按键回调
        button_register_cb(BUTTON_ID_SW1, BUTTON_EVENT_SHORT_PRESS, sw1_callback);
        button_register_cb(BUTTON_ID_SW1, BUTTON_EVENT_LONG_PRESS, sw1_callback);
        button_register_cb(BUTTON_ID_SW2, BUTTON_EVENT_SHORT_PRESS, sw2_callback);
        button_register_cb(BUTTON_ID_SW2, BUTTON_EVENT_LONG_PRESS, sw2_callback);
    }

    // 初始化 UI 管理器
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI manager init failed!");
    }

    // 初始化事件总线
    ret = event_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event bus init failed!");
    }

    // 初始化传感器服务
    ret = sensor_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sensor service init failed!");
    }

    // 订阅传感器数据事件
    event_subscribe(EVT_SENSOR_DATA, sensor_data_handler, NULL);

    // 启动传感器服务
    ret = sensor_service_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sensor service start failed!");
    }

    // 初始化健康监测服务
    ret = health_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Health monitor init failed!");
    }

    // 启动健康监测服务
    ret = health_monitor_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Health monitor start failed!");
    }

    // 初始化计步服务
    ret = pedometer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Pedometer init failed!");
    }

    // 启动计步服务
    ret = pedometer_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Pedometer start failed!");
    }

    // 初始化跌倒检测服务
    ret = fall_detect_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fall detect init failed!");
    }

    // 启动跌倒检测服务
    ret = fall_detect_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fall detect start failed!");
    }

    // 订阅跌倒检测事件
    event_subscribe(EVT_FALL_DETECTED, fall_detected_handler, NULL);

    // 初始化 BLE 服务（在所有传感器和服务初始化完成后）
    ret = ble_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE service init failed!");
    }

    // 订阅 BLE 连接状态事件
    event_subscribe(EVT_BLE_CONN, ble_conn_handler, NULL);

    ESP_LOGI(TAG, "System initialization complete.");
    ESP_LOGI(TAG, "Press SW2 to switch pages, long press SW2 for manual measure.");
    ESP_LOGI(TAG, "Press SW1 for manual alarm (BLE Notify).");

    // 主循环：保持系统运行，按键和 UI 由各自模块处理
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
