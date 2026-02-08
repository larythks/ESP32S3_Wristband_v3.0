/**
 * @file ui_manager.h
 * @brief UI 管理器头文件
 *
 * 管理 OLED 显示页面切换和手动测量模式
 */

#ifndef __UI_MANAGER_H__
#define __UI_MANAGER_H__

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI 页面枚举
 */
typedef enum {
    UI_PAGE_HOME = 0,       // 主页：时间、基础信息
    UI_PAGE_HEART_RATE,     // 心率页
    UI_PAGE_STEPS,          // 步数页
    UI_PAGE_MANUAL_MEASURE, // 手动测量页
    UI_PAGE_MAX
} ui_page_t;

/**
 * @brief 初始化 UI 管理器
 * @return ESP_OK 成功，其他失败
 */
esp_err_t ui_manager_init(void);

/**
 * @brief 切换到指定页面
 * @param page 目标页面
 */
void ui_switch_page(ui_page_t page);

/**
 * @brief 切换到下一页
 */
void ui_next_page(void);

/**
 * @brief 获取当前页面
 * @return 当前页面
 */
ui_page_t ui_get_current_page(void);

/**
 * @brief 进入手动测量模式
 */
void ui_enter_manual_measure(void);

/**
 * @brief 退出手动测量模式
 */
void ui_exit_manual_measure(void);

/**
 * @brief 检查是否在手动测量模式
 * @return true 在手动测量模式
 */
bool ui_is_manual_measuring(void);

/**
 * @brief 更新 UI 显示
 */
void ui_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_MANAGER_H__ */
