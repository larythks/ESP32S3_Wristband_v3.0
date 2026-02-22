/**
 * @file ui_manager.h
 * @brief UI 管理器头文件
 *
 * 管理 OLED 显示页面切换和手动测量模式
 */

#ifndef __UI_MANAGER_H__
#define __UI_MANAGER_H__

#include "esp_err.h"
#include "ds3231.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// UI 刷新间隔 (ms)
#define UI_REFRESH_INTERVAL_MS       480000  // 8 分钟（心率/血氧/体温）
#define UI_FAST_REFRESH_INTERVAL_MS  500     // 500ms（快速刷新周期）
#define UI_MANUAL_RESULT_DISPLAY_MS  5000    // 手动测量结果展示 5 秒

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

/**
 * @brief 预填充 HOME 页 RTC 时间缓存
 *
 * 在 ui_manager_init() 之后、sensor_service 启动之前调用，
 * 将 main 中已读取的 RTC 时间注入 UI 缓存，避免 UI 任务
 * 启动后因 I2C 总线竞争而延迟数十秒才显示时间。
 *
 * @param time 已从 DS3231 读取的有效时间，不可为 NULL
 */
void ui_manager_set_rtc_cache(const ds3231_time_t *time);

#ifdef __cplusplus
}
#endif

#endif /* __UI_MANAGER_H__ */
