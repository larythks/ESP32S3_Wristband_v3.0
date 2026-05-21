/**
 * @file mpu6050.h
 * @brief MPU6050 6轴IMU驱动
 */

#ifndef MPU6050_H
#define MPU6050_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// MPU6050 I2C 地址
#define MPU6050_I2C_ADDR        0x69

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

// 校准采样数量及 NVS 存储键
#define MPU6050_CAL_SAMPLES     200
#define MPU6050_NVS_NAMESPACE   "mpu6050_cal"
#define MPU6050_NVS_KEY         "cal_data"

/**
 * @brief MPU6050 校准数据
 */
typedef struct {
    int16_t accel_bias[3]; // [ax, ay, az] 偏置，单位 LSB
    int16_t gyro_bias[3];  // [gx, gy, gz] 偏置，单位 LSB
    bool    valid;         // 校准数据是否有效
} mpu6050_calibration_t;

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
esp_err_t mpu6050_read_temp(int16_t *temp);

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

/**
 * @brief 执行静态一点法校准（设备必须水平静置，Z轴朝上，约1秒阻塞）
 * @param cal 输出校准数据
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_calibrate(mpu6050_calibration_t *cal);

/**
 * @brief 运行时设置校准数据（立即生效）
 * @param cal 校准数据
 */
void mpu6050_set_calibration(const mpu6050_calibration_t *cal);

/**
 * @brief 读取当前生效的校准数据
 * @param cal 输出校准数据
 */
void mpu6050_get_calibration(mpu6050_calibration_t *cal);

/**
 * @brief 将校准数据写入 NVS 持久化
 * @param cal 校准数据
 * @return ESP_OK 成功
 */
esp_err_t mpu6050_save_calibration(const mpu6050_calibration_t *cal);

/**
 * @brief 从 NVS 加载校准数据并立即应用（无数据则零偏置运行）
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无校准数据
 */
esp_err_t mpu6050_load_calibration(void);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H
