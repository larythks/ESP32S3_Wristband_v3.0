/**
 * @file health_monitor.c
 * @brief 健康监测服务实现
 */

#include "health_monitor.h"
#include "sensor_service.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "health_monitor";

// ============== 内部常量 ==============

#define PPG_BUFFER_SIZE         100     // PPG 数据缓冲区大小
#define TEMP_FILTER_SIZE        5       // 环境温度滑动平均窗口
#define HR_CALC_INTERVAL_MS     1000    // 心率计算间隔

// 峰值检测参数
#define PEAK_MIN_INTERVAL_MS    300     // 最小峰值间隔 (对应 200bpm)
#define PEAK_MAX_INTERVAL_MS    2000    // 最大峰值间隔 (对应 30bpm)
#define PEAK_DETECTION_THRESHOLD 50     // 峰值检测最小变化量

// ============== 内部数据结构 ==============

/**
 * @brief 心率血氧计算上下文
 */
typedef struct {
    uint32_t red_buffer[PPG_BUFFER_SIZE];
    uint32_t ir_buffer[PPG_BUFFER_SIZE];
    uint16_t buffer_index;
    uint16_t buffer_count;

    // 峰值检测
    uint32_t last_peak_time;
    uint32_t peak_intervals[8];
    uint8_t interval_index;
    uint8_t interval_count;

    // 峰值检测状态
    uint32_t prev_ir_value;         // 前一个 IR 值
    bool rising;                    // 信号上升中
    uint32_t local_max_value;       // 局部最大值
    uint32_t local_max_time;        // 局部最大值时间

    // AC/DC 分量
    uint32_t red_ac;
    uint32_t red_dc;
    uint32_t ir_ac;
    uint32_t ir_dc;
} ppg_context_t;

/**
 * @brief 环境温度监测上下文
 */
typedef struct {
    float filter_buffer[TEMP_FILTER_SIZE];
    uint8_t filter_index;
    uint8_t filter_count;
    float last_valid_temp;

    // 连续越阈计数
    uint8_t high_count;
    uint8_t low_count;
} temp_context_t;

/**
 * @brief 告警判定上下文
 */
typedef struct {
    uint8_t hr_alert_count;     // 心率连续异常计数
    uint8_t spo2_alert_count;   // 血氧连续异常计数
    bool hr_in_alert;           // 心率处于告警状态
    bool spo2_in_alert;         // 血氧处于告警状态
} alert_context_t;

/**
 * @brief 健康监测服务上下文
 */
typedef struct {
    bool initialized;
    bool running;

    ppg_context_t ppg;
    temp_context_t temp;
    alert_context_t alert;

    // 测量窗口跟踪
    bool measuring_active;          // 当前是否在测量窗口内
    uint32_t measure_start_time;    // 窗口开始时间
    bool ppg_result_fresh;          // 本次事件是否有新的 HR/SpO2 测量结果

    health_status_t status;
} health_monitor_ctx_t;

static health_monitor_ctx_t s_ctx = {0};

// ============== 前向声明 ==============

static void on_sensor_data(const event_t *event, void *user_data);
static void process_ppg_data(uint32_t red, uint32_t ir, uint32_t timestamp);
static void detect_peak(uint32_t ir_value, uint32_t timestamp);
static void process_temp_data(float temp, uint32_t timestamp);
static void check_alerts(uint32_t timestamp);
static void calculate_ac_dc(void);
static uint8_t calculate_heart_rate(void);
static uint8_t calculate_spo2(void);
static void publish_health_alert(alert_type_t type, alert_level_t level, int16_t value, uint32_t timestamp);

// ============== API 实现 ==============

esp_err_t health_monitor_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.temp.last_valid_temp = 25.0f;  // 默认环境温度

    ESP_LOGI(TAG, "Health monitor initialized");
    s_ctx.initialized = true;
    return ESP_OK;
}

esp_err_t health_monitor_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    // 订阅传感器数据事件
    esp_err_t ret = event_subscribe(EVT_SENSOR_DATA, on_sensor_data, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe sensor data event");
        return ret;
    }

    s_ctx.running = true;
    ESP_LOGI(TAG, "Health monitor started");
    return ESP_OK;
}

