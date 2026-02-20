/**
 * @file sensor_service.c
 * @brief 传感器采样服务实现
 */

#include "sensor_service.h"
#include "event_bus.h"
#include "pedometer.h"
#include "mpu6050.h"
#include "max30102.h"
#include "ds18b20.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "sensor_svc";

// 任务配置
#define SENSOR_TASK_STACK       4096
#define SENSOR_TASK_PRIO        6

// DS18B20 转换时间 (12位精度需要750ms)
#define DS18B20_CONVERT_TIME_MS 750

// 事件发布间隔 (ms)
#define EVENT_PUBLISH_INTERVAL  100

// PPG 环形缓冲区大小（必须是 2 的幂）
#define PPG_RING_SIZE           64
#define PPG_RING_MASK           (PPG_RING_SIZE - 1)

/**
 * @brief PPG 环形缓冲区（保存 FIFO 读出的所有样本）
 */
typedef struct {
    uint32_t red[PPG_RING_SIZE];
    uint32_t ir[PPG_RING_SIZE];
    uint16_t head;      // 写入位置
    uint16_t tail;      // 读取位置
} ppg_ring_t;

static ppg_ring_t s_ppg_ring = {0};

/**
 * @brief DS18B20 异步采样状态
 */
typedef enum {
    TEMP_STATE_IDLE = 0,        // 空闲状态
    TEMP_STATE_CONVERTING,      // 正在转换
    TEMP_STATE_READY            // 转换完成，可读取
} temp_sample_state_t;

// 传感器服务上下文
typedef struct {
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;
    sensor_data_t latest_data;
    sampling_mode_t temp_mode;
    sampling_mode_t hr_mode;
    uint32_t temp_last_sample;
    uint32_t last_event_publish;    // 上次事件发布时间
    // DS18B20 异步采样状态
    temp_sample_state_t temp_state;
    uint32_t temp_convert_start;    // 转换开始时间
    // 温度异常状态
    bool temp_alert_active;         // 温度异常标志（自动触发的实时模式）
    // 心率测量窗口状态机
    hr_measure_state_t hr_measure_state;    // 当前测量状态
    uint32_t hr_measure_start_time;         // 测量开始时间
    uint32_t hr_last_auto_trigger;          // 上次自动触发时间
    bool running;
    bool initialized;
} sensor_service_ctx_t;

static sensor_service_ctx_t s_ctx = {0};

/**
 * @brief 异步采样温度传感器 - 启动转换
 * @return true 如果成功启动转换
 */
static bool temp_start_convert(void)
{
    esp_err_t ret = ds18b20_start_convert();
    if (ret == ESP_OK) {
        s_ctx.temp_state = TEMP_STATE_CONVERTING;
        s_ctx.temp_convert_start = get_timestamp_ms();
        return true;
    }
    ESP_LOGW(TAG, "Failed to start temp convert");
    return false;
}

/**
 * @brief 异步采样温度传感器 - 读取结果
 * @return true 如果成功读取温度
 */
static bool temp_read_result(void)
{
    float temp = 0;
    esp_err_t ret = ds18b20_read_scratchpad(&temp);

    if (ret == ESP_OK) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.latest_data.temperature = temp;
        s_ctx.latest_data.data_valid |= SENSOR_TEMP;
        xSemaphoreGive(s_ctx.mutex);

        // 温度异常检测（使用滞回避免频繁切换）
        bool is_temp_abnormal = (temp >= SENSOR_TEMP_HIGH_ALERT_THRESHOLD) ||
                                (temp <= SENSOR_TEMP_LOW_ALERT_THRESHOLD);
        bool is_temp_normal = (temp < SENSOR_TEMP_HIGH_NORMAL_THRESHOLD) &&
                              (temp > SENSOR_TEMP_LOW_NORMAL_THRESHOLD);

        if (!s_ctx.temp_alert_active && is_temp_abnormal) {
            // 温度异常，进入异常状态，切换到实时采样
            s_ctx.temp_alert_active = true;
            s_ctx.temp_mode = SAMPLING_MODE_REALTIME;
            ESP_LOGW(TAG, "Temp alert: %.2f°C, switch to realtime", temp);
        } else if (s_ctx.temp_alert_active && is_temp_normal) {
            // 温度恢复正常，退出异常状态，恢复正常采样
            s_ctx.temp_alert_active = false;
            s_ctx.temp_mode = SAMPLING_MODE_NORMAL;
            ESP_LOGI(TAG, "Temp normal: %.2f°C, switch to normal", temp);
        } else if (s_ctx.temp_alert_active) {
            // 实时模式下每秒打印温度日志
            ESP_LOGW(TAG, "[REALTIME] Temp: %.2f°C", temp);
        }

        s_ctx.temp_state = TEMP_STATE_IDLE;
        return true;
    }
    ESP_LOGW(TAG, "Failed to read temperature");
    s_ctx.temp_state = TEMP_STATE_IDLE;
    return false;
}

