/**
 * @file max30102.c
 * @brief MAX30102 心率血氧传感器驱动实现
 */

#include "max30102.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX30102";

// 默认配置
static const max30102_config_t default_config = {
    .fifo_sample_avg = 2,       // 4 samples average
    .fifo_rollover = 1,         // 允许覆盖
    .fifo_almost_full = 15,     // 17 samples 触发中断
    .mode = MAX30102_MODE_SPO2, // SpO2 模式
    .adc_range = 1,             // 4096 nA
    .sample_rate = 1,           // 100 Hz
    .pulse_width = 3,           // 411 us (18-bit ADC)
    .led1_current = 0x24,       // ~7mA
    .led2_current = 0x24,       // ~7mA
};

/**
 * @brief 写入单个寄存器
 */
static esp_err_t max30102_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_byte(MAX30102_I2C_ADDR, reg, value);
}

/**
 * @brief 读取单个寄存器
 */
static esp_err_t max30102_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_bus_read_byte(MAX30102_I2C_ADDR, reg, value);
}

esp_err_t max30102_reset(void)
{
    esp_err_t ret = max30102_write_reg(MAX30102_REG_MODE_CONFIG, 0x40);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t max30102_read_part_id(uint8_t *part_id)
{
    return max30102_read_reg(MAX30102_REG_PART_ID, part_id);
}

esp_err_t max30102_clear_fifo(void)
{
    esp_err_t ret;
    ret = max30102_write_reg(MAX30102_REG_FIFO_WR_PTR, 0);
    if (ret != ESP_OK) return ret;
    ret = max30102_write_reg(MAX30102_REG_OVF_COUNTER, 0);
    if (ret != ESP_OK) return ret;
    ret = max30102_write_reg(MAX30102_REG_FIFO_RD_PTR, 0);
    return ret;
}

esp_err_t max30102_init_with_config(const max30102_config_t *config)
{
    esp_err_t ret;
    uint8_t part_id;

    // 读取并验证 Part ID
    ret = max30102_read_part_id(&part_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Part ID");
        return ret;
    }

    if (part_id != MAX30102_PART_ID_VALUE) {
        ESP_LOGE(TAG, "Invalid Part ID: 0x%02X (expected 0x%02X)",
                 part_id, MAX30102_PART_ID_VALUE);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Part ID: 0x%02X", part_id);

    // 软复位
    ret = max30102_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset failed");
        return ret;
    }

    // 配置 FIFO
    uint8_t fifo_cfg = ((config->fifo_sample_avg & 0x07) << 5) |
                       ((config->fifo_rollover & 0x01) << 4) |
                       (config->fifo_almost_full & 0x0F);
    ret = max30102_write_reg(MAX30102_REG_FIFO_CONFIG, fifo_cfg);
    if (ret != ESP_OK) return ret;

    // 配置 SpO2
    uint8_t spo2_cfg = ((config->adc_range & 0x03) << 5) |
                       ((config->sample_rate & 0x07) << 2) |
                       (config->pulse_width & 0x03);
    ret = max30102_write_reg(MAX30102_REG_SPO2_CONFIG, spo2_cfg);
    if (ret != ESP_OK) return ret;

    // 配置 LED 电流
    ret = max30102_write_reg(MAX30102_REG_LED1_PA, config->led1_current);
    if (ret != ESP_OK) return ret;
    ret = max30102_write_reg(MAX30102_REG_LED2_PA, config->led2_current);
    if (ret != ESP_OK) return ret;

    // 清空 FIFO
    ret = max30102_clear_fifo();
    if (ret != ESP_OK) return ret;

    // 设置工作模式
    ret = max30102_write_reg(MAX30102_REG_MODE_CONFIG, config->mode);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

esp_err_t max30102_init(void)
{
    return max30102_init_with_config(&default_config);
}

esp_err_t max30102_get_fifo_count(uint8_t *count)
{
    uint8_t wr_ptr, rd_ptr;
    esp_err_t ret;

    ret = max30102_read_reg(MAX30102_REG_FIFO_WR_PTR, &wr_ptr);
    if (ret != ESP_OK) return ret;

    ret = max30102_read_reg(MAX30102_REG_FIFO_RD_PTR, &rd_ptr);
    if (ret != ESP_OK) return ret;

    if (wr_ptr >= rd_ptr) {
        *count = wr_ptr - rd_ptr;
    } else {
        *count = (MAX30102_FIFO_DEPTH - rd_ptr) + wr_ptr;
    }

    return ESP_OK;
}

esp_err_t max30102_read_fifo(uint32_t *red, uint32_t *ir, uint8_t *count)
{
    esp_err_t ret;
    uint8_t available;
    uint8_t buffer[6];
    uint8_t samples_to_read;

    ret = max30102_get_fifo_count(&available);
    if (ret != ESP_OK) return ret;

    if (available == 0) {
        *count = 0;
        return ESP_OK;
    }

    samples_to_read = (available < *count) ? available : *count;

    for (uint8_t i = 0; i < samples_to_read; i++) {
        ret = i2c_bus_read(MAX30102_I2C_ADDR, MAX30102_REG_FIFO_DATA, buffer, 6);
        if (ret != ESP_OK) {
            *count = i;
            return ret;
        }

        red[i] = ((uint32_t)buffer[0] << 16) |
                 ((uint32_t)buffer[1] << 8) |
                 buffer[2];
        red[i] &= 0x3FFFF;

        ir[i] = ((uint32_t)buffer[3] << 16) |
                ((uint32_t)buffer[4] << 8) |
                buffer[5];
        ir[i] &= 0x3FFFF;
    }

    *count = samples_to_read;
    return ESP_OK;
}

esp_err_t max30102_shutdown(void)
{
    uint8_t mode;
    esp_err_t ret = max30102_read_reg(MAX30102_REG_MODE_CONFIG, &mode);
    if (ret != ESP_OK) return ret;
    return max30102_write_reg(MAX30102_REG_MODE_CONFIG, mode | 0x80);
}

esp_err_t max30102_wakeup(void)
{
    uint8_t mode;
    esp_err_t ret = max30102_read_reg(MAX30102_REG_MODE_CONFIG, &mode);
    if (ret != ESP_OK) return ret;
    return max30102_write_reg(MAX30102_REG_MODE_CONFIG, mode & ~0x80);
}

esp_err_t max30102_read_temperature(float *temp)
{
    esp_err_t ret;
    uint8_t temp_int, temp_frac;

    // 触发温度测量
    ret = max30102_write_reg(MAX30102_REG_TEMP_CONFIG, 0x01);
    if (ret != ESP_OK) return ret;

    // 等待测量完成
    vTaskDelay(pdMS_TO_TICKS(30));

    // 读取温度
    ret = max30102_read_reg(MAX30102_REG_TEMP_INT, &temp_int);
    if (ret != ESP_OK) return ret;

    ret = max30102_read_reg(MAX30102_REG_TEMP_FRAC, &temp_frac);
    if (ret != ESP_OK) return ret;

    *temp = (float)temp_int + (float)(temp_frac & 0x0F) * 0.0625f;
    return ESP_OK;
}