esp_err_t health_monitor_stop(void)
{
    if (!s_ctx.running) {
        return ESP_OK;
    }

    event_unsubscribe(EVT_SENSOR_DATA, on_sensor_data);
    s_ctx.running = false;
    ESP_LOGI(TAG, "Health monitor stopped");
    return ESP_OK;
}

health_status_t health_get_status(void)
{
    return s_ctx.status;
}

void health_reset_alert(void)
{
    s_ctx.alert.hr_in_alert = false;
    s_ctx.alert.spo2_in_alert = false;
    s_ctx.alert.hr_alert_count = 0;
    s_ctx.alert.spo2_alert_count = 0;
    s_ctx.status.alert_level = ALERT_LEVEL_NONE;
    s_ctx.status.alert_type = ALERT_TYPE_NONE;
    ESP_LOGI(TAG, "Alert state reset");
}

// ============== 事件处理 ==============

static void on_sensor_data(const event_t *event, void *user_data)
{
    (void)user_data;

    if (!s_ctx.running || event == NULL) {
        return;
    }

    const sensor_data_t *data = &event->data.sensor;
    uint32_t timestamp = data->timestamp;

    // 处理环境温度数据
    if (data->temperature > 0) {
        process_temp_data(data->temperature, timestamp);
    }

    // 查询 sensor_service 的心率测量状态
    hr_measure_state_t hr_state = sensor_get_hr_measure_state();

    if (hr_state == HR_MEASURE_MEASURING) {
        // 测量窗口激活
        if (!s_ctx.measuring_active) {
            // 窗口刚开始，重置PPG缓冲区和峰值检测
            ESP_LOGI(TAG, "HR measure window started, resetting PPG context");
            memset(&s_ctx.ppg, 0, sizeof(ppg_context_t));
            s_ctx.measuring_active = true;
            s_ctx.measure_start_time = timestamp;
        }

        // 窗口内：处理 PPG 数据
        if ((data->data_valid & SENSOR_HR_SPO2) && data->ppg_fresh) {
            process_ppg_data(data->ppg_red, data->ppg_ir, timestamp);
        }
    } else {
        // 不在测量中
        if (s_ctx.measuring_active) {
            // 窗口刚结束，执行最终计算
            ESP_LOGI(TAG, "HR measure window ended, finalizing results");
            s_ctx.measuring_active = false;

            // 一次性计算 AC/DC 分量
            calculate_ac_dc();

            // 最终计算心率和血氧
            uint8_t hr = calculate_heart_rate();
            uint8_t spo2 = calculate_spo2();

            if (hr > 0) {
                s_ctx.status.heart_rate = hr;
                s_ctx.status.hr_validity = MEASURE_VALID;
                ESP_LOGI(TAG, "Window HR result: %d bpm", hr);
            } else {
                s_ctx.status.hr_validity = MEASURE_INVALID_NO_SIGNAL;
                ESP_LOGW(TAG, "Window HR: no valid result");
            }

            if (spo2 > 0) {
                s_ctx.status.spo2 = spo2;
                s_ctx.status.spo2_validity = MEASURE_VALID;
                ESP_LOGI(TAG, "Window SpO2 result: %d%%", spo2);
            } else {
                s_ctx.status.spo2_validity = MEASURE_INVALID_NO_SIGNAL;
                ESP_LOGW(TAG, "Window SpO2: no valid result");
            }
            s_ctx.ppg_result_fresh = true;  // 标记有新的测量结果
        }
    }

    // 更新状态时间戳
    s_ctx.status.timestamp = timestamp;

    // 检查告警
    check_alerts(timestamp);
}

// ============== 环境温度处理 ==============

