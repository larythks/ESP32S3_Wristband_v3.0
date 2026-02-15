/**
 * @file ble_service.h
 * @brief BLE GATT 服务 - NimBLE 初始化、GATT 注册、连接管理、Notify 发送
 *
 * 提供 CareBand 手环的 BLE 外设功能:
 * - 初始化 NimBLE 协议栈并开始广播
 * - 注册自定义 GATT 服务 (UUID: 0000FF00-...)
 * - 支持 Telemetry 和 Alarm 特征的 Notify 发送
 * - 连接状态查询
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "esp_err.h"
#include "ble_gatt_defs.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 服务
 *
 * 执行以下操作:
 * 1. 初始化 NimBLE 协议栈
 * 2. 设置设备名为 "CareBand"
 * 3. 注册 GATT 服务和特征
 * 4. 开始 BLE 广播
 * 5. 启动 Telemetry 定时上报任务
 *
 * @return ESP_OK 成功
 * @return ESP_FAIL 初始化失败
 * @return ESP_ERR_INVALID_STATE 已初始化
 */
esp_err_t ble_service_init(void);

/**
 * @brief 发送 Telemetry 数据 (Notify)
 *
 * 通过 Telemetry 特征 (UUID: FF01) 向已连接的客户端发送数据。
 * 客户端必须已启用该特征的 Notification (写入 CCCD)。
 *
 * @param data Telemetry 数据包指针 (20 bytes)
 * @return ESP_OK 发送成功
 * @return ESP_ERR_INVALID_STATE 未连接或客户端未订阅
 * @return ESP_ERR_INVALID_ARG data 为 NULL
 */
esp_err_t ble_notify_telemetry(const ble_telemetry_t *data);

/**
 * @brief 发送 Alarm 数据 (Notify)
 *
 * 通过 Alarm 特征 (UUID: FF02) 向已连接的客户端发送告警数据。
 * 客户端必须已启用该特征的 Notification (写入 CCCD)。
 *
 * @param data Alarm 数据包指针 (16 bytes)
 * @return ESP_OK 发送成功
 * @return ESP_ERR_INVALID_STATE 未连接或客户端未订阅
 * @return ESP_ERR_INVALID_ARG data 为 NULL
 */
esp_err_t ble_notify_alarm(const ble_alarm_t *data);

/**
 * @brief 查询 BLE 连接状态
 *
 * @return true  已有客户端连接
 * @return false 未连接
 */
bool ble_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SERVICE_H */
