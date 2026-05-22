/**
 * @file fall_detect.c
 * @brief 跌倒检测算法实现
 *
 * 算法原理：
 * 1. 自由落体检测：跌倒前通常有短暂失重（SVM < 0.6g）
 * 2. 冲击检测：跌倒瞬间产生大加速度（SVM > 2.0g）
 * 3. 静止检测：跌倒后身体保持静止（SVM ≈ 1g）
 * 4. 姿态变化：从直立变为水平
 */

#include "fall_detect.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "fall_detect";

// MPU6050 加速度量程转换 (±8g 模式, 4096 LSB/g)
#define ACCEL_SENSITIVITY   4096.0f

// ============================================================================
// 内部数据结构
// ============================================================================

/**
 * @brief 加速度样本
 */
typedef struct {
    float ax;           // 加速度 X (g)
    float ay;           // 加速度 Y (g)
    float az;           // 加速度 Z (g)
    float svm;          // 矢量幅值 (g)
    uint32_t timestamp; // 时间戳 (ms)
} accel_sample_t;

/**
 * @brief 跌倒检测上下文
 */
typedef struct {
    // 状态
    bool initialized;
    bool running;
    fall_state_t state;

    // 历史数据环形缓冲区
    accel_sample_t history[FALL_HISTORY_SIZE];
    uint16_t history_head;
    uint16_t history_count;

    // 检测过程数据
    uint32_t state_enter_time;      // 进入当前状态的时间
    float peak_svm;                 // 检测到的峰值 SVM
    float pre_fall_orientation[3]; // 跌倒前姿态
    uint32_t freefall_start;        // 自由落体开始时间
    uint32_t impact_time;           // 冲击时间
    uint32_t static_start;          // 静止开始时间

    // 最近事件
    fall_event_data_t last_event;
    bool has_event;

    // 统计
    fall_detect_stats_t stats;
} fall_detect_ctx_t;

static fall_detect_ctx_t s_ctx = {0};

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 将原始加速度值转换为 g
 */
static float raw_to_g(int16_t raw)
{
    return (float)raw / ACCEL_SENSITIVITY;
}

/**
 * @brief 计算 SVM (Signal Vector Magnitude)
 */
static float calculate_svm(float ax, float ay, float az)
{
    return sqrtf(ax * ax + ay * ay + az * az);
}

/**
 * @brief 归一化加速度向量（用于姿态判断）
 */
static void normalize_orientation(float ax, float ay, float az, float *out)
{
    float mag = calculate_svm(ax, ay, az);
    if (mag > 0.01f) {
        out[0] = ax / mag;
        out[1] = ay / mag;
        out[2] = az / mag;
    } else {
        out[0] = 0;
        out[1] = 0;
        out[2] = 1.0f;
    }
}

/**
 * @brief 计算两个姿态向量的点积（用于判断姿态变化）
 */
