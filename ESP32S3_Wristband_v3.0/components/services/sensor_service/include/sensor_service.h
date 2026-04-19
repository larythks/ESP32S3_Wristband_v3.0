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
#define SENSOR_TEMP_NORMAL_INTERVAL     30000   // 环境温度: 30秒
#define SENSOR_HR_NORMAL_INTERVAL       480000  // 心率血氧: 480秒(8分钟)
#define SENSOR_IMU_NORMAL_INTERVAL      20      // IMU: 50Hz (20ms)

// 心率测量窗口配置
#define SENSOR_HR_MEASURE_WINDOW_MS     15000   // 15秒测量窗口
#define SENSOR_HR_AUTO_INTERVAL_MS      480000  // 8分钟自动触发间隔
#define SENSOR_HR_BOOT_TRIGGER_DELAY_MS 1500    // Keep boot auto trigger; delay 1.5s to ease I2C contention

// 实时采样间隔 (ms)
#define SENSOR_REALTIME_INTERVAL        1000   // 实时模式: 16秒(匹配15秒HR测量窗口)

// PPG 批量数据配置
#define PPG_BATCH_MAX           16      // 单次取出最大样本数

/**
 * @brief PPG 批量数据结构（用于 sensor_drain_ppg）
 */
typedef struct {
    uint32_t red[PPG_BATCH_MAX];    // RED 通道原始值
    uint32_t ir[PPG_BATCH_MAX];     // IR 通道原始值
    uint8_t  count;                 // 实际样本数
} ppg_batch_t;

// 环境温度异常阈值 (摄氏度)
#define SENSOR_TEMP_HIGH_ALERT_THRESHOLD    34.8f   // 高温异常阈值
#define SENSOR_TEMP_HIGH_NORMAL_THRESHOLD   34.5f   // 高温恢复阈值
// #define SENSOR_TEMP_LOW_ALERT_THRESHOLD     35.0f   // 低温异常阈值
// #define SENSOR_TEMP_LOW_NORMAL_THRESHOLD    35.2f   // 低温恢复阈值
#define SENSOR_TEMP_LOW_ALERT_THRESHOLD     20.0f   // 低温异常阈值
#define SENSOR_TEMP_LOW_NORMAL_THRESHOLD    20.2f   // 低温恢复阈值

/**
 * @brief 心率测量窗口状态
 */
typedef enum {
    HR_MEASURE_IDLE = 0,    // 空闲，不采集PPG
    HR_MEASURE_MEASURING,   // 测量中，持续读取FIFO
    HR_MEASURE_COMPLETE     // 测量完成
} hr_measure_state_t;

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

/**
 * @brief 手动触发心率测量（进入15秒测量窗口）
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 已在测量中
 */
esp_err_t sensor_start_hr_measure(void);

/**
 * @brief 获取当前心率测量状态
 * @return 当前测量状态
 */
hr_measure_state_t sensor_get_hr_measure_state(void);

/**
 * @brief 取出所有已缓存的 PPG 样本（调用后缓冲区清空）
 * @param out 输出批量数据
 * @return ESP_OK 成功
 */
esp_err_t sensor_drain_ppg(ppg_batch_t *out);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_SERVICE_H