/**
 * @brief 异步温度采样状态机处理
 * @param now 当前时间戳
 * @param interval 采样间隔
 */
static void sample_temperature_async(uint32_t now, uint32_t interval)
{
    switch (s_ctx.temp_state) {
        case TEMP_STATE_IDLE:
            // 检查是否到达采样时间
            if ((now - s_ctx.temp_last_sample) >= interval) {
                if (temp_start_convert()) {
                    s_ctx.temp_last_sample = now;
                }
            }
            break;

        case TEMP_STATE_CONVERTING:
            // 检查转换是否完成
            if ((now - s_ctx.temp_convert_start) >= DS18B20_CONVERT_TIME_MS) {
                temp_read_result();
            }
            break;

        default:
            s_ctx.temp_state = TEMP_STATE_IDLE;
            break;
    }
}

/**
 * @brief 采样心率血氧传感器（持续读取 FIFO）
 * @return true 如果读取到新的 PPG 数据
 */
static bool sample_hr_spo2(void)
{
    uint32_t red[32], ir[32];
    uint8_t count = 32;

    esp_err_t ret = max30102_read_fifo(red, ir, &count);

    if (ret == ESP_OK && count > 0) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        // 将所有 FIFO 样本写入环形缓冲区
        for (uint8_t i = 0; i < count; i++) {
            uint16_t next = (s_ppg_ring.head + 1) & PPG_RING_MASK;
            if (next == s_ppg_ring.tail) {
                // 缓冲区满，丢弃最老的样本
                s_ppg_ring.tail = (s_ppg_ring.tail + 1) & PPG_RING_MASK;
            }
            s_ppg_ring.red[s_ppg_ring.head] = red[i];
            s_ppg_ring.ir[s_ppg_ring.head] = ir[i];
            s_ppg_ring.head = next;
        }
        // 同时保留 latest_data 兼容性（取最后一个样本）
        s_ctx.latest_data.ppg_red = red[count - 1];
        s_ctx.latest_data.ppg_ir = ir[count - 1];
        s_ctx.latest_data.ppg_fresh = true;
        s_ctx.latest_data.data_valid |= SENSOR_HR_SPO2;
        xSemaphoreGive(s_ctx.mutex);
        return true;
    }
    return false;
}

/**
 * @brief 采样 IMU 传感器
 */
static void sample_imu(void)
{
    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    esp_err_t ret1 = mpu6050_read_accel(&ax, &ay, &az);
    esp_err_t ret2 = mpu6050_read_gyro(&gx, &gy, &gz);

    if (ret1 == ESP_OK && ret2 == ESP_OK) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.latest_data.accel_x = ax;
        s_ctx.latest_data.accel_y = ay;
        s_ctx.latest_data.accel_z = az;
        s_ctx.latest_data.gyro_x = gx;
        s_ctx.latest_data.gyro_y = gy;
        s_ctx.latest_data.gyro_z = gz;
        s_ctx.latest_data.data_valid |= SENSOR_IMU;
        xSemaphoreGive(s_ctx.mutex);
    }
}

/**
 * @brief 传感器采样任务
 */