static float orientation_dot_product(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/**
 * @brief 添加样本到历史缓冲区
 */
static void add_to_history(const accel_sample_t *sample)
{
    s_ctx.history[s_ctx.history_head] = *sample;
    s_ctx.history_head = (s_ctx.history_head + 1) % FALL_HISTORY_SIZE;
    if (s_ctx.history_count < FALL_HISTORY_SIZE) {
        s_ctx.history_count++;
    }
}

/**
 * @brief 获取历史样本（0 = 最新，负数 = 更早）
 */
static const accel_sample_t* get_history_sample(int offset)
{
    if (s_ctx.history_count == 0) return NULL;

    int idx = (int)s_ctx.history_head - 1 + offset;
    while (idx < 0) idx += FALL_HISTORY_SIZE;
    idx = idx % FALL_HISTORY_SIZE;

    return &s_ctx.history[idx];
}

/**
 * @brief 切换状态
 */
static void change_state(fall_state_t new_state)
{
    if (s_ctx.state != new_state) {
        ESP_LOGD(TAG, "State: %d -> %d", s_ctx.state, new_state);
        s_ctx.state = new_state;
        s_ctx.state_enter_time = get_timestamp_ms();
    }
}

/**
 * @brief 发布跌倒检测事件
 */
static void publish_fall_event(void)
{
    // 填充事件数据
    s_ctx.last_event.timestamp = get_timestamp_ms();
    s_ctx.last_event.peak_svm = s_ctx.peak_svm;
    s_ctx.has_event = true;

    // 更新统计
    s_ctx.stats.total_detections++;

    // 发布事件到事件总线
    event_data_t evt_data;
    evt_data.health_alert.type = ALERT_TYPE_FALL;
    evt_data.health_alert.level = ALERT_LEVEL_ALARM;
    evt_data.health_alert.value = (int16_t)(s_ctx.peak_svm * 10);
    evt_data.health_alert.timestamp = s_ctx.last_event.timestamp;

    event_publish(EVT_FALL_DETECTED, &evt_data);

    ESP_LOGW(TAG, "Fall detected! Peak SVM: %.2fg", s_ctx.peak_svm);
}

// ============================================================================
// 状态机处理函数
// ============================================================================

/**
 * @brief 处理 IDLE 状态
 */
static void process_state_idle(const accel_sample_t *sample)
{
    // 检测自由落体（SVM 突然降低）
    if (sample->svm < FALL_FREEFALL_THRESHOLD) {
        s_ctx.freefall_start = sample->timestamp;
        // 记录跌倒前姿态
        normalize_orientation(sample->ax, sample->ay, sample->az,
                              s_ctx.pre_fall_orientation);
        change_state(FALL_STATE_FREEFALL);
        ESP_LOGD(TAG, "Freefall detected, SVM=%.2fg", sample->svm);
        return;
    }

    // 直接检测冲击（有些跌倒没有明显自由落体阶段）
    if (sample->svm > FALL_IMPACT_THRESHOLD) {
        s_ctx.peak_svm = sample->svm;
        s_ctx.impact_time = sample->timestamp;
        // 从历史数据获取跌倒前姿态
        const accel_sample_t *pre = get_history_sample(-100);
        if (pre) {
            normalize_orientation(pre->ax, pre->ay, pre->az,
                                  s_ctx.pre_fall_orientation);
        }
        change_state(FALL_STATE_IMPACT);
        ESP_LOGD(TAG, "Impact detected, SVM=%.2fg", sample->svm);
    }
}

/**
 * @brief 处理 FREEFALL 状态
 */
static void process_state_freefall(const accel_sample_t *sample)
{
    uint32_t elapsed = sample->timestamp - s_ctx.freefall_start;

    // 检测冲击
    if (sample->svm > FALL_IMPACT_THRESHOLD) {
        s_ctx.peak_svm = sample->svm;
        s_ctx.impact_time = sample->timestamp;
        change_state(FALL_STATE_IMPACT);
        ESP_LOGD(TAG, "Impact after freefall, SVM=%.2fg", sample->svm);
        return;
    }

    // 自由落体窗口超时，回到 IDLE
    if (elapsed > FALL_FREEFALL_WINDOW) {
        change_state(FALL_STATE_IDLE);
        ESP_LOGD(TAG, "Freefall timeout, back to IDLE");
    }
}

/**
 * @brief 处理 IMPACT 状态
 */
static void process_state_impact(const accel_sample_t *sample)
{
    uint32_t elapsed = sample->timestamp - s_ctx.impact_time;

    // 更新峰值
    if (sample->svm > s_ctx.peak_svm) {
        s_ctx.peak_svm = sample->svm;
    }

    // 冲击窗口结束，进入后续监测
    if (elapsed > FALL_IMPACT_WINDOW) {
        s_ctx.static_start = 0;
        change_state(FALL_STATE_POST_IMPACT);
        ESP_LOGD(TAG, "Impact window end, peak=%.2fg", s_ctx.peak_svm);
    }
}

/**
 * @brief 处理 POST_IMPACT 状态（冲击后监测）
 */
static void process_state_post_impact(const accel_sample_t *sample)
{
    uint32_t elapsed = sample->timestamp - s_ctx.state_enter_time;

    // 检查是否处于静止状态
    bool is_static = (sample->svm >= FALL_STATIC_LOW) &&
                     (sample->svm <= FALL_STATIC_HIGH);

    if (is_static) {
        // 开始或继续静止计时
        if (s_ctx.static_start == 0) {
            s_ctx.static_start = sample->timestamp;
        }

        uint32_t static_duration = sample->timestamp - s_ctx.static_start;

        // 静止时间足够长，检查姿态变化
        if (static_duration >= FALL_STATIC_DURATION) {
            // 获取当前姿态
            float current_orientation[3];
            normalize_orientation(sample->ax, sample->ay, sample->az,
                                  current_orientation);

            // 计算姿态变化
            float dot = orientation_dot_product(s_ctx.pre_fall_orientation,
                                                current_orientation);

            // 姿态变化显著（点积小于阈值表示角度变化大）
            if (dot < FALL_ORIENTATION_THRESHOLD) {
                // 保存跌倒后姿态
                memcpy(s_ctx.last_event.post_orientation,
                       current_orientation, sizeof(current_orientation));
                memcpy(s_ctx.last_event.pre_orientation,
                       s_ctx.pre_fall_orientation,
                       sizeof(s_ctx.pre_fall_orientation));

                // 确认跌倒
                change_state(FALL_STATE_FALL_DETECTED);
                publish_fall_event();
                return;
            }
        }
    } else {
        // 不再静止，重置静止计时
        s_ctx.static_start = 0;
    }

    // 监测窗口超时，判定为误报
    if (elapsed > FALL_POST_IMPACT_WINDOW) {
        ESP_LOGD(TAG, "Post-impact timeout, false alarm");
        change_state(FALL_STATE_IDLE);
    }
}

// ============================================================================
// 事件总线回调
// ============================================================================

/**
 * @brief IMU 数据事件回调 - 自动接收高频 IMU 数据进行跌倒检测
 */
static void on_imu_data(const event_t *event, void *user_data)
{
    (void)user_data;
    if (!s_ctx.running || event == NULL) return;

    const imu_data_t *data = &event->data.imu;
    fall_detect_process(data->accel_x, data->accel_y, data->accel_z);
}

// ============================================================================
// 公共 API 实现
// ============================================================================

/**
 * @brief 初始化跌倒检测服务
 */
esp_err_t fall_detect_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = FALL_STATE_IDLE;
    s_ctx.initialized = true;

    ESP_LOGI(TAG, "Fall detect initialized");
    return ESP_OK;
}

