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

#define TEMP_FILTER_SIZE        5       // 环境温度滑动平均窗口

// PPG 有效采样率 (MAX30102 100Hz / 4倍FIFO平均 = 25Hz)
#define PPG_SAMPLE_RATE_HZ      25
#define PPG_SAMPLE_PERIOD_MS    40      // 1000 / 25

// 峰值间隔范围 (以采样点数计)
#define PEAK_MIN_INTERVAL       8       // 320ms → ~187bpm
#define PEAK_MAX_INTERVAL       50      // 2000ms → 30bpm
#define PEAK_INTERVALS_MAX      16      // 峰值间隔缓冲区大小

// IIR 滤波器参数 (Fs = 25Hz)
#define FILTER_HP_ALPHA         0.888f  // 高通截止 ~0.5Hz
#define FILTER_LP_ALPHA         0.501f  // 低通截止 ~4Hz

// 自适应阈值参数
#define ADAPTIVE_WINDOW         50      // 自适应窗口 (~2秒 @25Hz)
#define ADAPTIVE_THRESH_RATIO   0.3f    // 阈值 = 峰峰值 × 比例
#define ADAPTIVE_THRESH_MIN     10.0f   // 最小阈值
#define FILTER_SETTLE_SAMPLES   25      // 滤波器建立期 (1秒 @25Hz)

// 运动抑制参数 (MPU6050 ±2g, 1g = 16384 LSB)
#define MOTION_GRAVITY_REF      16384
#define MOTION_THRESHOLD        2000    // ~0.12g 偏差视为运动
#define MOTION_REJECT_PERCENT   50      // 超过50%样本被运动污染则无效

// ============== 内部数据结构 ==============

/**
 * @brief 心率血氧计算上下文
 */
typedef struct {
    // IIR 滤波器状态 (IR 通道 - 用于峰值检测)
    float hp_prev_in;           // 高通前一输入
    float hp_prev_out;          // 高通前一输出
    float lp_prev_out;          // 低通前一输出
    bool filter_initialized;

    // IIR 滤波器状态 (RED 通道 - 用于 AC 计算)
    float red_hp_prev_in;
    float red_hp_prev_out;
    float red_lp_prev_out;

    // 采样计数 (用于替代时间戳)
    uint32_t sample_count;

    // 自适应阈值：滑动窗口跟踪滤波后信号
    float adapt_buffer[ADAPTIVE_WINDOW];
    uint16_t adapt_index;
    uint16_t adapt_count;

    // 峰值检测状态 (使用滤波后信号)
    bool rising;                // 当前处于上升阶段
    float local_max_value;      // 局部最大值
    uint32_t local_max_sample;  // 局部最大值对应的采样序号
    float local_min_value;      // 局部最小值

    // 峰值间隔 (以采样点数为单位)
    uint32_t last_peak_sample;  // 上次确认峰值的采样序号
    uint32_t peak_intervals[PEAK_INTERVALS_MAX];
    uint8_t interval_index;
    uint8_t interval_count;

    // AC/DC 分量 (最终计算结果, 使用 float 避免精度丢失)
    float red_ac;
    float red_dc;
    float ir_ac;
    float ir_dc;

    // 增量式 AC/DC 累加器 (在 process_ppg_data 中逐样本累加)
    double red_filt_sq_sum;     // RED 滤波后信号的平方和 (用于 AC)
    double ir_filt_sq_sum;      // IR 滤波后信号的平方和 (用于 AC)
    uint64_t red_raw_sum;       // RED 原始值累加 (用于 DC)
    uint64_t ir_raw_sum;        // IR 原始值累加 (用于 DC)
    uint32_t valid_sample_count; // 非运动样本计数 (排除建立期)
    uint32_t filter_settle_count; // 总采样计数 (含建立期, 用于判断滤波器是否稳定)

    // 运动抑制统计
    uint32_t motion_reject_count;
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
    bool motion_active;             // 当前 IMU 是否检测到运动

    health_status_t status;
} health_monitor_ctx_t;

static health_monitor_ctx_t s_ctx = {0};

// ============== 前向声明 ==============

