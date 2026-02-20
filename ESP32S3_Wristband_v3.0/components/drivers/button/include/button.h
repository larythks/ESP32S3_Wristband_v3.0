/**
 * @file button.h
 * @brief 按键驱动头文件
 *
 * 支持短按/长按检测，带消抖处理
 */

#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按键 ID 枚举
 */
typedef enum {
    BUTTON_ID_SW1 = 0,  // 报警键 GPIO7
    BUTTON_ID_SW2,      // 交互键 GPIO6
    BUTTON_ID_MAX
} button_id_t;

/**
 * @brief 按键事件类型
 */
typedef enum {
    BUTTON_EVENT_SHORT_PRESS = 0,  // 短按事件
    BUTTON_EVENT_LONG_PRESS,       // 长按事件
    BUTTON_EVENT_MAX
} button_event_t;

/**
 * @brief 按键事件回调函数类型
 * @param id 按键 ID
 * @param event 事件类型
 */
typedef void (*button_cb_t)(button_id_t id, button_event_t event);

/**
 * @brief 初始化按键驱动
 * @return ESP_OK 成功，其他失败
 */
esp_err_t button_init(void);

/**
 * @brief 注册按键事件回调
 * @param id 按键 ID
 * @param event 事件类型
 * @param cb 回调函数
 */
void button_register_cb(button_id_t id, button_event_t event, button_cb_t cb);

/**
 * @brief 反初始化按键驱动
 */
void button_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_H__ */
