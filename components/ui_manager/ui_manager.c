/**
 * @file ui_manager.c
 * @brief UI 管理器实现
 */

#include "ui_manager.h"
#include "sh1106.h"
#include "health_monitor.h"
#include "pedometer.h"
#include "sensor_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_manager";

/* 定时刷新定时器 */
static TimerHandle_t s_refresh_timer = NULL;      // 2分钟：心率/血氧/体温
static TimerHandle_t s_step_refresh_timer = NULL;  // 500ms：步数

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

/* 手动测量阶段 */
typedef enum {
    MANUAL_PHASE_COUNTDOWN = 0,  // 倒计时中（15秒）
    MANUAL_PHASE_RESULT          // 显示结果（5秒）
} manual_measure_phase_t;

static manual_measure_phase_t s_manual_phase = MANUAL_PHASE_COUNTDOWN;
static int64_t s_manual_start_us = 0;      // 测量起始时间 (us)
static int64_t s_result_start_us = 0;      // 结果展示起始时间 (us)

/**
 * @brief 绘制主页
 */
static void draw_home_page(void)
{
    char buf[32];
    health_status_t status = health_get_status();
    uint32_t steps = pedometer_get_steps();

    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Home --", 1);

    // 心率显示
    if (status.hr_validity == MEASURE_VALID) {
        snprintf(buf, sizeof(buf), "HR: %d bpm", status.heart_rate);
    } else {
        snprintf(buf, sizeof(buf), "HR: --");
    }
    sh1106_draw_string(0, 16, buf, 1);

    // 血氧显示
    if (status.spo2_validity == MEASURE_VALID) {
        snprintf(buf, sizeof(buf), "SpO2: %d%%", status.spo2);
    } else {
        snprintf(buf, sizeof(buf), "SpO2: --");
    }
    sh1106_draw_string(0, 28, buf, 1);

    // 体温显示
    if (status.temp_validity == MEASURE_VALID) {
        snprintf(buf, sizeof(buf), "Temp: %.1fC", status.temperature);
    } else {
        snprintf(buf, sizeof(buf), "Temp: --");
    }
    sh1106_draw_string(0, 40, buf, 1);

    // 步数显示
    snprintf(buf, sizeof(buf), "Steps: %lu", (unsigned long)steps);
    sh1106_draw_string(0, 52, buf, 1);

    sh1106_update();
}

/**
 * @brief 绘制心率页
 */
static void draw_heart_rate_page(void)
{
    char buf[32];
    health_status_t status = health_get_status();

    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Heart Rate --", 1);

    // 心率显示
    if (status.hr_validity == MEASURE_VALID) {
        snprintf(buf, sizeof(buf), "%d", status.heart_rate);
        sh1106_draw_string(40, 20, buf, 1);
        sh1106_draw_string(70, 20, "bpm", 1);
    } else {
        sh1106_draw_string(30, 20, "No Signal", 1);
    }

    // 血氧显示
    if (status.spo2_validity == MEASURE_VALID) {
        snprintf(buf, sizeof(buf), "SpO2: %d%%", status.spo2);
    } else {
        snprintf(buf, sizeof(buf), "SpO2: --");
    }
    sh1106_draw_string(0, 40, buf, 1);

    sh1106_update();
}

/**
 * @brief 绘制步数页
 */
static void draw_steps_page(void)
{
    char buf[32];
    uint32_t steps = pedometer_get_steps();

    sh1106_clear();
    sh1106_draw_string(0, 0, "-- Steps --", 1);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
    sh1106_draw_string(30, 24, buf, 1);
    sh1106_draw_string(80, 24, "steps", 1);

    sh1106_update();
}

/**
 * @brief 绘制手动测量页
 */
static void draw_manual_measure_page(void)
{
    char buf[32];
    sh1106_clear();

    if (s_manual_phase == MANUAL_PHASE_COUNTDOWN) {
        int64_t elapsed_us = esp_timer_get_time() - s_manual_start_us;
        int remaining_s = (SENSOR_HR_MEASURE_WINDOW_MS - (int)(elapsed_us / 1000)) / 1000;
        if (remaining_s < 0) remaining_s = 0;

        sh1106_draw_string(0, 0, "-- Measuring --", 1);
        snprintf(buf, sizeof(buf), "Time left: %ds", remaining_s);
        sh1106_draw_string(0, 20, buf, 1);
        sh1106_draw_string(0, 40, "Long press SW2", 1);
        sh1106_draw_string(0, 52, "to cancel", 1);
    } else {
        /* MANUAL_PHASE_RESULT */
        health_status_t status = health_get_status();

        sh1106_draw_string(0, 0, "-- Result --", 1);

        if (status.hr_validity == MEASURE_VALID) {
            snprintf(buf, sizeof(buf), "HR: %d bpm", status.heart_rate);
        } else {
            snprintf(buf, sizeof(buf), "HR: --");
        }
        sh1106_draw_string(0, 16, buf, 1);

        if (status.spo2_validity == MEASURE_VALID) {
            snprintf(buf, sizeof(buf), "SpO2: %d%%", status.spo2);
        } else {
            snprintf(buf, sizeof(buf), "SpO2: --");
        }
        sh1106_draw_string(0, 32, buf, 1);

        if (status.temp_validity == MEASURE_VALID) {
            snprintf(buf, sizeof(buf), "Temp: %.1fC", status.temperature);
        } else {
            snprintf(buf, sizeof(buf), "Temp: --");
        }
        sh1106_draw_string(0, 48, buf, 1);
    }

    sh1106_update();
}

