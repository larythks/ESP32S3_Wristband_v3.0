/**
 * @file fall_detect.h
 * @brief 跌倒检测服务 - 基于 MPU6050 加速度数据的跌倒检测算法
 */

#ifndef FALL_DETECT_H
#define FALL_DETECT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 算法参数配置（可调优）
// ============================================================================

// 加速度阈值 (单位: g, 1g = 9.8m/s²)
#define FALL_IMPACT_THRESHOLD       2.0f    // 冲击阈值 (SVM > 2.0g 判定为冲击)
#define FALL_FREEFALL_THRESHOLD     0.6f    // 自由落体阈值 (SVM < 0.6g)
#define FALL_STATIC_LOW             0.8f    // 静止状态下限
#define FALL_STATIC_HIGH            1.2f    // 静止状态上限

// 姿态判断阈值
#define FALL_ORIENTATION_THRESHOLD  0.5f    // 姿态变化阈值 (cos值，约60度)

// 时间窗口 (ms)
#define FALL_FREEFALL_WINDOW        200     // 自由落体检测窗口
#define FALL_IMPACT_WINDOW          100     // 冲击检测窗口
#define FALL_POST_IMPACT_WINDOW     2000    // 冲击后监测窗口
#define FALL_STATIC_DURATION        1000    // 静止持续时间要求
#define FALL_COOLDOWN_DURATION      30000   // 跌倒检测后冷却时间 (30秒)

// 采样配置
#define FALL_SAMPLE_RATE_HZ         50      // 采样率 (与 IMU 采样率一致)
#define FALL_HISTORY_SIZE           100     // 历史数据缓冲区大小 (2秒数据)

// ============================================================================
// 数据类型定义
// ============================================================================

/**
 * @brief 跌倒检测状态
 */
typedef enum {
    FALL_STATE_IDLE = 0,            // 空闲状态，正常监测
    FALL_STATE_FREEFALL,            // 检测到自由落体
    FALL_STATE_IMPACT,              // 检测到冲击
    FALL_STATE_POST_IMPACT,         // 冲击后监测
    FALL_STATE_FALL_DETECTED        // 确认跌倒
} fall_state_t;

/**
 * @brief 跌倒事件数据
 */
typedef struct {
    float peak_svm;                 // 冲击峰值 (g)
    float pre_orientation[3];       // 跌倒前姿态 (归一化加速度)
    float post_orientation[3];      // 跌倒后姿态
    uint32_t timestamp;             // 检测时间戳
} fall_event_data_t;

/**
 * @brief 跌倒检测统计信息（用于调试和量化测试）
 */
typedef struct {
    uint32_t total_detections;      // 总检测次数
    uint32_t false_alarms;          // 误报次数（被取消的）
    uint32_t confirmed_falls;       // 确认跌倒次数
    float avg_response_time_ms;     // 平均响应时间
} fall_detect_stats_t;

// ============================================================================
// API 函数声明
// ============================================================================

/**
 * @brief 初始化跌倒检测服务
 * @return ESP_OK 成功
 */
esp_err_t fall_detect_init(void);

/**
 * @brief 启动跌倒检测
 * @return ESP_OK 成功
 */
esp_err_t fall_detect_start(void);

/**
 * @brief 停止跌倒检测
 * @return ESP_OK 成功
 */
esp_err_t fall_detect_stop(void);

/**
 * @brief 处理加速度数据（由 sensor_service 事件回调调用）
 * @param ax 加速度 X (原始值)
 * @param ay 加速度 Y (原始值)
 * @param az 加速度 Z (原始值)
 * @return true 如果检测到跌倒
 */
bool fall_detect_process(int16_t ax, int16_t ay, int16_t az);

/**
 * @brief 获取当前跌倒检测状态
 * @return 当前状态
 */
fall_state_t fall_detect_get_state(void);

/**
 * @brief 重置跌倒检测状态（用于取消误报）
 */
void fall_detect_reset(void);

/**
 * @brief 获取最近一次跌倒事件数据
 * @param event 输出事件数据
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无事件
 */
esp_err_t fall_detect_get_event(fall_event_data_t *event);

/**
 * @brief 获取统计信息
 * @param stats 输出统计数据
 * @return ESP_OK 成功
 */
esp_err_t fall_detect_get_stats(fall_detect_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif // FALL_DETECT_H