static void sensor_task(void *arg)
{
    uint32_t now;
    uint32_t temp_interval;

    ESP_LOGI(TAG, "Sensor task started");

    while (s_ctx.running) {
        now = get_timestamp_ms();

        // 计算温度采样间隔
        temp_interval = (s_ctx.temp_mode == SAMPLING_MODE_REALTIME)
                        ? SENSOR_REALTIME_INTERVAL
                        : SENSOR_TEMP_NORMAL_INTERVAL;

        // 温度异步采样（非阻塞）
        sample_temperature_async(now, temp_interval);

        // 心率测量窗口状态机
        switch (s_ctx.hr_measure_state) {
            case HR_MEASURE_IDLE: {
                // 根据模式选择间隔：实时模式 1秒，正常模式 120秒
                uint32_t hr_interval = (s_ctx.hr_mode == SAMPLING_MODE_REALTIME)
                                       ? SENSOR_REALTIME_INTERVAL
                                       : SENSOR_HR_AUTO_INTERVAL_MS;
                if ((now - s_ctx.hr_last_auto_trigger) >= hr_interval) {
                    ESP_LOGI(TAG, "Auto trigger HR measure window");
                    max30102_wakeup();
                    // 清空 PPG 环形缓冲区，避免混入旧数据
                    s_ppg_ring.head = s_ppg_ring.tail = 0;
                    s_ctx.hr_measure_state = HR_MEASURE_MEASURING;
                    s_ctx.hr_measure_start_time = now;
                    s_ctx.hr_last_auto_trigger = now;
                }
                // IDLE 状态不读取 PPG FIFO（节省功耗）
                break;
            }

            case HR_MEASURE_MEASURING:
                // 持续读取 FIFO，防止溢出
                sample_hr_spo2();
                // 检查15秒窗口是否结束
                if ((now - s_ctx.hr_measure_start_time) >= SENSOR_HR_MEASURE_WINDOW_MS) {
                    ESP_LOGI(TAG, "HR measure window complete (15s)");
                    s_ctx.hr_measure_state = HR_MEASURE_COMPLETE;
                }
                break;

            case HR_MEASURE_COMPLETE:
                // 标记数据就绪，回到空闲
                xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
                s_ctx.latest_data.ppg_fresh = true;
                xSemaphoreGive(s_ctx.mutex);
                max30102_shutdown();
                s_ctx.hr_measure_state = HR_MEASURE_IDLE;
                s_ctx.hr_last_auto_trigger = now;  // 从测量结束时开始计算下次间隔
                ESP_LOGI(TAG, "HR sensor shutdown, back to IDLE");
                break;
        }

        // IMU 持续采样 (50Hz)
        sample_imu();

        // 更新时间戳
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.latest_data.timestamp = now;
        xSemaphoreGive(s_ctx.mutex);

        // 定期发布传感器数据事件
        if ((now - s_ctx.last_event_publish) >= EVENT_PUBLISH_INTERVAL) {
            event_data_t evt_data;
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            // 同步步数到传感器数据
            s_ctx.latest_data.steps = pedometer_get_steps();
            memcpy(&evt_data.sensor, &s_ctx.latest_data, sizeof(sensor_data_t));
            // 发布后清除 ppg_fresh，避免下次事件误报新数据
            s_ctx.latest_data.ppg_fresh = false;
            xSemaphoreGive(s_ctx.mutex);
            event_publish(EVT_SENSOR_DATA, &evt_data);
            s_ctx.last_event_publish = now;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_IMU_NORMAL_INTERVAL));
    }

    vTaskDelete(NULL);
}

/**
 * @brief 初始化传感器服务
 */
esp_err_t sensor_service_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Sensor service already initialized");
        return ESP_OK;
    }

    // 创建互斥锁
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化数据
    memset(&s_ctx.latest_data, 0, sizeof(sensor_data_t));
    s_ctx.temp_mode = SAMPLING_MODE_NORMAL;
    s_ctx.hr_mode = SAMPLING_MODE_NORMAL;
    s_ctx.temp_state = TEMP_STATE_IDLE;
    s_ctx.temp_convert_start = 0;
    s_ctx.temp_alert_active = false;
    s_ctx.last_event_publish = 0;
    s_ctx.hr_measure_state = HR_MEASURE_IDLE;
    s_ctx.hr_measure_start_time = 0;
    s_ctx.hr_last_auto_trigger = 0;
    s_ctx.running = false;

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Sensor service initialized");
    return ESP_OK;
}

/**
 * @brief 启动传感器服务
 */