static void on_sensor_data(const event_t *event, void *user_data);
static void process_ppg_data(uint32_t red, uint32_t ir, bool motion_active);
static void detect_peak(float filtered_value);
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

    // ---- 运动检测 (利用 IMU 加速度) ----
    if (data->data_valid & SENSOR_IMU) {
        int32_t ax = data->accel_x;
        int32_t ay = data->accel_y;
        int32_t az = data->accel_z;
        uint32_t mag_sq = (uint32_t)(ax * ax + ay * ay + az * az);
        uint32_t gravity_sq = (uint32_t)MOTION_GRAVITY_REF * MOTION_GRAVITY_REF;
        int32_t diff = (int32_t)mag_sq - (int32_t)gravity_sq;
        int32_t thresh_sq = 2 * MOTION_GRAVITY_REF * MOTION_THRESHOLD;
        s_ctx.motion_active = (diff > thresh_sq || diff < -thresh_sq);
    }

    // ---- 心率测量窗口处理 ----
    hr_measure_state_t hr_state = sensor_get_hr_measure_state();

    if (hr_state == HR_MEASURE_MEASURING) {
        // 测量窗口激活
        if (!s_ctx.measuring_active) {
            ESP_LOGI(TAG, "HR measure window started, resetting PPG context");
            memset(&s_ctx.ppg, 0, sizeof(ppg_context_t));
            s_ctx.measuring_active = true;
            s_ctx.measure_start_time = timestamp;
        }

        // 从环形缓冲区批量取出所有 PPG 样本
        ppg_batch_t batch;
        if (sensor_drain_ppg(&batch) == ESP_OK && batch.count > 0) {
            for (uint8_t i = 0; i < batch.count; i++) {
                process_ppg_data(batch.red[i], batch.ir[i], s_ctx.motion_active);
            }
        }
    } else {
        // 不在测量中
        if (s_ctx.measuring_active) {
            ESP_LOGI(TAG, "HR measure window ended, finalizing results");
            s_ctx.measuring_active = false;

            // 取出剩余 PPG 样本
            ppg_batch_t batch;
            if (sensor_drain_ppg(&batch) == ESP_OK && batch.count > 0) {
                for (uint8_t i = 0; i < batch.count; i++) {
                    process_ppg_data(batch.red[i], batch.ir[i], s_ctx.motion_active);
                }
            }

            // 检查运动污染比例
            uint32_t total = s_ctx.ppg.sample_count;
            uint32_t rejected = s_ctx.ppg.motion_reject_count;
            bool too_much_motion = (total > 0 &&
                                    rejected * 100 / total > MOTION_REJECT_PERCENT);

            if (too_much_motion) {
                s_ctx.status.hr_validity = MEASURE_INVALID_MOTION;
                s_ctx.status.spo2_validity = MEASURE_INVALID_MOTION;
                ESP_LOGW(TAG, "Too much motion: %u/%u samples rejected",
                         (unsigned)rejected, (unsigned)total);
            } else {
                // 计算 AC/DC 分量
                calculate_ac_dc();

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
            }
            s_ctx.ppg_result_fresh = true;
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
 * @brief 自适应峰值检测 (使用滤波后信号)
 *
 * 算法：跟踪局部最大/最小值，当信号从最大值回落超过自适应阈值时
 * 确认峰值；当信号从最小值回升超过阈值时确认谷值。
 */
static void detect_peak(float filtered_value)
{
    ppg_context_t *ppg = &s_ctx.ppg;
    ppg->sample_count++;

    // 更新自适应窗口缓冲区
    ppg->adapt_buffer[ppg->adapt_index] = filtered_value;
    ppg->adapt_index = (ppg->adapt_index + 1) % ADAPTIVE_WINDOW;
    if (ppg->adapt_count < ADAPTIVE_WINDOW) {
        ppg->adapt_count++;
    }

    // 计算自适应阈值：最近 2 秒信号的峰峰值 × 比例
    float threshold = ADAPTIVE_THRESH_MIN;
    if (ppg->adapt_count >= ADAPTIVE_WINDOW / 2) {
        float win_min = ppg->adapt_buffer[0];
        float win_max = ppg->adapt_buffer[0];
        for (uint16_t i = 1; i < ppg->adapt_count; i++) {
            if (ppg->adapt_buffer[i] > win_max) win_max = ppg->adapt_buffer[i];
            if (ppg->adapt_buffer[i] < win_min) win_min = ppg->adapt_buffer[i];
        }
        float pp = win_max - win_min;
        float adaptive_th = pp * ADAPTIVE_THRESH_RATIO;
        if (adaptive_th > threshold) {
            threshold = adaptive_th;
        }
    }

    // 首个样本初始化
    if (ppg->sample_count == 1) {
        ppg->local_max_value = filtered_value;
        ppg->local_min_value = filtered_value;
        ppg->local_max_sample = ppg->sample_count;
        ppg->rising = true;
        return;
    }

    if (ppg->rising) {
        // 上升阶段：跟踪局部最大值
        if (filtered_value > ppg->local_max_value) {
            ppg->local_max_value = filtered_value;
            ppg->local_max_sample = ppg->sample_count;
        }
        // 从局部最大值回落超过阈值 → 确认峰值
        if (ppg->local_max_value - filtered_value >= threshold) {
            uint32_t interval = ppg->local_max_sample - ppg->last_peak_sample;

            if (ppg->last_peak_sample > 0 &&
                interval >= PEAK_MIN_INTERVAL &&
                interval <= PEAK_MAX_INTERVAL) {
                ppg->peak_intervals[ppg->interval_index] = interval;
                ppg->interval_index = (ppg->interval_index + 1) % PEAK_INTERVALS_MAX;
                if (ppg->interval_count < PEAK_INTERVALS_MAX) {
                    ppg->interval_count++;
                }
            }

            ppg->last_peak_sample = ppg->local_max_sample;
            ppg->rising = false;
            ppg->local_min_value = filtered_value;
        }
    } else {
        // 下降阶段：跟踪局部最小值
        if (filtered_value < ppg->local_min_value) {
            ppg->local_min_value = filtered_value;
        }
        // 从局部最小值回升超过阈值 → 确认谷值，开始新的上升
        if (filtered_value - ppg->local_min_value >= threshold) {
            ppg->rising = true;
            ppg->local_max_value = filtered_value;
            ppg->local_max_sample = ppg->sample_count;
        }
    }
}

/**
 * @brief 处理单个 PPG 样本：带通滤波(IR+RED) → 增量 AC/DC 累加 → 峰值检测
 *
 * 改进：对 RED 和 IR 两个通道都执行 IIR 带通滤波，
 * 滤波后信号的平方和用于 AC 计算（代替原始缓冲区遍历），
 * 运动样本不参与 AC/DC 累加。
 */
static void process_ppg_data(uint32_t red, uint32_t ir, bool motion_active)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    // 对 IR 信号应用带通滤波
    float ir_f = (float)ir;
    float red_f = (float)red;
    float ir_filtered, red_filtered;

    if (!ppg->filter_initialized) {
        // IR 通道初始化
        ppg->hp_prev_in = ir_f;
        ppg->hp_prev_out = 0.0f;
        ppg->lp_prev_out = 0.0f;
        // RED 通道初始化
        ppg->red_hp_prev_in = red_f;
        ppg->red_hp_prev_out = 0.0f;
        ppg->red_lp_prev_out = 0.0f;
        ppg->filter_initialized = true;
        ir_filtered = 0.0f;
        red_filtered = 0.0f;
    } else {
        // IR 通道：高通 → 低通
        float ir_hp = FILTER_HP_ALPHA * (ppg->hp_prev_out + ir_f - ppg->hp_prev_in);
        ppg->hp_prev_in = ir_f;
        ppg->hp_prev_out = ir_hp;
        ir_filtered = FILTER_LP_ALPHA * ir_hp + (1.0f - FILTER_LP_ALPHA) * ppg->lp_prev_out;
        ppg->lp_prev_out = ir_filtered;

        // RED 通道：高通 → 低通
        float red_hp = FILTER_HP_ALPHA * (ppg->red_hp_prev_out + red_f - ppg->red_hp_prev_in);
        ppg->red_hp_prev_in = red_f;
        ppg->red_hp_prev_out = red_hp;
        red_filtered = FILTER_LP_ALPHA * red_hp + (1.0f - FILTER_LP_ALPHA) * ppg->red_lp_prev_out;
        ppg->red_lp_prev_out = red_filtered;
    }

    // 跟踪总采样数 (用于判断滤波器建立期, 在运动检查之前)
    ppg->filter_settle_count++;

    // 运动期间：维护滤波器状态但跳过峰值检测和 AC/DC 累加
    if (motion_active) {
        ppg->motion_reject_count++;
        return;
    }

    // 滤波器建立期后才累加 AC/DC (前 1 秒输出不稳定)
    if (ppg->filter_settle_count > FILTER_SETTLE_SAMPLES) {
        // 累加 AC 分量：滤波后信号的平方和
        ppg->red_filt_sq_sum += (double)red_filtered * red_filtered;
        ppg->ir_filt_sq_sum  += (double)ir_filtered * ir_filtered;

        // 累加 DC 分量：原始值求和
        ppg->red_raw_sum += red;
        ppg->ir_raw_sum  += ir;
        ppg->valid_sample_count++;
    }

    // 对 IR 滤波后信号执行自适应峰值检测
    detect_peak(ir_filtered);
}

// ============== AC/DC 分量计算 ==============

/**
 * @brief 计算 PPG 的 AC/DC 分量（使用增量累加值）
 *
 * DC = 原始值均值 (raw_sum / valid_count)
 * AC = RMS(滤波信号) × 2√2
 *
 * 使用滤波后信号计算 AC 可以排除基线漂移和高频噪声的影响，
 * 运动样本已在 process_ppg_data 中被排除，不参与累加。
 */
static void calculate_ac_dc(void)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    if (ppg->valid_sample_count < 50) {
        ESP_LOGW(TAG, "Too few valid PPG samples: %u", (unsigned)ppg->valid_sample_count);
        return;
    }

    uint32_t n = ppg->valid_sample_count;

    // DC = 原始值均值 (使用 float 避免精度丢失)
    ppg->red_dc = (float)ppg->red_raw_sum / (float)n;
    ppg->ir_dc  = (float)ppg->ir_raw_sum / (float)n;

    // AC = RMS(滤波信号) × 2√2 (使用 float 保留完整精度)
    float red_rms = sqrtf((float)(ppg->red_filt_sq_sum / n));
    float ir_rms  = sqrtf((float)(ppg->ir_filt_sq_sum / n));

    ppg->red_ac = red_rms * 2.828f;
    ppg->ir_ac  = ir_rms * 2.828f;

}