static void process_temp_data(float temp, uint32_t timestamp)
{
    // 检查温度跳变
    float diff = fabsf(temp - s_ctx.temp.last_valid_temp);
    if (diff > TEMP_MAX_JUMP && s_ctx.temp.filter_count > 0) {
        ESP_LOGW(TAG, "Temp jump too large: %.1f -> %.1f",
                 s_ctx.temp.last_valid_temp, temp);
        s_ctx.status.temp_validity = MEASURE_INVALID_UNSTABLE;
        return;
    }

    // 滑动平均滤波
    s_ctx.temp.filter_buffer[s_ctx.temp.filter_index] = temp;
    s_ctx.temp.filter_index = (s_ctx.temp.filter_index + 1) % TEMP_FILTER_SIZE;
    if (s_ctx.temp.filter_count < TEMP_FILTER_SIZE) {
        s_ctx.temp.filter_count++;
    }

    // 计算平均值
    float sum = 0;
    for (int i = 0; i < s_ctx.temp.filter_count; i++) {
        sum += s_ctx.temp.filter_buffer[i];
    }
    float avg_temp = sum / s_ctx.temp.filter_count;

    s_ctx.temp.last_valid_temp = avg_temp;
    s_ctx.status.temperature = avg_temp;
    s_ctx.status.temp_validity = MEASURE_VALID;

    // 阈值检测
    if (avg_temp >= TEMP_ALARM_HIGH) {
        s_ctx.temp.high_count++;
        s_ctx.temp.low_count = 0;
    } else if (avg_temp <= TEMP_ALARM_LOW) {
        s_ctx.temp.low_count++;
        s_ctx.temp.high_count = 0;
    } else {
        s_ctx.temp.high_count = 0;
        s_ctx.temp.low_count = 0;
    }
}

// ============== PPG 数据处理 ==============

/**
 * @brief 峰值检测 - 检测 IR 信号的峰值用于心率计算
 * @param ir_value 当前 IR 值
 * @param timestamp 当前时间戳
 */
static void detect_peak(uint32_t ir_value, uint32_t timestamp)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    // 首次调用初始化
    if (ppg->prev_ir_value == 0) {
        ppg->prev_ir_value = ir_value;
        ppg->rising = true;
        return;
    }

    // 判断信号趋势
    if (ir_value > ppg->prev_ir_value + PEAK_DETECTION_THRESHOLD) {
        // 信号上升
        ppg->rising = true;
        if (ir_value > ppg->local_max_value) {
            ppg->local_max_value = ir_value;
            ppg->local_max_time = timestamp;
        }
    } else if (ir_value < ppg->prev_ir_value - PEAK_DETECTION_THRESHOLD) {
        // 信号下降，检查是否刚从上升转为下降（峰值）
        if (ppg->rising && ppg->local_max_value > 0) {
            // 检测到峰值
            uint32_t interval = ppg->local_max_time - ppg->last_peak_time;

            // 验证峰值间隔是否在合理范围内
            if (ppg->last_peak_time > 0 &&
                interval >= PEAK_MIN_INTERVAL_MS &&
                interval <= PEAK_MAX_INTERVAL_MS) {
                // 记录有效峰值间隔
                ppg->peak_intervals[ppg->interval_index] = interval;
                ppg->interval_index = (ppg->interval_index + 1) % 8;
                if (ppg->interval_count < 8) {
                    ppg->interval_count++;
                }
            }

            ppg->last_peak_time = ppg->local_max_time;
            ppg->local_max_value = 0;
        }
        ppg->rising = false;
    }

    ppg->prev_ir_value = ir_value;
}

static void process_ppg_data(uint32_t red, uint32_t ir, uint32_t timestamp)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    // 存入缓冲区
    ppg->red_buffer[ppg->buffer_index] = red;
    ppg->ir_buffer[ppg->buffer_index] = ir;
    ppg->buffer_index = (ppg->buffer_index + 1) % PPG_BUFFER_SIZE;
    if (ppg->buffer_count < PPG_BUFFER_SIZE) {
        ppg->buffer_count++;
    }

    // 峰值检测（用于心率计算）
    detect_peak(ir, timestamp);
}

// ============== AC/DC 分量计算 ==============

/**
 * @brief 计算 PPG 缓冲区的 AC/DC 分量（窗口结束时调用一次）
 */
