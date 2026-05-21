/**
 * @file mpu6050.c
 * @brief MPU6050 6轴IMU驱动实现
 */

#include "mpu6050.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "mpu6050";

// 模块内校准状态（零初始化 = valid:false, bias全0）
static mpu6050_calibration_t s_cal = { .valid = false };

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

    // 尝试从 NVS 加载校准数据，失败则零偏置运行
    if (mpu6050_load_calibration() == ESP_OK) {
        ESP_LOGI(TAG, "Calibration loaded from NVS");
    } else {
        ESP_LOGW(TAG, "No calibration data, running with zero bias");
    }

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

    // 应用加速度偏置补偿，int32_t 中间值防溢出后饱和截断
    int32_t raw_ax = (int16_t)((data[0] << 8) | data[1]);
    int32_t raw_ay = (int16_t)((data[2] << 8) | data[3]);
    int32_t raw_az = (int16_t)((data[4] << 8) | data[5]);

    if (s_cal.valid) {
        raw_ax -= s_cal.accel_bias[0];
        raw_ay -= s_cal.accel_bias[1];
        raw_az -= s_cal.accel_bias[2];
    }

#define CLAMP16(v) ((int16_t)((v) > 32767 ? 32767 : ((v) < -32768 ? -32768 : (v))))
    *ax = CLAMP16(raw_ax);
    *ay = CLAMP16(raw_ay);
    *az = CLAMP16(raw_az);
#undef CLAMP16

    return ESP_OK;
}

esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t data[6];
    esp_err_t ret = i2c_bus_read_port(I2C_BUS0_NUM, MPU6050_I2C_ADDR, MPU6050_REG_GYRO_XOUT_H, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // 应用陀螺仪偏置补偿
    int32_t raw_gx = (int16_t)((data[0] << 8) | data[1]);
    int32_t raw_gy = (int16_t)((data[2] << 8) | data[3]);
    int32_t raw_gz = (int16_t)((data[4] << 8) | data[5]);

    if (s_cal.valid) {
        raw_gx -= s_cal.gyro_bias[0];
        raw_gy -= s_cal.gyro_bias[1];
        raw_gz -= s_cal.gyro_bias[2];
    }

#define CLAMP16(v) ((int16_t)((v) > 32767 ? 32767 : ((v) < -32768 ? -32768 : (v))))
    *gx = CLAMP16(raw_gx);
    *gy = CLAMP16(raw_gy);
    *gz = CLAMP16(raw_gz);
#undef CLAMP16

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

esp_err_t mpu6050_calibrate(mpu6050_calibration_t *cal)
{
    if (!cal) return ESP_ERR_INVALID_ARG;

    int32_t sum[6] = {0};   // [ax, ay, az, gx, gy, gz]
    int16_t ax, ay, az, gx, gy, gz;

    // 采集 MPU6050_CAL_SAMPLES 个样本（设备须水平静置）
    for (int i = 0; i < MPU6050_CAL_SAMPLES; i++) {
        esp_err_t r1 = mpu6050_read_accel(&ax, &ay, &az);
        esp_err_t r2 = mpu6050_read_gyro(&gx, &gy, &gz);
        if (r1 != ESP_OK || r2 != ESP_OK) {
            ESP_LOGE(TAG, "Calibrate: read failed at sample %d", i);
            return ESP_FAIL;
        }
        // read_accel 已减去旧偏置；校准时需要原始值，先加回来
        if (s_cal.valid) {
            sum[0] += (int32_t)ax + s_cal.accel_bias[0];
            sum[1] += (int32_t)ay + s_cal.accel_bias[1];
            sum[2] += (int32_t)az + s_cal.accel_bias[2];
            sum[3] += (int32_t)gx + s_cal.gyro_bias[0];
            sum[4] += (int32_t)gy + s_cal.gyro_bias[1];
            sum[5] += (int32_t)gz + s_cal.gyro_bias[2];
        } else {
            sum[0] += ax; sum[1] += ay; sum[2] += az;
            sum[3] += gx; sum[4] += gy; sum[5] += gz;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 计算均值作为偏置（Z轴加速度减去 1g 重力分量 4096 LSB @ ±8g）
    cal->accel_bias[0] = (int16_t)(sum[0] / MPU6050_CAL_SAMPLES);
    cal->accel_bias[1] = (int16_t)(sum[1] / MPU6050_CAL_SAMPLES);
    cal->accel_bias[2] = (int16_t)(sum[2] / MPU6050_CAL_SAMPLES - 4096);
    cal->gyro_bias[0]  = (int16_t)(sum[3] / MPU6050_CAL_SAMPLES);
    cal->gyro_bias[1]  = (int16_t)(sum[4] / MPU6050_CAL_SAMPLES);
    cal->gyro_bias[2]  = (int16_t)(sum[5] / MPU6050_CAL_SAMPLES);
    cal->valid = true;

    ESP_LOGI(TAG, "Calibration done: a_bias=[%d,%d,%d] g_bias=[%d,%d,%d]",
             cal->accel_bias[0], cal->accel_bias[1], cal->accel_bias[2],
             cal->gyro_bias[0],  cal->gyro_bias[1],  cal->gyro_bias[2]);

    return ESP_OK;
}

void mpu6050_set_calibration(const mpu6050_calibration_t *cal)
{
    if (!cal) return;
    memcpy(&s_cal, cal, sizeof(mpu6050_calibration_t));
}

void mpu6050_get_calibration(mpu6050_calibration_t *cal)
{
    if (!cal) return;
    memcpy(cal, &s_cal, sizeof(mpu6050_calibration_t));
}

esp_err_t mpu6050_save_calibration(const mpu6050_calibration_t *cal)
{
    if (!cal || !cal->valid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(MPU6050_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_blob(handle, MPU6050_NVS_KEY, cal, sizeof(mpu6050_calibration_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return ret;
}

esp_err_t mpu6050_load_calibration(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(MPU6050_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    mpu6050_calibration_t tmp;
    size_t size = sizeof(mpu6050_calibration_t);
    ret = nvs_get_blob(handle, MPU6050_NVS_KEY, &tmp, &size);
    nvs_close(handle);

    if (ret == ESP_OK && tmp.valid) {
        memcpy(&s_cal, &tmp, sizeof(mpu6050_calibration_t));
    }
    return ret;
}