// ============== 心率计算 ==============

/**
 * @brief 心率计算：中值离群值剔除 + 平均
 *
 * 1. 对所有峰值间隔排序，取中值
 * 2. 剔除偏离中值 ±30% 的异常间隔
 * 3. 对剩余有效间隔取平均，转换为 BPM
 */
static uint8_t calculate_heart_rate(void)
{
    ppg_context_t *ppg = &s_ctx.ppg;

    if (ppg->interval_count < 3) {
        return 0;
    }

    // 提取间隔到临时数组
    uint32_t intervals[PEAK_INTERVALS_MAX];
    uint8_t n = ppg->interval_count;
    for (uint8_t i = 0; i < n; i++) {
        intervals[i] = ppg->peak_intervals[i];
    }

    // 冒泡排序 (数组小，开销可忽略)
    for (uint8_t i = 0; i < n - 1; i++) {
        for (uint8_t j = 0; j < n - 1 - i; j++) {
            if (intervals[j] > intervals[j + 1]) {
                uint32_t tmp = intervals[j];
                intervals[j] = intervals[j + 1];
                intervals[j + 1] = tmp;
            }
        }
    }

    // 中值
    uint32_t median = intervals[n / 2];
    if (median == 0) {
        return 0;
    }

    // 剔除偏离中值 ±40% 的离群值
    uint32_t low_bound  = median * 60 / 100;
    uint32_t high_bound = median * 140 / 100;

    uint32_t sum = 0;
    uint8_t valid = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (intervals[i] >= low_bound && intervals[i] <= high_bound) {
            sum += intervals[i];
            valid++;
        }
    }

    if (valid < 2) {
        return 0;
    }

    uint32_t avg_interval = sum / valid;
    if (avg_interval == 0) {
        return 0;
    }

    // 采样点数 → BPM: hr = 60 × Fs / avg_interval
    uint32_t hr = 60 * PPG_SAMPLE_RATE_HZ / avg_interval;

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
    if (ppg->red_dc < 1.0f || ppg->ir_dc < 1.0f) {
        return 0;
    }

    // 信号幅度校验：AC 过小说明信号被噪声主导，结果不可信
    if (ppg->ir_ac < (float)PPG_MIN_AMPLITUDE || ppg->red_ac < (float)PPG_MIN_AMPLITUDE) {
        ESP_LOGW(TAG, "SpO2: signal too weak (red_ac=%.1f, ir_ac=%.1f, min=%d)",
                 ppg->red_ac, ppg->ir_ac, PPG_MIN_AMPLITUDE);
        return 0;
    }

    // R = (AC_red/DC_red) / (AC_ir/DC_ir)
    float r_red = ppg->red_ac / ppg->red_dc;
    float r_ir  = ppg->ir_ac / ppg->ir_dc;

    if (r_ir < 0.0001f) {
        return 0;
    }

    float R = r_red / r_ir;

    // Maxim MAX30102 官方二次多项式校准公式 (比线性公式更精确)
    // 原公式 SpO2 = 110 - 25*R 在 R=0.5~0.8 范围系统性偏低 3~5%
    float spo2 = -45.060f * R * R + 30.354f * R + 94.845f;

    // 范围限制
    if (spo2 < 70.0f || spo2 > 100.0f) {
        return 0;
    }

    return (uint8_t)(spo2 + 0.5f);  // 四舍五入
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