static void calculate_ac_dc(void)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    if (ppg->buffer_count < PPG_BUFFER_SIZE / 2) {
        return;
    }

    uint32_t red_min = UINT32_MAX, red_max = 0;
    uint32_t ir_min = UINT32_MAX, ir_max = 0;
    uint64_t red_sum = 0, ir_sum = 0;

    for (int i = 0; i < ppg->buffer_count; i++) {
        uint32_t r = ppg->red_buffer[i];
        uint32_t ir_val = ppg->ir_buffer[i];

        if (r < red_min) red_min = r;
        if (r > red_max) red_max = r;
        if (ir_val < ir_min) ir_min = ir_val;
        if (ir_val > ir_max) ir_max = ir_val;

        red_sum += r;
        ir_sum += ir_val;
    }

    ppg->red_ac = red_max - red_min;
    ppg->ir_ac = ir_max - ir_min;
    ppg->red_dc = (uint32_t)(red_sum / ppg->buffer_count);
    ppg->ir_dc = (uint32_t)(ir_sum / ppg->buffer_count);
}

// ============== 心率计算 ==============

static uint8_t calculate_heart_rate(void)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    if (ppg->interval_count < 3) {
        return 0;
    }

    // 计算平均间隔
    uint32_t sum = 0;
    for (int i = 0; i < ppg->interval_count; i++) {
        sum += ppg->peak_intervals[i];
    }
    uint32_t avg_interval = sum / ppg->interval_count;

    if (avg_interval == 0) {
        return 0;
    }

    // 心率 = 60000 / 间隔(ms)
    uint32_t hr = 60000 / avg_interval;

    // 范围限制
    if (hr < 30 || hr > 200) {
        return 0;
    }

    return (uint8_t)hr;
}

// ============== 血氧计算 ==============

static uint8_t calculate_spo2(void)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    // 避免除零
    if (ppg->red_dc == 0 || ppg->ir_dc == 0) {
        return 0;
    }

    // R = (AC_red/DC_red) / (AC_ir/DC_ir)
    float r_red = (float)ppg->red_ac / (float)ppg->red_dc;
    float r_ir = (float)ppg->ir_ac / (float)ppg->ir_dc;

    if (r_ir < 0.0001f) {
        return 0;
    }

    float R = r_red / r_ir;

    // SpO2 = 110 - 25 * R (经验公式)
    float spo2 = 110.0f - 25.0f * R;

    // 范围限制
    if (spo2 < 70.0f || spo2 > 100.0f) {
        return 0;
    }

    return (uint8_t)spo2;
}

// ============== 告警检查 ==============

