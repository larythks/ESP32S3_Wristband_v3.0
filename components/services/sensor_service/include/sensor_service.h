/**
 * @file sensor_service.h
 * @brief 传感器采样服务 - 统一传感器采样调度
 */

#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "esp_err.h"
#include "event_bus.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 常规采样间隔 (ms)
#define SENSOR_TEMP_NORMAL_INTERVAL     30000   // 体温: 30秒
#define SENSOR_HR_NORMAL_INTERVAL       120000  // 心率血氧: 120秒
#define SENSOR_IMU_NORMAL_INTERVAL      20      // IMU: 50Hz (20ms)

// 实时采样间隔 (ms)
#define SENSOR_REALTIME_INTERVAL        1000    // 实时模式: 1秒

// 温度异常阈值 (摄氏度)
#define SENSOR_TEMP_HIGH_ALERT_THRESHOLD    37.5f   // 高温异常阈值
#define SENSOR_TEMP_HIGH_NORMAL_THRESHOLD   37.3f   // 高温恢复阈值
// #define SENSOR_TEMP_LOW_ALERT_THRESHOLD     35.0f   // 低温异常阈值
// #define SENSOR_TEMP_LOW_NORMAL_THRESHOLD    35.2f   // 低温恢复阈值
#define SENSOR_TEMP_LOW_ALERT_THRESHOLD     20.0f   // 低温异常阈值
#define SENSOR_TEMP_LOW_NORMAL_THRESHOLD    20.2f   // 低温恢复阈值

/**
 * @brief 初始化传感器服务
 * @return ESP_OK 成功
 */
esp_err_t sensor_service_init(void);

/**
 * @brief 启动传感器服务
 * @return ESP_OK 成功
 */
esp_err_t sensor_service_start(void);

/**
 * @brief 停止传感器服务
 * @return ESP_OK 成功
 */
esp_err_t sensor_service_stop(void);

/**
 * @brief 获取最新传感器数据
 * @param data 输出数据
 * @return ESP_OK 成功
 */
esp_err_t sensor_get_latest(sensor_data_t *data);

/**
 * @brief 设置传感器采样模式
 * @param sensor_mask 传感器掩码
 * @param mode 采样模式
 * @return ESP_OK 成功
 */
esp_err_t sensor_set_mode(uint8_t sensor_mask, sampling_mode_t mode);

/**
 * @brief 获取当前采样模式
 * @param sensor_mask 传感器掩码
 * @return 采样模式
 */
sampling_mode_t sensor_get_mode(uint8_t sensor_mask);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_SERVICE_H
