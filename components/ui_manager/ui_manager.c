/**
 * @file ui_manager.c
 * @brief UI 管理器实现
 */

#include "ui_manager.h"
#include "sh1106.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_manager";

/* 页面名称 */
static const char *s_page_names[] = {
    "Home",
    "Heart Rate",
    "Steps",
    "Manual Measure"
};

/* UI 状态 */
static ui_page_t s_current_page = UI_PAGE_HOME;
static bool s_manual_measuring = false;
static ui_page_t s_page_before_measure = UI_PAGE_HOME;

/* 模拟数据（后续由传感器服务提供） */
static int s_heart_rate = 72;
static int s_spo2 = 98;
static uint32_t s_steps = 1234;
static float s_temperature = 36.5f;

/**
 * @brief 绘制主页
 */
static void draw_home_page(void)
{
    char buf[32];
    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Home --", 1);

    snprintf(buf, sizeof(buf), "HR: %d bpm", s_heart_rate);
    sh1106_draw_string(0, 16, buf, 1);

    snprintf(buf, sizeof(buf), "SpO2: %d%%", s_spo2);
    sh1106_draw_string(0, 28, buf, 1);

    snprintf(buf, sizeof(buf), "Temp: %.1fC", s_temperature);
    sh1106_draw_string(0, 40, buf, 1);

    sh1106_update();
}

/**
 * @brief 绘制心率页
 */
static void draw_heart_rate_page(void)
{
    char buf[32];
    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Heart Rate --", 1);

    snprintf(buf, sizeof(buf), "%d", s_heart_rate);
    sh1106_draw_string(40, 24, buf, 1);
    sh1106_draw_string(70, 24, "bpm", 1);

    sh1106_update();
}

/**
 * @brief 绘制步数页
 */
static void draw_steps_page(void)
{
    char buf[32];
    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Steps --", 1);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_steps);
    sh1106_draw_string(30, 24, buf, 1);
    sh1106_draw_string(80, 24, "steps", 1);

    sh1106_update();
}

/**
 * @brief 绘制手动测量页
 */
static void draw_manual_measure_page(void)
{
    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Manual Measure --", 1);
    sh1106_draw_string(0, 20, "Measuring...", 1);
    sh1106_draw_string(0, 36, "Long press SW2", 1);
    sh1106_draw_string(0, 48, "to cancel", 1);
    sh1106_update();
}

/**
 * @brief 初始化 UI 管理器
 */
esp_err_t ui_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing UI manager");
    s_current_page = UI_PAGE_HOME;
    s_manual_measuring = false;
    ui_update();
    ESP_LOGI(TAG, "UI manager initialized");
    return ESP_OK;
}

/**
 * @brief 切换到指定页面
 */
void ui_switch_page(ui_page_t page)
{
    if (page < UI_PAGE_MAX && !s_manual_measuring) {
        s_current_page = page;
        ESP_LOGI(TAG, "Switch to page: %s", s_page_names[page]);
        ui_update();
    }
}

/**
 * @brief 切换到下一页
 */
void ui_next_page(void)
{
    if (s_manual_measuring) {
        return;
    }
    ui_page_t next = (s_current_page + 1) % (UI_PAGE_MAX - 1);
    ui_switch_page(next);
}

/**
 * @brief 获取当前页面
 */
ui_page_t ui_get_current_page(void)
{
    return s_current_page;
}

/**
 * @brief 进入手动测量模式
 */
void ui_enter_manual_measure(void)
{
    if (!s_manual_measuring) {
        s_page_before_measure = s_current_page;
        s_manual_measuring = true;
        s_current_page = UI_PAGE_MANUAL_MEASURE;
        ESP_LOGI(TAG, "Enter manual measure mode");
        ui_update();
    }
}

/**
 * @brief 退出手动测量模式
 */
void ui_exit_manual_measure(void)
{
    if (s_manual_measuring) {
        s_manual_measuring = false;
        s_current_page = s_page_before_measure;
        ESP_LOGI(TAG, "Exit manual measure mode");
        ui_update();
    }
}

/**
 * @brief 检查是否在手动测量模式
 */
bool ui_is_manual_measuring(void)
{
    return s_manual_measuring;
}

/**
 * @brief 更新 UI 显示
 */
void ui_update(void)
{
    switch (s_current_page) {
        case UI_PAGE_HOME:
            draw_home_page();
            break;
        case UI_PAGE_HEART_RATE:
            draw_heart_rate_page();
            break;
        case UI_PAGE_STEPS:
            draw_steps_page();
            break;
        case UI_PAGE_MANUAL_MEASURE:
            draw_manual_measure_page();
            break;
        default:
            break;
    }
}