static void check_alerts(uint32_t timestamp)
{
    alert_type_t new_alert = ALERT_TYPE_NONE;
    alert_level_t new_level = ALERT_LEVEL_NONE;
    int16_t alert_value = 0;

    // 1. 环境温度告警检查 (连续3次越阈)
    if (s_ctx.temp.high_count >= TEMP_ALARM_COUNT) {
        new_alert = ALERT_TYPE_TEMP_HIGH;
        new_level = ALERT_LEVEL_ALARM;
        alert_value = (int16_t)(s_ctx.status.temperature * 10);
    } else if (s_ctx.temp.low_count >= TEMP_ALARM_COUNT) {
        new_alert = ALERT_TYPE_TEMP_LOW;
        new_level = ALERT_LEVEL_ALARM;
        alert_value = (int16_t)(s_ctx.status.temperature * 10);
    }

    // 2-3. 心率/血氧告警检查（仅在有新测量结果时递增计数）
    if (s_ctx.ppg_result_fresh) {
        s_ctx.ppg_result_fresh = false;

        // 2. 心率告警检查（连续 2 次异常测量）
        if (s_ctx.status.hr_validity == MEASURE_VALID) {
            uint8_t hr = s_ctx.status.heart_rate;
            bool hr_abnormal = (hr >= HR_ALARM_HIGH || hr <= HR_ALARM_LOW);

            if (hr_abnormal) {
                if (!s_ctx.alert.hr_in_alert) {
                    // 首次异常：进入告警状态，立刻切换到实时模式
                    s_ctx.alert.hr_in_alert = true;
                    s_ctx.alert.hr_alert_count = 1;
                    sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_REALTIME);
                    ESP_LOGW(TAG, "HR abnormal (%d bpm), enter realtime mode", hr);
                } else {
                    // 后续异常：递增计数
                    s_ctx.alert.hr_alert_count++;
                    if (s_ctx.alert.hr_alert_count >= HR_ALARM_COUNT) {
                        // 连续 2 次异常 → 触发报警
                        if (new_level < ALERT_LEVEL_ALARM) {
                            new_alert = (hr >= HR_ALARM_HIGH) ? ALERT_TYPE_HR_HIGH : ALERT_TYPE_HR_LOW;
                            new_level = ALERT_LEVEL_ALARM;
                            alert_value = hr * 10;
                        }
                    }
                }
            } else {
                // 恢复正常：清除告警状态
                s_ctx.alert.hr_in_alert = false;
                s_ctx.alert.hr_alert_count = 0;
            }
        }

        // 3. 血氧告警检查（连续 2 次异常测量）
        if (s_ctx.status.spo2_validity == MEASURE_VALID) {
            uint8_t spo2 = s_ctx.status.spo2;

            if (spo2 <= SPO2_ALARM_LOW) {
                if (!s_ctx.alert.spo2_in_alert) {
                    // 首次异常：进入告警状态，立刻切换到实时模式
                    s_ctx.alert.spo2_in_alert = true;
                    s_ctx.alert.spo2_alert_count = 1;
                    sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_REALTIME);
                    ESP_LOGW(TAG, "SpO2 abnormal (%d%%), enter realtime mode", spo2);
                } else {
                    // 后续异常：递增计数
                    s_ctx.alert.spo2_alert_count++;
                    if (s_ctx.alert.spo2_alert_count >= SPO2_ALARM_COUNT) {
                        // 连续 2 次异常 → 触发报警
                        if (new_level < ALERT_LEVEL_ALARM) {
                            new_alert = ALERT_TYPE_SPO2_LOW;
                            new_level = ALERT_LEVEL_ALARM;
                            alert_value = spo2 * 10;
                        }
                    }
                }
            } else {
                // 恢复正常：清除告警状态
                s_ctx.alert.spo2_in_alert = false;
                s_ctx.alert.spo2_alert_count = 0;
            }
        }

        // 检查是否应退出 HR/SpO2 实时模式
        // 心率和血氧共享 MAX30102，只有两者都恢复正常才退出实时模式
        if (!s_ctx.alert.hr_in_alert && !s_ctx.alert.spo2_in_alert) {
            if (sensor_get_mode(SENSOR_HR_SPO2) == SAMPLING_MODE_REALTIME) {
                sensor_set_mode(SENSOR_HR_SPO2, SAMPLING_MODE_NORMAL);
                ESP_LOGI(TAG, "HR/SpO2 normalized, exit realtime mode");
            }
        }
    }

    // 更新状态
    s_ctx.status.alert_type = new_alert;
    s_ctx.status.alert_level = new_level;
    s_ctx.status.alert_value = alert_value;

    // 发布告警事件
    if (new_level >= ALERT_LEVEL_ALARM) {
        publish_health_alert(new_alert, new_level, alert_value, timestamp);
    }
}

// ============== 告警发布 ==============

static void publish_health_alert(alert_type_t type, alert_level_t level, int16_t value, uint32_t timestamp)
{
    ESP_LOGW(TAG, "Health alert: type=%d, level=%d, value=%d", type, level, value);

    // 触发对应传感器进入实时检测模式
    uint8_t sensor_mask = 0;
    if (type == ALERT_TYPE_TEMP_HIGH || type == ALERT_TYPE_TEMP_LOW) {
        sensor_mask = SENSOR_TEMP;
    } else if (type == ALERT_TYPE_HR_HIGH || type == ALERT_TYPE_HR_LOW ||
               type == ALERT_TYPE_SPO2_LOW) {
        sensor_mask = SENSOR_HR_SPO2;
    }

    if (sensor_mask != 0) {
        sensor_set_mode(sensor_mask, SAMPLING_MODE_REALTIME);
    }

    // 发布健康告警事件
    event_data_t evt_data;
    evt_data.health_alert.type = type;
    evt_data.health_alert.level = level;
    evt_data.health_alert.value = value;
    evt_data.health_alert.timestamp = timestamp;
    event_publish(EVT_HEALTH_ALERT, &evt_data);
}
