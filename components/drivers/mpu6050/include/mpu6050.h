/**
 * @file mpu6050.h
 * @brief MPU6050 6轴IMU驱动
 */

#ifndef MPU6050_H
#define MPU6050_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MPU6050 I2C 地址
#define MPU6050_I2C_ADDR        0x68

// MPU6050 寄存器地址
#define MPU6050_REG_WHO_AM_I    0x75
#define MPU6050_REG_PWR_MGMT_1  0x6B
#define MPU6050_REG_PWR_MGMT_2  0x6C
#define MPU6050_REG_SMPLRT_DIV  0x19
#define MPU6050_REG_CONFIG      0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_GYRO_XOUT_H  0x43
#define MPU6050_REG_TEMP_OUT_H   0x41

// WHO_AM_I 期望值
#define MPU6050_WHO_AM_I_VAL    0x70

// 加速度量程配置
typedef enum {
    MPU6050_ACCEL_FS_2G  = 0,  // ±2g
    MPU6050_ACCEL_FS_4G  = 1,  // ±4g
    MPU6050_ACCEL_FS_8G  = 2,  // ±8g
    MPU6050_ACCEL_FS_16G = 3   // ±16g
} mpu6050_accel_fs_t;

// 陀螺仪量程配置
typedef enum {
    MPU6050_GYRO_FS_250DPS  = 0,  // ±250°/s
    MPU6050_GYRO_FS_500DPS  = 1,  // ±500°/s
    MPU6050_GYRO_FS_1000DPS = 2,  // ±1000°/s
    MPU6050_GYRO_FS_2000DPS = 3   // ±2000°/s
} mpu6050_gyro_fs_t;

/**
 * @brief 初始化 MPU6050
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_init(void);

/**
 * @brief 读取加速度原始数据
 * @param ax X轴加速度
 * @param ay Y轴加速度
 * @param az Z轴加速度
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az);

/**
 * @brief 读取陀螺仪原始数据
 * @param gx X轴角速度
 * @param gy Y轴角速度
 * @param gz Z轴角速度
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz);

/**
 * @brief 读取温度原始数据
 * @param temp 温度原始值
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_read_temp_raw(int16_t *temp);

/**
 * @brief 设置加速度量程
 * @param fs 量程选择
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_set_accel_fs(mpu6050_accel_fs_t fs);

/**
 * @brief 设置陀螺仪量程
 * @param fs 量程选择
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_set_gyro_fs(mpu6050_gyro_fs_t fs);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H
