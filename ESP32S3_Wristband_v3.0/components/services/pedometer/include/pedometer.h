/**
 * @file pedometer.h
 * @brief 计步服务 - 基于加速度的步数统计
 */

#ifndef PEDOMETER_H
#define PEDOMETER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============== 算法参数 ==============

#define PEDOMETER_THRESHOLD_LOW     16200   // 峰值检测下阈值 (原始LSB, 静止≈16384)
#define PEDOMETER_THRESHOLD_HIGH    18000   // 峰值检测上阈值 (原始LSB, 行走峰值≈17000-19000)
#define PEDOMETER_MIN_INTERVAL_MS   250     // 最小步间隔 (ms)
#define PEDOMETER_MAX_INTERVAL_MS   2000    // 最大步间隔 (ms)

// ============== API 函数 ==============

/**
 * @brief 初始化计步服务
 * @return ESP_OK 成功
 */
esp_err_t pedometer_init(void);

/**
 * @brief 启动计步服务
 * @return ESP_OK 成功
 */
esp_err_t pedometer_start(void);

/**
 * @brief 停止计步服务
 * @return ESP_OK 成功
 */
esp_err_t pedometer_stop(void);

/**
 * @brief 获取当前步数
 * @return 步数
 */
uint32_t pedometer_get_steps(void);

/**
 * @brief 重置步数 (每日重置)
 */
void pedometer_reset(void);

#ifdef __cplusplus
}
#endif

#endif // PEDOMETER_H
