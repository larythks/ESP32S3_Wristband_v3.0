/**
 * @file mpu6050.c
 * @brief MPU6050 6轴IMU驱动实现
 */

#include "mpu6050.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "mpu6050";

esp_err_t mpu6050_init(void)
{
    esp_err_t ret;
    uint8_t who_am_i;

    // 读取 WHO_AM_I 寄存器验证设备
    ret = i2c_bus_read_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I");
        return ret;
    }

    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X", who_am_i);
    if (who_am_i != MPU6050_WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I value (expected 0x%02X)", MPU6050_WHO_AM_I_VAL);
    }

    // 复位设备
    ret = i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_PWR_MGMT_1, 0x80);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset device");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // 唤醒设备，使用内部8MHz时钟
    ret = i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up device");
        return ret;
    }

    // 设置采样率分频器 (Sample Rate = 1kHz / (1 + SMPLRT_DIV))
    ret = i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_SMPLRT_DIV, 9);
    if (ret != ESP_OK) return ret;

    // 配置数字低通滤波器
    ret = i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_CONFIG, 0x03);
    if (ret != ESP_OK) return ret;

    // 设置加速度量程 ±8g
    ret = mpu6050_set_accel_fs(MPU6050_ACCEL_FS_8G);
    if (ret != ESP_OK) return ret;

    // 设置陀螺仪量程 ±250°/s
    ret = mpu6050_set_gyro_fs(MPU6050_GYRO_FS_250DPS);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    return ESP_OK;
}

esp_err_t mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t data[6];
    esp_err_t ret = i2c_bus_read_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_ACCEL_XOUT_H, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    *ax = (int16_t)((data[0] << 8) | data[1]);
    *ay = (int16_t)((data[2] << 8) | data[3]);
    *az = (int16_t)((data[4] << 8) | data[5]);

    return ESP_OK;
}

esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t data[6];
    esp_err_t ret = i2c_bus_read_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_GYRO_XOUT_H, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    *gx = (int16_t)((data[0] << 8) | data[1]);
    *gy = (int16_t)((data[2] << 8) | data[3]);
    *gz = (int16_t)((data[4] << 8) | data[5]);

    return ESP_OK;
}

esp_err_t mpu6050_read_temp(int16_t *temp)
{
    uint8_t data[2];
    esp_err_t ret = i2c_bus_read_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_TEMP_OUT_H, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    *temp = (int16_t)((data[0] << 8) | data[1]);
    return ESP_OK;
}

esp_err_t mpu6050_set_accel_fs(mpu6050_accel_fs_t fs)
{
    return i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_ACCEL_CONFIG, fs << 3);
}

esp_err_t mpu6050_set_gyro_fs(mpu6050_gyro_fs_t fs)
{
    return i2c_bus_write_byte_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_GYRO_CONFIG, fs << 3);
}
