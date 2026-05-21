#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"
#include "mpu6050.h"
#include "sh1106.h"
#include "max30102.h"
#include "ds18b20.h"
#include "ds3231.h"
#include "button.h"
#include "ui_manager.h"
#include "event_bus.h"
#include "alarm_manager.h"
#include "ws2812.h"
#include "sensor_service.h"
#include "health_monitor.h"
#include "pedometer.h"
#include "fall_detect.h"
#include "ble_service.h"
#include "ble_gatt_defs.h"
#include "nvs_flash.h"
#include "audio_player.h"
#include "voice_cmd.h"

static const char *TAG = "main";

// 设为 1 时仅运行 MPU6050 校准验证，跳过所有其他初始化；验证完改回 0
#define MPU6050_CAL_VERIFY  0

#define RTC_BOOT_WAIT_TIMEOUT_MS      3000
#define RTC_BOOT_RETRY_DELAY_MS         50
#define RTC_BOOT_RETRY_LOG_INTERVAL     10

static bool wait_rtc_ready(ds3231_time_t *rtc_time, uint32_t timeout_ms)
{
    if (rtc_time == NULL) {
        return false;
    }

    uint32_t attempt = 0;
    int64_t start_us = esp_timer_get_time();
    esp_err_t last_err = ESP_FAIL;

    while ((uint32_t)((esp_timer_get_time() - start_us) / 1000) < timeout_ms) {
        attempt++;
        last_err = ds3231_get_time(rtc_time);
        if (last_err == ESP_OK) {
            uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGI(TAG, "RTC ready after %lu attempt(s), %lu ms",
                     (unsigned long)attempt, (unsigned long)elapsed_ms);
            return true;
        }

        if (attempt == 1 || (attempt % RTC_BOOT_RETRY_LOG_INTERVAL) == 0) {
            ESP_LOGW(TAG, "RTC read retry %lu failed: %s",
                     (unsigned long)attempt, esp_err_to_name(last_err));
        }

        vTaskDelay(pdMS_TO_TICKS(RTC_BOOT_RETRY_DELAY_MS));
    }

    ESP_LOGW(TAG, "RTC read timeout after %lu ms, last error: %s",
             (unsigned long)timeout_ms, esp_err_to_name(last_err));
    return false;
}

/**
 * @brief 传感器数据事件处理回调
 */
