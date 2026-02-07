#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_bus.h"

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

    ESP_LOGI(TAG, "System initialization complete.");
}
