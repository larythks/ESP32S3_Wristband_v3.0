/**
 * @file ui_manager.c
 * @brief UI 管理器实现
 */

#include "ui_manager.h"
#include "sh1106.h"
#include "font_cn.h"
#include "font_large.h"
#include "ds3231.h"
#include "health_monitor.h"
#include "pedometer.h"
#include "sensor_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_manager";

/* UI 独立刷新任务 */
#define UI_TASK_STACK_SIZE  4096
#define UI_TASK_PRIORITY    2       // 高于 Timer Service(1)，低于 sensor_task(6)
#define UI_NOTIFY_REFRESH   (1 << 0)

static TaskHandle_t s_ui_task_handle = NULL;       // UI 刷新任务句柄
static SemaphoreHandle_t s_ui_mutex = NULL;        // UI 绘制互斥锁

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
static int s_home_last_minute = -1;

static bool ui_lock(TickType_t timeout)
{
    if (s_ui_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_ui_mutex, timeout) == pdTRUE;
}

static void ui_unlock(void)
{
    if (s_ui_mutex != NULL) {
        xSemaphoreGive(s_ui_mutex);
    }
}

static uint8_t get_weekday_cn_index(uint16_t year, uint8_t month, uint8_t day)
{
    // Tomohiko Sakamoto 算法: 0=Sunday ... 6=Saturday
    static const uint8_t offset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t y = year;
    if (month < 3) {
        y--;
    }
    uint8_t w = (uint8_t)((y + y / 4 - y / 100 + y / 400 + offset[month - 1] + day) % 7);
    if (w == 0) {
        return FONT_CN_RI;
    }
    return (uint8_t)(FONT_CN_YI + (w - 1));
}

/* 手动测量阶段 */
typedef enum {
    MANUAL_PHASE_COUNTDOWN = 0,  // 倒计时中（15秒）
    MANUAL_PHASE_WAIT_RESULT,    // 等待 health_monitor 计算完毕
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
    char date_buf[8];
    char temp_buf[12];
    char time_buf[8];
    health_status_t status = health_get_status();
    ds3231_time_t rtc_time = {0};
    bool rtc_ok = (ds3231_get_time(&rtc_time) == ESP_OK);

    sh1106_clear();

    // 第一行：日期 + 中文星期
    if (rtc_ok) {
        snprintf(date_buf, sizeof(date_buf), "%02u/%02u", rtc_time.month, rtc_time.day);
        sh1106_draw_string(0, 0, date_buf, 1);

        int16_t week_x = 45;  // "MM/DD " 占 6*6=36 像素，留点间距到 45
        sh1106_draw_chinese(week_x, 0, FONT_CN_XING, 1);
        sh1106_draw_chinese(week_x + 13, 0, FONT_CN_QI, 1);
        sh1106_draw_chinese(week_x + 26, 0,
                            get_weekday_cn_index(rtc_time.year, rtc_time.month, rtc_time.day), 1);

        snprintf(time_buf, sizeof(time_buf), "%02u:%02u", rtc_time.hour, rtc_time.minute);
        s_home_last_minute = rtc_time.minute;
    } else {
        sh1106_draw_string(0, 0, "--/-- RTC", 1);
        snprintf(time_buf, sizeof(time_buf), "00:00");
        s_home_last_minute = -1;  // 强制下次刷新周期重绘
    }

    // 第一行右侧温度
    if (status.temp_validity == MEASURE_VALID) {
        snprintf(temp_buf, sizeof(temp_buf), "%.1fC", status.temperature);
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "--.-C");
    }
    int16_t temp_x = SH1106_WIDTH - (int16_t)(strlen(temp_buf) * 6);
    if (temp_x < 78) {
        temp_x = 78;
    }
    sh1106_draw_string(temp_x, 0, temp_buf, 1);

    // 中间区域：大字体时间 HH:MM
    int16_t time_width = (int16_t)(5 * FONT_LARGE_WIDTH + 4 * FONT_LARGE_CHAR_SPACING);
    int16_t time_x = (SH1106_WIDTH - time_width) / 2;
    sh1106_draw_string_large(time_x, 20, time_buf, 1);

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
        snprintf(buf, sizeof(buf), "HR: %d bpm", status.heart_rate);
    } else {
        snprintf(buf, sizeof(buf), "HR: --");
    }
    sh1106_draw_string(0, 20, buf, 1);

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
    } else if (s_manual_phase == MANUAL_PHASE_WAIT_RESULT) {
        sh1106_draw_string(0, 0, "-- Measuring --", 1);
        sh1106_draw_string(0, 28, "Calculating...", 1);
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

static void ui_update_locked(void)
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

static void ui_switch_page_locked(ui_page_t page)
{
    if (page < UI_PAGE_MAX && !s_manual_measuring) {
        s_current_page = page;
        ESP_LOGI(TAG, "Switch to page: %s", s_page_names[page]);
        ui_update_locked();
    }
}

static void ui_enter_manual_measure_locked(void)
{
    if (!s_manual_measuring) {
        s_page_before_measure = s_current_page;
        s_manual_measuring = true;
        s_current_page = UI_PAGE_MANUAL_MEASURE;
        s_manual_phase = MANUAL_PHASE_COUNTDOWN;
        s_manual_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Enter manual measure mode");
        ui_update_locked();
    }
}

static void ui_exit_manual_measure_locked(void)
{
    if (s_manual_measuring) {
        s_manual_measuring = false;
        s_current_page = s_page_before_measure;
        ESP_LOGI(TAG, "Exit manual measure mode");
        ui_update_locked();
    }
}

/**
 * @brief UI 刷新任务（独立任务，替代 Timer Service 回调）
 *
 * 以 500ms 为基础周期运行，承担以下职责：
 * - 每 2 分钟全量刷新当前页面（心率/血氧/环境温度等数据更新）
 * - HOME 页：每 10 秒检查 RTC 分钟变化，触发重绘
 * - STEPS 页：每 500ms 局部刷新步数数字
 * - MANUAL_MEASURE 页：管理倒计时/等待/结果三阶段状态机
 */
static void ui_task(void *arg)
{
    (void)arg;

    /* 2 分钟全量刷新计数器: 120000ms / 500ms = 240 次 */
    uint32_t full_refresh_counter = 0;
    const uint32_t full_refresh_cycles = UI_REFRESH_INTERVAL_MS / UI_FAST_REFRESH_INTERVAL_MS;

    /* HOME 页 RTC 检查计数器: 每 20 次 (10秒) 读一次 DS3231 */
    uint8_t rtc_check_counter = 0;

    while (1) {
        uint32_t notify_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notify_value,
                        pdMS_TO_TICKS(UI_FAST_REFRESH_INTERVAL_MS));

        if (!ui_lock(pdMS_TO_TICKS(50))) {
            ESP_LOGD(TAG, "Skip UI cycle: lock busy");
            continue;
        }

        /* 外部通知或到达 2 分钟全量刷新周期 */
        if ((notify_value & UI_NOTIFY_REFRESH) ||
            ++full_refresh_counter >= full_refresh_cycles) {
            full_refresh_counter = 0;
            ESP_LOGD(TAG, "Full UI refresh");
            ui_update_locked();
            ui_unlock();
            continue;
        }

        /* --- 500ms 快速刷新逻辑 --- */
        ui_page_t page = s_current_page;

        if (page == UI_PAGE_HOME) {
            /* 每 20 次循环 (10秒) 才读一次 RTC，避免高频 I2C 竞争 */
            if (++rtc_check_counter >= 20) {
                rtc_check_counter = 0;
                ds3231_time_t rtc_time = {0};
                if (ds3231_get_time(&rtc_time) == ESP_OK) {
                    if (s_home_last_minute != (int)rtc_time.minute) {
                        draw_home_page();
                    }
                }
            }
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
                    /* 倒计时结束，进入等待阶段，等 health_monitor 完成计算 */
                    s_manual_phase = MANUAL_PHASE_WAIT_RESULT;
                    ESP_LOGI(TAG, "Manual measure countdown done, waiting for result");
                }
                draw_manual_measure_page();
            } else if (s_manual_phase == MANUAL_PHASE_WAIT_RESULT) {
                /* 等待一个刷新周期（500ms），确保 health_monitor 已完成计算 */
                s_manual_phase = MANUAL_PHASE_RESULT;
                s_result_start_us = now_us;
                ESP_LOGI(TAG, "Showing manual measure result");
                draw_manual_measure_page();
            } else {
                /* MANUAL_PHASE_RESULT */
                int64_t result_elapsed_ms = (now_us - s_result_start_us) / 1000;
                if (result_elapsed_ms >= UI_MANUAL_RESULT_DISPLAY_MS) {
                    /* 结果展示 5 秒结束，自动退出 */
                    ESP_LOGI(TAG, "Result display timeout, auto exit");
                    ui_exit_manual_measure_locked();
                }
            }
        }

        ui_unlock();
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
    s_home_last_minute = -1;

    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    // 首次绘制
    if (ui_lock(pdMS_TO_TICKS(50))) {
        ui_update_locked();
        ui_unlock();
    }

    // 创建 UI 刷新任务（替代定时器回调，避免阻塞 Timer Service）
    BaseType_t ret = xTaskCreate(
        ui_task,
        "ui_task",
        UI_TASK_STACK_SIZE,
        NULL,
        UI_TASK_PRIORITY,
        &s_ui_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI manager initialized (full refresh %d ms, fast refresh %d ms)",
             UI_REFRESH_INTERVAL_MS, UI_FAST_REFRESH_INTERVAL_MS);
    return ESP_OK;
}

