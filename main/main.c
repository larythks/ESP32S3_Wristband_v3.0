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

static const char *TAG = "main";

/**
 * @brief SW1 按键回调（报警键）
 */
static void sw1_callback(button_id_t id, button_event_t event)
{
    if (event == BUTTON_EVENT_SHORT_PRESS) {
        ESP_LOGI(TAG, "SW1 short press - Alarm button");
    } else if (event == BUTTON_EVENT_LONG_PRESS) {
        ESP_LOGI(TAG, "SW1 long press - Alarm button");
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
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Wristband v3.0 ===");
    ESP_LOGI(TAG, "Starting system initialization...");

    // 初始化 I2C 总线
    esp_err_t ret = i2c_bus_init();
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

    ESP_LOGI(TAG, "System initialization complete.");
    ESP_LOGI(TAG, "Press SW2 to switch pages, long press SW2 for manual measure.");
    ESP_LOGI(TAG, "Press SW1 for alarm (not implemented yet).");

    // 主循环：保持系统运行，按键和 UI 由各自模块处理
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