/**
 * @brief 启动跌倒检测
 */
esp_err_t fall_detect_start(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.running = true;
    s_ctx.state = FALL_STATE_IDLE;
    s_ctx.history_head = 0;
    s_ctx.history_count = 0;

    // 订阅高频 IMU 数据事件
    event_subscribe(EVT_IMU_DATA, on_imu_data, NULL);

    ESP_LOGI(TAG, "Fall detect started");
    return ESP_OK;
}

/**
 * @brief 停止跌倒检测
 */
esp_err_t fall_detect_stop(void)
{
    event_unsubscribe(EVT_IMU_DATA, on_imu_data);
    s_ctx.running = false;
    ESP_LOGI(TAG, "Fall detect stopped");
    return ESP_OK;
}

/**
 * @brief 处理加速度数据
 */
bool fall_detect_process(int16_t ax, int16_t ay, int16_t az)
{
    if (!s_ctx.running) {
        return false;
    }

    // 转换为 g 单位
    accel_sample_t sample;
    sample.ax = raw_to_g(ax);
    sample.ay = raw_to_g(ay);
    sample.az = raw_to_g(az);
    sample.svm = calculate_svm(sample.ax, sample.ay, sample.az);
    sample.timestamp = get_timestamp_ms();

    // 添加到历史缓冲区
    add_to_history(&sample);

    // 状态机处理
    switch (s_ctx.state) {
        case FALL_STATE_IDLE:
            process_state_idle(&sample);
            break;

        case FALL_STATE_FREEFALL:
            process_state_freefall(&sample);
            break;

        case FALL_STATE_IMPACT:
            process_state_impact(&sample);
            break;

        case FALL_STATE_POST_IMPACT:
            process_state_post_impact(&sample);
            break;

        case FALL_STATE_FALL_DETECTED:
            // 冷却期结束后自动重置，允许检测后续跌倒
            if ((sample.timestamp - s_ctx.state_enter_time) >= FALL_COOLDOWN_DURATION) {
                ESP_LOGI(TAG, "Cooldown ended, reset to IDLE");
                s_ctx.state = FALL_STATE_IDLE;
                s_ctx.peak_svm = 0;
                s_ctx.static_start = 0;
            }
            break;

        default:
            s_ctx.state = FALL_STATE_IDLE;
            break;
    }

    return (s_ctx.state == FALL_STATE_FALL_DETECTED);
}

/**
 * @brief 获取当前跌倒检测状态
 */
fall_state_t fall_detect_get_state(void)
{
    return s_ctx.state;
}

/**
 * @brief 重置跌倒检测状态
 */
void fall_detect_reset(void)
{
    if (s_ctx.state == FALL_STATE_FALL_DETECTED) {
        s_ctx.stats.confirmed_falls++;
    }
    s_ctx.state = FALL_STATE_IDLE;
    s_ctx.peak_svm = 0;
    s_ctx.static_start = 0;
    ESP_LOGI(TAG, "Fall detect reset");
}

/**
 * @brief 获取最近一次跌倒事件数据
 */
esp_err_t fall_detect_get_event(fall_event_data_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.has_event) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(event, &s_ctx.last_event, sizeof(fall_event_data_t));
    return ESP_OK;
}

/**
 * @brief 获取统计信息
 */
esp_err_t fall_detect_get_stats(fall_detect_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_ctx.stats, sizeof(fall_detect_stats_t));
    return ESP_OK;
}
