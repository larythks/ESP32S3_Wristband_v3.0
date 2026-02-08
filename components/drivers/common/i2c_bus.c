/**
 * @file i2c_bus.c
 * @brief I2C 总线管理封装实现
 */

#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";
static bool s_i2c_initialized = false;

esp_err_t i2c_bus_init(void)
{
    if (s_i2c_initialized) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_initialized = true;
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, freq=%dHz)",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);

    return ESP_OK;
}

void i2c_bus_scan(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    uint8_t device_count = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                              pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            const char *name = "Unknown";
            if (addr == 0x68) name = "MPU6050";
            else if (addr == 0x57) name = "MAX30102";
            else if (addr == 0x3C) name = "SH1106";

            ESP_LOGI(TAG, "I2C device found at 0x%02X (%s)", addr, name);
            device_count++;
        }
    }

    ESP_LOGI(TAG, "I2C scan complete. Found %d device(s)", device_count);
}

esp_err_t i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    if (data != NULL && len > 0) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_bus_write_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    return i2c_bus_write(dev_addr, reg_addr, &data, 1);
}

esp_err_t i2c_bus_read_byte(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    return i2c_bus_read(dev_addr, reg_addr, data, 1);
}