/**
 * @brief 切换到指定页面
 */
void ui_switch_page(ui_page_t page)
{
    if (!ui_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock UI for page switch");
        return;
    }

    ui_switch_page_locked(page);
    ui_unlock();
}

/**
 * @brief 切换到下一页
 */
void ui_next_page(void)
{
    if (!ui_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock UI for next page");
        return;
    }

    if (!s_manual_measuring) {
        ui_page_t next = (s_current_page + 1) % (UI_PAGE_MAX - 1);
        ui_switch_page_locked(next);
    }

    ui_unlock();
}

/**
 * @brief 获取当前页面
 */
ui_page_t ui_get_current_page(void)
{
    ui_page_t page = s_current_page;
    if (ui_lock(pdMS_TO_TICKS(20))) {
        page = s_current_page;
        ui_unlock();
    }
    return page;
}

/**
 * @brief 进入手动测量模式
 */
void ui_enter_manual_measure(void)
{
    if (!ui_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock UI for manual measure entry");
        return;
    }

    ui_enter_manual_measure_locked();
    ui_unlock();
}

/**
 * @brief 退出手动测量模式
 */
void ui_exit_manual_measure(void)
{
    if (!ui_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock UI for manual measure exit");
        return;
    }

    ui_exit_manual_measure_locked();
    ui_unlock();
}

/**
 * @brief 检查是否在手动测量模式
 */
bool ui_is_manual_measuring(void)
{
    bool measuring = s_manual_measuring;
    if (ui_lock(pdMS_TO_TICKS(20))) {
        measuring = s_manual_measuring;
        ui_unlock();
    }
    return measuring;
}

/**
 * @brief 更新 UI 显示
 */
void ui_update(void)
{
    if (!ui_lock(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock UI for update");
        return;
    }

    ui_update_locked();
    ui_unlock();
}
