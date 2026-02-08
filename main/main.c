#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "mpu6050.h"
#include "sh1106.h"

static const char *TAG = "main";

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

    // 显示欢迎信息
    sh1106_draw_string(10, 0, "ESP32-S3", 1);
    sh1106_draw_string(10, 10, "Wristband v3.0", 1);
    sh1106_update();

    ESP_LOGI(TAG, "System initialization complete.");

    // 主循环：读取并显示加速度数据
    char buf[32];
    int16_t ax, ay, az;

    while (1) {
        if (mpu6050_read_accel(&ax, &ay, &az) == ESP_OK) {
            ESP_LOGI(TAG, "Accel: X=%d Y=%d Z=%d", ax, ay, az);

            // 更新 OLED 显示
            sh1106_clear();
            sh1106_draw_string(0, 0, "Accel Data:", 1);

            snprintf(buf, sizeof(buf), "X: %d", ax);
            sh1106_draw_string(0, 16, buf, 1);

            snprintf(buf, sizeof(buf), "Y: %d", ay);
            sh1106_draw_string(0, 26, buf, 1);

            snprintf(buf, sizeof(buf), "Z: %d", az);
            sh1106_draw_string(0, 36, buf, 1);

            sh1106_update();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
