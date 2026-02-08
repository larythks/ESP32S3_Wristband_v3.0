/**
 * @file event_bus.h
 * @brief 事件总线 - 模块间发布/订阅通信机制
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 最大订阅者数量
#define EVENT_BUS_MAX_SUBSCRIBERS   8

// 事件队列深度
#define EVENT_BUS_QUEUE_DEPTH       16

/**
 * @brief 事件类型枚举
 */
typedef enum {
    EVT_SENSOR_DATA = 0,    // 传感器数据更新
    EVT_HEALTH_ALERT,       // 健康告警事件
    EVT_FALL_DETECTED,      // 跌倒检测事件
    EVT_ALARM_STATE,        // 报警状态变更
    EVT_VOICE_CMD,          // 语音命令事件
    EVT_BUTTON,             // 按键事件
    EVT_BLE_CONN,           // BLE 连接状态
    EVT_SAMPLING_MODE,      // 采样模式变更
    EVT_TYPE_MAX            // 事件类型数量
} event_type_t;

/**
 * @brief 采样模式
 */
typedef enum {
    SAMPLING_MODE_NORMAL = 0,   // 常规采样
    SAMPLING_MODE_REALTIME      // 实时采样
} sampling_mode_t;

/**
 * @brief 传感器类型标志位
 */
typedef enum {
    SENSOR_TEMP     = (1 << 0),  // 体温传感器
    SENSOR_HR_SPO2  = (1 << 1),  // 心率血氧传感器
    SENSOR_IMU      = (1 << 2),  // IMU 传感器
    SENSOR_ALL      = 0x07       // 所有传感器
} sensor_type_t;

/**
 * @brief 传感器数据结构
 */
typedef struct {
    float temperature;      // 体温 (°C)
    uint8_t heart_rate;     // 心率 (bpm)
    uint8_t spo2;           // 血氧 (%)
    int16_t accel_x;        // 加速度 X
    int16_t accel_y;        // 加速度 Y
    int16_t accel_z;        // 加速度 Z
    int16_t gyro_x;         // 陀螺仪 X
    int16_t gyro_y;         // 陀螺仪 Y
    int16_t gyro_z;         // 陀螺仪 Z
    uint32_t steps;         // 步数
    uint32_t ppg_red;       // PPG RED 通道原始值
    uint32_t ppg_ir;        // PPG IR 通道原始值
    uint8_t data_valid;     // 数据有效标志位
    uint32_t timestamp;     // 时间戳 (ms)
} sensor_data_t;

/**
 * @brief 采样模式变更事件数据
 */
typedef struct {
    sampling_mode_t mode;       // 新的采样模式
    uint8_t sensor_mask;        // 受影响的传感器掩码
} sampling_mode_event_t;

/**
 * @brief 告警级别
 */
typedef enum {
    ALERT_LEVEL_NONE = 0,   // 无告警
    ALERT_LEVEL_INFO,       // 信息
    ALERT_LEVEL_WARNING,    // 预警
    ALERT_LEVEL_ALARM       // 报警
} alert_level_t;

/**
 * @brief 告警类型
 */
typedef enum {
    ALERT_TYPE_NONE = 0,
    ALERT_TYPE_TEMP_HIGH,       // 体温过高
    ALERT_TYPE_TEMP_LOW,        // 体温过低
    ALERT_TYPE_HR_HIGH,         // 心率过高
    ALERT_TYPE_HR_LOW,          // 心率过低
    ALERT_TYPE_SPO2_LOW,        // 血氧过低
    ALERT_TYPE_SPO2_WARNING,    // 血氧预警
    ALERT_TYPE_FALL             // 跌倒
} alert_type_t;

/**
 * @brief 健康告警事件数据
 */
typedef struct {
    alert_type_t type;          // 告警类型
    alert_level_t level;        // 告警级别
    int16_t value;              // 触发值 (×10)
    uint32_t timestamp;         // 时间戳
} health_alert_t;

/**
 * @brief 事件数据联合体
 */
typedef union {
    sensor_data_t sensor;           // 传感器数据
    sampling_mode_event_t sampling; // 采样模式变更
    health_alert_t health_alert;    // 健康告警
    uint32_t raw[12];               // 原始数据 (48 bytes，扩大以容纳 sensor_data_t)
} event_data_t;

/**
 * @brief 事件结构体
 */
typedef struct {
    event_type_t type;      // 事件类型
    event_data_t data;      // 事件数据
} event_t;

/**
 * @brief 事件处理回调函数类型
 */
typedef void (*event_handler_t)(const event_t *event, void *user_data);

/**
 * @brief 初始化事件总线
 * @return ESP_OK 成功
 */
esp_err_t event_bus_init(void);

/**
 * @brief 发布事件
 * @param type 事件类型
 * @param data 事件数据 (可为 NULL)
 * @return ESP_OK 成功
 */
esp_err_t event_publish(event_type_t type, const event_data_t *data);

/**
 * @brief 订阅事件
 * @param type 事件类型
 * @param handler 回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
esp_err_t event_subscribe(event_type_t type, event_handler_t handler, void *user_data);

/**
 * @brief 取消订阅事件
 * @param type 事件类型
 * @param handler 回调函数
 * @return ESP_OK 成功
 */
esp_err_t event_unsubscribe(event_type_t type, event_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H
