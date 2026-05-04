/**
 * @file pedometer.c
 * @brief 计步服务实现 - 基于加速度峰值检测
 */

#include "pedometer.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "pedometer";

// ============== 内部数据结构 ==============

typedef struct {
    bool initialized;
    bool running;

    // 步数统计
    uint32_t steps;

    // 峰值检测状态
    uint32_t last_step_time;
    int32_t last_svm;
    bool above_threshold;

    // 滤波缓冲
    int32_t filter_buffer[4];
    uint8_t filter_index;
} pedometer_ctx_t;

static pedometer_ctx_t s_ctx = {0};

// ============== 前向声明 ==============

static void on_imu_data(const event_t *event, void *user_data);
static void pedometer_feed_data(int16_t ax, int16_t ay, int16_t az, uint32_t timestamp);
static int32_t calculate_svm(int16_t ax, int16_t ay, int16_t az);
static int32_t apply_filter(int32_t value);

// ============== API 实现 ==============

esp_err_t pedometer_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));

    ESP_LOGI(TAG, "Pedometer initialized");
    s_ctx.initialized = true;
    return ESP_OK;
}

esp_err_t pedometer_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.running) {
        return ESP_OK;
    }

    // 订阅高频 IMU 数据事件
    esp_err_t ret = event_subscribe(EVT_IMU_DATA, on_imu_data, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe");
        return ret;
    }

    s_ctx.running = true;
    ESP_LOGI(TAG, "Pedometer started");
    return ESP_OK;
}

esp_err_t pedometer_stop(void)
{
    if (!s_ctx.running) {
        return ESP_OK;
    }

    event_unsubscribe(EVT_IMU_DATA, on_imu_data);
    s_ctx.running = false;
    ESP_LOGI(TAG, "Pedometer stopped");
    return ESP_OK;
}

uint32_t pedometer_get_steps(void)
{
    return s_ctx.steps;
}

void pedometer_reset(void)
{
    s_ctx.steps = 0;
    ESP_LOGI(TAG, "Steps reset");
}

// ============== 数据输入接口 ==============

static void pedometer_feed_data(int16_t ax, int16_t ay, int16_t az, uint32_t timestamp)
{
    if (!s_ctx.running) {
        return;
    }

    // 计算 SVM
    int32_t svm = calculate_svm(ax, ay, az);

    // 滤波
    int32_t filtered = apply_filter(svm);

    // 峰值检测
    bool is_peak = false;

    if (filtered > PEDOMETER_THRESHOLD_HIGH && !s_ctx.above_threshold) {
        s_ctx.above_threshold = true;
    } else if (filtered < PEDOMETER_THRESHOLD_LOW && s_ctx.above_threshold) {
        s_ctx.above_threshold = false;
        is_peak = true;
    }

    // 步数判定
    if (is_peak) {
        uint32_t interval = timestamp - s_ctx.last_step_time;

        if (interval >= PEDOMETER_MIN_INTERVAL_MS &&
            interval <= PEDOMETER_MAX_INTERVAL_MS) {
            s_ctx.steps++;
        }

        s_ctx.last_step_time = timestamp;
    }

    s_ctx.last_svm = filtered;
}

// ============== 事件处理 ==============

static void on_imu_data(const event_t *event, void *user_data)
{
    (void)user_data;

    if (!s_ctx.running || event == NULL) {
        return;
    }

    const imu_data_t *data = &event->data.imu;

    // 使用高频加速度数据进行计步
    pedometer_feed_data(data->accel_x, data->accel_y, data->accel_z,
                        data->timestamp);
}

// ============== SVM 计算 ==============

static int32_t calculate_svm(int16_t ax, int16_t ay, int16_t az)
{
    // SVM = sqrt(ax^2 + ay^2 + az^2)
    int32_t sum = (int32_t)ax * ax + (int32_t)ay * ay + (int32_t)az * az;
    return (int32_t)sqrtf((float)sum);
}

// ============== 滑动平均滤波 ==============

static int32_t apply_filter(int32_t value)
{
    s_ctx.filter_buffer[s_ctx.filter_index] = value;
    s_ctx.filter_index = (s_ctx.filter_index + 1) % 4;

    int32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += s_ctx.filter_buffer[i];
    }
    return sum / 4;
}