esp_err_t sensor_service_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Sensor service not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.running) {
        ESP_LOGW(TAG, "Sensor service already running");
        return ESP_OK;
    }

    s_ctx.running = true;
    // 初始化为当前时间减去采样间隔，确保立即触发第一次温度采样
    uint32_t now = get_timestamp_ms();
    s_ctx.temp_last_sample = now - SENSOR_TEMP_NORMAL_INTERVAL;
    // 立即触发第一次心率测量
    s_ctx.hr_last_auto_trigger = now - SENSOR_HR_AUTO_INTERVAL_MS;

    BaseType_t ret = xTaskCreate(
        sensor_task,
        "sensor_svc",
        SENSOR_TASK_STACK,
        NULL,
        SENSOR_TASK_PRIO,
        &s_ctx.task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        s_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sensor service started");
    return ESP_OK;
}

/**
 * @brief 停止传感器服务
 */
esp_err_t sensor_service_stop(void)
{
    if (!s_ctx.running) {
        return ESP_OK;
    }

    s_ctx.running = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Sensor service stopped");
    return ESP_OK;
}

/**
 * @brief 获取最新传感器数据
 */
esp_err_t sensor_get_latest(sensor_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memcpy(data, &s_ctx.latest_data, sizeof(sensor_data_t));
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

/**
 * @brief 设置传感器采样模式
 */
esp_err_t sensor_set_mode(uint8_t sensor_mask, sampling_mode_t mode)
{
    if (sensor_mask & SENSOR_TEMP) {
        s_ctx.temp_mode = mode;
        ESP_LOGI(TAG, "Temp mode: %s",
                 mode == SAMPLING_MODE_REALTIME ? "REALTIME" : "NORMAL");
    }

    if (sensor_mask & SENSOR_HR_SPO2) {
        s_ctx.hr_mode = mode;
        ESP_LOGI(TAG, "HR mode: %s",
                 mode == SAMPLING_MODE_REALTIME ? "REALTIME" : "NORMAL");
    }

    // 发布采样模式变更事件
    event_data_t evt_data;
    evt_data.sampling.mode = mode;
    evt_data.sampling.sensor_mask = sensor_mask;
    event_publish(EVT_SAMPLING_MODE, &evt_data);

    return ESP_OK;
}

/**
 * @brief 获取当前采样模式
 */
sampling_mode_t sensor_get_mode(uint8_t sensor_mask)
{
    if (sensor_mask & SENSOR_TEMP) {
        return s_ctx.temp_mode;
    }
    if (sensor_mask & SENSOR_HR_SPO2) {
        return s_ctx.hr_mode;
    }
    return SAMPLING_MODE_NORMAL;
}

/**
 * @brief 手动触发心率测量（进入15秒测量窗口）
 */
esp_err_t sensor_start_hr_measure(void)
{
    if (!s_ctx.initialized || !s_ctx.running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.hr_measure_state == HR_MEASURE_MEASURING) {
        ESP_LOGW(TAG, "HR measure already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Manual trigger HR measure window");
    max30102_wakeup();
    // 清空 PPG 环形缓冲区
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ppg_ring.head = s_ppg_ring.tail = 0;
    xSemaphoreGive(s_ctx.mutex);
    s_ctx.hr_measure_state = HR_MEASURE_MEASURING;
    s_ctx.hr_measure_start_time = get_timestamp_ms();
    s_ctx.hr_last_auto_trigger = s_ctx.hr_measure_start_time;
    return ESP_OK;
}

/**
 * @brief 获取当前心率测量状态
 */
hr_measure_state_t sensor_get_hr_measure_state(void)
{
    return s_ctx.hr_measure_state;
}

/**
 * @brief 取出环形缓冲区中所有已缓存的 PPG 样本
 */
esp_err_t sensor_drain_ppg(ppg_batch_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    uint8_t n = 0;
    while (s_ppg_ring.tail != s_ppg_ring.head && n < PPG_BATCH_MAX) {
        out->red[n] = s_ppg_ring.red[s_ppg_ring.tail];
        out->ir[n]  = s_ppg_ring.ir[s_ppg_ring.tail];
        s_ppg_ring.tail = (s_ppg_ring.tail + 1) & PPG_RING_MASK;
        n++;
    }
    out->count = n;

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}