/**
 * @brief 定时刷新回调函数（2分钟，心率/血氧/体温整页刷新）
 */
static void refresh_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGD(TAG, "Auto refresh UI");
    ui_update();
}

/**
 * @brief 步数定时刷新回调函数（500ms，仅刷新步数区域）
 */
static void step_refresh_timer_callback(TimerHandle_t timer)
{
    (void)timer;

    ui_page_t page = s_current_page;

    if (page == UI_PAGE_HOME) {
        char buf[32];
        uint32_t steps = pedometer_get_steps();
        /* 主页步数位于 y=52 行，先用黑色清除该行再绘制 */
        sh1106_draw_string(0, 52, "                ", 1);
        snprintf(buf, sizeof(buf), "Steps: %lu", (unsigned long)steps);
        sh1106_draw_string(0, 52, buf, 1);
        sh1106_update();
    } else if (page == UI_PAGE_STEPS) {
        char buf[32];
        uint32_t steps = pedometer_get_steps();
        /* 步数页数字位于 y=24 行 */
        sh1106_draw_string(30, 24, "                ", 1);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
        sh1106_draw_string(30, 24, buf, 1);
        sh1106_draw_string(80, 24, "steps", 1);
        sh1106_update();
    } else if (page == UI_PAGE_MANUAL_MEASURE && s_manual_measuring) {
        int64_t now_us = esp_timer_get_time();

        if (s_manual_phase == MANUAL_PHASE_COUNTDOWN) {
            int64_t elapsed_ms = (now_us - s_manual_start_us) / 1000;
            if (elapsed_ms >= SENSOR_HR_MEASURE_WINDOW_MS) {
                /* 倒计时结束，切换到结果展示阶段 */
                s_manual_phase = MANUAL_PHASE_RESULT;
                s_result_start_us = now_us;
                ESP_LOGI("ui_manager", "Manual measure done, showing result");
            }
            /* 刷新页面（倒计时或刚切换到结果） */
            draw_manual_measure_page();
        } else {
            /* MANUAL_PHASE_RESULT */
            int64_t result_elapsed_ms = (now_us - s_result_start_us) / 1000;
            if (result_elapsed_ms >= UI_MANUAL_RESULT_DISPLAY_MS) {
                /* 结果展示 5 秒结束，自动退出 */
                ESP_LOGI("ui_manager", "Result display timeout, auto exit");
                ui_exit_manual_measure();
            }
        }
    }
}

/**
 * @brief 初始化 UI 管理器
 */
esp_err_t ui_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing UI manager");
    s_current_page = UI_PAGE_HOME;
    s_manual_measuring = false;

    // 创建定时刷新定时器
    s_refresh_timer = xTimerCreate(
        "ui_refresh",
        pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS),
        pdTRUE,     // 自动重载
        NULL,
        refresh_timer_callback
    );

    if (s_refresh_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create refresh timer");
        return ESP_ERR_NO_MEM;
    }

    // 启动定时器
    if (xTimerStart(s_refresh_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start refresh timer");
        return ESP_FAIL;
    }

    // 创建步数刷新定时器（500ms）
    s_step_refresh_timer = xTimerCreate(
        "ui_step",
        pdMS_TO_TICKS(UI_STEP_REFRESH_INTERVAL_MS),
        pdTRUE,
        NULL,
        step_refresh_timer_callback
    );

    if (s_step_refresh_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create step refresh timer");
        return ESP_ERR_NO_MEM;
    }

    if (xTimerStart(s_step_refresh_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start step refresh timer");
        return ESP_FAIL;
    }

    ui_update();
    ESP_LOGI(TAG, "UI manager initialized (health refresh %d ms, step refresh %d ms)",
             UI_REFRESH_INTERVAL_MS, UI_STEP_REFRESH_INTERVAL_MS);
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
        s_manual_phase = MANUAL_PHASE_COUNTDOWN;
        s_manual_start_us = esp_timer_get_time();
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
