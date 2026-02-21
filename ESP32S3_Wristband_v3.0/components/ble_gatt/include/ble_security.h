/**
 * @file ble_security.h
 * @brief BLE 安全管理器 - NimBLE SM 配置、nonce 防重放、加密状态查询
 */

#ifndef BLE_SECURITY_H
#define BLE_SECURITY_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* 前向声明 NimBLE 类型 */
struct ble_gap_event;

/* 安全配置常量 */
#define BLE_SECURITY_MAX_BONDS      1    /* 最大绑定设备数 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BLE 安全管理器
 * 配置 NimBLE SM: NoInputNoOutput, bonding, SC, no MITM
 *
 * @return ESP_OK 成功
 */
esp_err_t ble_security_init(void);

/**
 * @brief 校验 nonce 防重放
 * @param nonce 命令携带的 nonce 值
 * @return true  nonce 有效（大于历史值），已更新 last_nonce
 * @return false nonce 无效（小于等于历史值），命令应被拒绝
 */
bool ble_security_validate_nonce(uint32_t nonce);

/**
 * @brief 重置 nonce 计数器
 * 在 BLE 断开连接时调用，允许新连接从 nonce=1 重新开始
 */
void ble_security_reset_nonce(void);

/**
 * @brief 检查连接是否已加密
 * @param conn_handle 连接句柄
 * @return true  连接已加密
 * @return false 连接未加密或连接不存在
 */
bool ble_security_is_encrypted(uint16_t conn_handle);

/**
 * @brief 处理安全相关 GAP 事件
 * 应由 ble_service.c 的 GAP 事件处理器调用
 *
 * @param event GAP 事件指针
 * @return 0 表示已处理, 非0 表示未处理
 */
int ble_security_gap_event(struct ble_gap_event *event);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SECURITY_H */
