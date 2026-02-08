/**
 * @file ds18b20.c
 * @brief DS18B20 温度传感器驱动实现
 */

#include "ds18b20.h"
#include "onewire.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";
static onewire_bus_t ow_bus;
static bool initialized = false;

esp_err_t ds18b20_init(gpio_num_t pin)
{
    esp_err_t ret = onewire_init(&ow_bus, pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "1-Wire init failed");
        return ret;
    }

    if (!onewire_reset(&ow_bus)) {
        ESP_LOGE(TAG, "No device found");
        return ESP_ERR_NOT_FOUND;
    }

    initialized = true;
    ESP_LOGI(TAG, "Initialized on GPIO%d", pin);
    return ESP_OK;
}

bool ds18b20_is_present(void)
{
    if (!initialized) return false;
    return onewire_reset(&ow_bus);
}

esp_err_t ds18b20_start_convert(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!onewire_reset(&ow_bus)) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(&ow_bus, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(&ow_bus, DS18B20_CMD_CONVERT_T);

    return ESP_OK;
}

esp_err_t ds18b20_read_scratchpad(float *temp_c)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 复位并读取暂存器
    if (!onewire_reset(&ow_bus)) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(&ow_bus, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(&ow_bus, DS18B20_CMD_READ_SCRATCH);

    // 读取温度数据 (2字节)
    uint8_t lsb = onewire_read_byte(&ow_bus);
    uint8_t msb = onewire_read_byte(&ow_bus);

    // 转换为温度值
    int16_t raw = (msb << 8) | lsb;
    *temp_c = raw / 16.0f;

    return ESP_OK;
}

esp_err_t ds18b20_read_temp(float *temp_c)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 启动转换
    esp_err_t ret = ds18b20_start_convert();
    if (ret != ESP_OK) {
        return ret;
    }

    // 等待转换完成 (12位精度需要750ms)
    vTaskDelay(pdMS_TO_TICKS(750));

    // 复位并读取暂存器
    if (!onewire_reset(&ow_bus)) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(&ow_bus, DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(&ow_bus, DS18B20_CMD_READ_SCRATCH);

    // 读取温度数据 (2字节)
    uint8_t lsb = onewire_read_byte(&ow_bus);
    uint8_t msb = onewire_read_byte(&ow_bus);

    // 转换为温度值
    int16_t raw = (msb << 8) | lsb;
    *temp_c = raw / 16.0f;

    return ESP_OK;
}
