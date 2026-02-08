/**
 * @file health_monitor.h
 * @brief 健康监测服务 - 心率/血氧计算、体温阈值检测
 */

#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include "esp_err.h"
#include "event_bus.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============== 阈值定义 ==============

// 心率阈值 (bpm)
#define HR_ALARM_HIGH           120     // 心率过高
#define HR_ALARM_LOW            45      // 心率过低

// 血氧阈值 (%)
#define SPO2_ALARM_LOW          90      // 血氧过低 (ALARM)
#define SPO2_WARNING_LOW        92      // 血氧偏低 (WARNING)

// 体温阈值 (°C)
#define TEMP_ALARM_HIGH         37.8f   // 体温过高
// #define TEMP_ALARM_LOW          35.0f   // 体温过低
#define TEMP_ALARM_LOW          20.0f   // 体温过低

// 信号质量阈值
#define PPG_MIN_AMPLITUDE       100     // PPG 最小有效振幅
#define TEMP_MAX_JUMP           2.0f    // 体温最大有效跳变

// 连续触发次数
#define TEMP_ALARM_COUNT        3       // 体温连续越阈次数
#define HR_ALARM_DURATION_MS    30000   // 心率持续越阈时间 (30秒)
#define SPO2_ALARM_DURATION_MS  20000   // 血氧持续越阈时间 (20秒)

// ============== 数据结构 ==============
// 注意: alert_level_t, alert_type_t, health_alert_t 已在 event_bus.h 中定义

/**
 * @brief 测量有效性状态
 */
typedef enum {
    MEASURE_VALID = 0,          // 测量有效
    MEASURE_INVALID_NO_SIGNAL,  // 无信号
    MEASURE_INVALID_MOTION,     // 运动干扰
    MEASURE_INVALID_UNSTABLE    // 信号不稳定
} measure_validity_t;

/**
 * @brief 健康状态结构
 */
typedef struct {
    // 计算结果
    uint8_t heart_rate;         // 心率 (bpm), 0 表示无效
    uint8_t spo2;               // 血氧 (%), 0 表示无效
    float temperature;          // 体温 (°C)

    // 有效性标志
    measure_validity_t hr_validity;     // 心率有效性
    measure_validity_t spo2_validity;   // 血氧有效性
    measure_validity_t temp_validity;   // 体温有效性

    // 告警状态
    alert_level_t alert_level;  // 当前告警级别
    alert_type_t alert_type;    // 当前告警类型
    int16_t alert_value;        // 触发值 (×10)

    // 时间戳
    uint32_t timestamp;         // 更新时间戳 (ms)
} health_status_t;

// ============== API 函数 ==============

/**
 * @brief 初始化健康监测服务
 * @return ESP_OK 成功
 */
esp_err_t health_monitor_init(void);

/**
 * @brief 启动健康监测服务
 * @return ESP_OK 成功
 */
esp_err_t health_monitor_start(void);

/**
 * @brief 停止健康监测服务
 * @return ESP_OK 成功
 */
esp_err_t health_monitor_stop(void);

/**
 * @brief 获取当前健康状态
 * @return 健康状态结构
 */
health_status_t health_get_status(void);

/**
 * @brief 重置告警状态
 */
void health_reset_alert(void);

#ifdef __cplusplus
}
#endif

#endif // HEALTH_MONITOR_H