static void sensor_data_handler(const event_t *event, void *user_data)
{
    const sensor_data_t *data = &event->data.sensor;

    // 每 5 秒打印一次传感器数据
    static uint32_t last_print = 0;
    if ((data->timestamp - last_print) >= 5000) {
        ESP_LOGD(TAG, "[SENSOR] temp=%.1f ax=%d ay=%d az=%d",
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
    alarm_state_t state = alarm_get_state();

    if (event == BUTTON_EVENT_SHORT_PRESS) {
        if (state == ALARM_STATE_IDLE) {
            ESP_LOGI(TAG, "SW1 short press - Manual alarm triggered");
            alarm_trigger(ALERT_TYPE_MANUAL, NULL);
        } else if (state == ALARM_STATE_PRE_ALARM) {
            ESP_LOGI(TAG, "SW1 short press - Cancel pre-alarm");
            alarm_cancel();
        }
    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        if (state == ALARM_STATE_ALARMING) {
            ESP_LOGI(TAG, "SW1 long press - Cancel alarm");
            alarm_cancel();
        }
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

    // 初始化 I2C Bus 0 (MAX30102 + MPU6050，原接线)
    ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus0 init failed!");
        return;
    }

    // 初始化 I2C Bus 1 (SH1106 OLED + DS3231 RTC，新接线)
    ret = i2c_bus1_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus1 init failed!");
        return;
    }

    // 扫描 I2C 设备
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_bus_scan();
    i2c_bus1_scan();

    // 初始化 MPU6050
    ret = mpu6050_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 init failed!");
    }

    // 首次启动时无 NVS 校准数据，自动执行一次静态校准
    mpu6050_calibration_t cal;
    mpu6050_get_calibration(&cal);
    if (!cal.valid) {
        ESP_LOGI(TAG, "No MPU6050 calibration data. Place device flat (Z-up), calibrating in 3s...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (mpu6050_calibrate(&cal) == ESP_OK) {
            mpu6050_save_calibration(&cal);
            ESP_LOGI(TAG, "Calibration saved: a_bias=[%d,%d,%d] g_bias=[%d,%d,%d]",
                     cal.accel_bias[0], cal.accel_bias[1], cal.accel_bias[2],
                     cal.gyro_bias[0],  cal.gyro_bias[1],  cal.gyro_bias[2]);
        } else {
            ESP_LOGW(TAG, "Calibration failed, running with zero bias");
        }
    }

#if MPU6050_CAL_VERIFY
    ESP_LOGI(TAG, "=== MPU6050 Calibration Verify Mode ===");
    ESP_LOGI(TAG, "Place device flat Z-up: expect ax≈0 ay≈0 az≈4096 SVM≈1.000g");
    while (1) {
        int16_t ax, ay, az, gx, gy, gz;
        if (mpu6050_read_accel(&ax, &ay, &az) == ESP_OK &&
            mpu6050_read_gyro(&gx, &gy, &gz)  == ESP_OK) {
            float svm = sqrtf((float)ax*ax + (float)ay*ay + (float)az*az) / 4096.0f;
            ESP_LOGI(TAG, "A=[%6d,%6d,%6d]  G=[%6d,%6d,%6d]  SVM=%.3fg",
                     ax, ay, az, gx, gy, gz, svm);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return; // 不执行后续任何初始化
#endif

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

    // 初始化 DS3231 RTC
    ds3231_time_t rtc_time = {0};
    bool rtc_valid = false;
    ret = ds3231_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 init failed!");
    } else {
        rtc_valid = wait_rtc_ready(&rtc_time, RTC_BOOT_WAIT_TIMEOUT_MS);
        if (rtc_valid) {
            ESP_LOGI(TAG, "RTC time: %04u-%02u-%02u %02u:%02u:%02u (DOW=%u)",
                     rtc_time.year, rtc_time.month, rtc_time.day,
                     rtc_time.hour, rtc_time.minute, rtc_time.second,
                     rtc_time.day_of_week);
        } else {
            ESP_LOGW(TAG, "RTC read failed in boot wait window");
        }
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

    // 初始化事件总线（必须先于所有订阅者，否则 event_subscribe 会失败）
    ret = event_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event bus init failed!");
    }

    // 初始化 UI 管理器
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI manager init failed!");
    }

    // 将开机时读到的 RTC 时间注入 UI 缓存，避免等待 I2C 总线空闲
    if (rtc_valid) {
        ui_manager_set_rtc_cache(&rtc_time);
    }

    // 初始化 WS2812 RGB LED
    ret = ws2812_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed!");
    }

    // 初始化音频播放器 (SPIFFS 挂载 + I2S mutex)
    bool audio_ok = false;
    ret = audio_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio player init failed! Voice features disabled.");
    } else {
        audio_ok = true;
    }

    // 初始化报警管理器
    ret = alarm_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Alarm manager init failed!");
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

    // 初始化 BLE 服务（在所有传感器和服务初始化完成后）
    ret = ble_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE service init failed!");
    }

    // 订阅 BLE 连接状态事件
    event_subscribe(EVT_BLE_CONN, ble_conn_handler, NULL);

    // 初始化语音命令模块 (ESP-SR) — 依赖 audio_player
    // 如果 audio_player 初始化失败，跳过语音功能以避免"看起来在运行但语音不可用"
    if (audio_ok) {
        ret = voice_cmd_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Voice command init failed! Voice features disabled.");
        } else {
            ret = voice_cmd_start();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Voice command start failed!");
            }
        }
    } else {
        ESP_LOGW(TAG, "Skipping voice_cmd (audio_player not available)");
    }

    ESP_LOGI(TAG, "System initialization complete.");
    ESP_LOGI(TAG, "Press SW2 to switch pages, long press SW2 for manual measure.");
    ESP_LOGI(TAG, "Press SW1 to trigger/cancel alarm.");
    ESP_LOGI(TAG, "Say 'Hi Lexin' to activate voice commands.");

    // 主循环：保持系统运行，按键和 UI 由各自模块处理
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
