/**
 * @file alarm_manager.h
 * @brief 报警管理器 - 统一管理健康报警、跌倒报警、手动报警的状态机
 *
 * 状态转换规则:
 *   IDLE -> PRE_ALARM  : 跌倒检测触发 (开始 15 秒倒计时)
 *   IDLE -> ALARMING   : 健康报警直接触发 (体温/心率/血氧越阈确认后)
 *   IDLE -> ALARMING   : SW1 手动报警
 *   PRE_ALARM -> ALARMING : 倒计时到期无取消
 *   PRE_ALARM -> IDLE     : SW1 按键取消 / 语音 "取消"
 *   ALARMING -> ACKED     : App 发送 ACK 命令
 *   ALARMING -> IDLE      : 本地取消 (SW1 长按)
 *   ACKED -> IDLE         : 超时自动清除 或 新报警触发
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include "esp_err.h"
#include "event_bus.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 报警缓存配置 ===== */
#define ALARM_CACHE_SIZE            16
#define ALARM_RETRY_MAX             5
#define ALARM_RETRY_INTERVAL_MS     2000

/**
 * @brief 报警状态枚举
 */
typedef enum {
    ALARM_STATE_IDLE = 0,       // 空闲，无报警
    ALARM_STATE_PRE_ALARM,      // 预报警 (跌倒专用，15 秒确认倒计时)
    ALARM_STATE_ALARMING,       // 报警中 (声光报警 + BLE 通知)
    ALARM_STATE_ACKED,          // 已确认 (App 已 ACK)
} alarm_state_t;

/**
 * @brief 报警数据
 */
typedef struct {
    alert_type_t type;          // 报警类型 (来自 event_bus.h alert_type_t)
    alert_level_t level;        // 报警级别 (来自 event_bus.h alert_level_t)
    int16_t value;              // 触发值 (x10, 如体温 37.5 存为 375)
    uint32_t timestamp;         // 触发时间戳 (ms)
} alarm_data_t;

/**
 * @brief 报警缓存条目
 */
typedef struct {
    uint32_t event_id;
    uint8_t  alarm_type;
    int16_t  value;
    uint32_t timestamp;
    uint8_t  retry_count;
    bool     acked;
    bool     valid;
} alarm_cache_entry_t;

/**
 * @brief 初始化报警管理器
 *
 * 创建内部定时器和事件订阅，初始状态为 ALARM_STATE_IDLE。
 *
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t alarm_manager_init(void);

/**
 * @brief 触发报警
 *
 * 根据报警类型进入不同状态:
 * - ALERT_TYPE_FALL: 进入 PRE_ALARM (15 秒倒计时)
 * - 其他类型: 直接进入 ALARMING
 *
 * @param type 报警类型
 * @param data 报警数据 (可为 NULL，type 字段会被自动设置)
 */
void alarm_trigger(alert_type_t type, alarm_data_t *data);

/**
 * @brief 取消报警
 *
 * 在 PRE_ALARM 或 ALARMING 状态下取消报警，回到 IDLE。
 * 对应操作: SW1 按键取消 (PRE_ALARM) 或 SW1 长按取消 (ALARMING)
 */
void alarm_cancel(void);

/**
 * @brief 确认报警 (App ACK)
 *
 * 在 ALARMING 状态下收到 App 确认，进入 ACKED 状态。
 * ACKED 状态会在超时后自动回到 IDLE。
 *
 * @param event_id 报警事件 ID
 */
void alarm_ack(uint32_t event_id);

/**
 * @brief 在缓存中查找指定 event_id 的报警条目
 *
 * @param event_id 报警事件 ID
 * @param out 输出缓存条目
 * @return true 找到, false 未找到
 */
bool alarm_cache_find(uint32_t event_id, alarm_cache_entry_t *out);

/**
 * @brief 获取当前报警状态
 *
 * @return 当前报警状态
 */
alarm_state_t alarm_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // ALARM_MANAGER_H
