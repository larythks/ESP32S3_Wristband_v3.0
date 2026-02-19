/**
 * @file alarm_manager.c
 * @brief 报警管理器 - 状态机实现
 *
 * 管理 IDLE -> PRE_ALARM -> ALARMING -> ACKED 状态转换，
 * 控制 WS2812 灯光和 BLE Alarm Notify 发送。
 */

#include "alarm_manager.h"
#include "event_bus.h"
#include "ble_service.h"
#include "ble_gatt_defs.h"
#include "ws2812.h"
#include "audio_player.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "alarm_mgr";

/* ============== 配置常量 ============== */

#define PRE_ALARM_TIMEOUT_MS    15000   /* 预报警倒计时 15 秒 */
#define ACKED_TIMEOUT_MS        2000    /* ACKED 状态持续 2 秒后回 IDLE */
#define BLINK_YELLOW_INTERVAL   500     /* PRE_ALARM 黄色闪烁间隔 */
#define BLINK_RED_INTERVAL      200     /* ALARMING 红色快闪间隔 */
#define TIMER_RETRY_MS          50      /* 定时器回调拿锁失败时短暂重试 */

/* ============== 内部状态 ============== */

typedef struct {
    alarm_state_t state;            /* 当前状态 */
    alarm_data_t  current_alarm;    /* 当前报警数据 */
    uint32_t      event_id;         /* 全局递增报警事件 ID */
    SemaphoreHandle_t mutex;        /* 状态保护互斥锁 */
    TimerHandle_t pre_alarm_timer;  /* 预报警倒计时定时器 */
    TimerHandle_t acked_timer;      /* ACKED 超时定时器 */
    bool          initialized;      /* 初始化标志 */
} alarm_ctx_t;

static alarm_ctx_t s_ctx = {0};

/* ============== 报警缓存环形队列 ============== */
static alarm_cache_entry_t s_cache[ALARM_CACHE_SIZE];
static uint8_t s_cache_head  = 0;
static uint8_t s_cache_count = 0;

/* 报警音频循环播放 */
static TaskHandle_t s_loop_task = NULL;
static volatile bool s_loop_active = false;
static const char *s_loop_wav = NULL;

#define ALARM_LOOP_GAP_MS  500   /* 两次播报之间间隔 */
#define ALARM_LOOP_STACK   4096

/* ============== alert_type_t -> ble_alarm_type_t 映射 ============== */

/**
 * @brief 将 event_bus 的 alert_type_t 映射到 BLE 的 ble_alarm_type_t
 *
 * 注意: 两个枚举的值不同！
 *   alert_type_t:     NONE=0, TEMP_HIGH=1, ..., PRE_ALARM_FALL=6, FALL=7, MANUAL=8, CALL_FAMILY=9
 *   ble_alarm_type_t: NONE=0, TEMP_HIGH=1, ..., SPO2_LOW=5, FALL=6, MANUAL=7, RESERVED_8=8, CALL_FAMILY=9
 */
static uint8_t alert_to_ble_alarm_type(alert_type_t type)
{
    switch (type) {
    case ALERT_TYPE_NONE:           return BLE_ALARM_TYPE_NONE;
    case ALERT_TYPE_TEMP_HIGH:      return BLE_ALARM_TYPE_TEMP_HIGH;
    case ALERT_TYPE_TEMP_LOW:       return BLE_ALARM_TYPE_TEMP_LOW;
    case ALERT_TYPE_HR_HIGH:        return BLE_ALARM_TYPE_HR_HIGH;
    case ALERT_TYPE_HR_LOW:         return BLE_ALARM_TYPE_HR_LOW;
    case ALERT_TYPE_SPO2_LOW:       return BLE_ALARM_TYPE_SPO2_LOW;
    case ALERT_TYPE_FALL:           return BLE_ALARM_TYPE_FALL;
    case ALERT_TYPE_MANUAL:         return BLE_ALARM_TYPE_MANUAL;
    case ALERT_TYPE_CALL_FAMILY:    return BLE_ALARM_TYPE_CALL_FAMILY;
    default:                        return BLE_ALARM_TYPE_NONE;
    }
}

/* ============== 音频播放辅助 ============== */

/**
 * @brief 根据 alert_type 返回对应的 WAV 文件名（字符串字面量）
 */
static const char *get_alarm_wav(alert_type_t type)
{
    switch (type) {
    case ALERT_TYPE_TEMP_HIGH:      return "alarm_temp_high";
    case ALERT_TYPE_TEMP_LOW:       return "alarm_temp_low";
    case ALERT_TYPE_HR_HIGH:        return "alarm_hr_high";
    case ALERT_TYPE_HR_LOW:         return "alarm_hr_low";
    case ALERT_TYPE_SPO2_LOW:       return "alarm_spo2_low";
    case ALERT_TYPE_PRE_ALARM_FALL: return "pre_alarm_fall";
    case ALERT_TYPE_FALL:           return "alarm_help";
    case ALERT_TYPE_MANUAL:         return "alarm_help";
    case ALERT_TYPE_CALL_FAMILY:    return "call_family";
    default:                        return NULL;
    }
}

/**
 * @brief 在独立任务中启动报警音频播放（不阻塞调用者）
 *
 * Uses audio_play_wav_async() which pre-sets s_playing to avoid race
 * conditions with audio_wait_done().
 */
static void alarm_audio_play_async(const char *wav_name)
{
    if (wav_name == NULL) {
        return;
    }
    /* 先停止当前播放（如果有） */
    audio_play_stop();
    audio_play_wav_async(wav_name);
}

/**
 * @brief 报警音频循环播放任务
 *
 * 在 ALARMING 状态下持续循环播放指定 WAV 文件，
 * 直到 s_loop_active 被置为 false。
 */
static void alarm_loop_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Alarm loop started: %s", s_loop_wav ? s_loop_wav : "null");

    while (s_loop_active && s_loop_wav != NULL) {
        audio_play_wav(s_loop_wav);
        /* 播放结束或被 audio_play_stop() 中断后检查是否继续 */
        if (!s_loop_active) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(ALARM_LOOP_GAP_MS));
    }

    ESP_LOGI(TAG, "Alarm loop stopped");
    s_loop_task = NULL;
    vTaskDelete(NULL);
}

/* ============== 前向声明 ============== */

static void enter_state(alarm_state_t new_state);
static void send_ble_alarm(void);
static void publish_alarm_state_event(void);
static void on_health_alert(const event_t *event, void *user_data);
static void on_fall_detected(const event_t *event, void *user_data);
static void on_ble_conn_change(const event_t *event, void *user_data);
static void alarm_retry_task(void *arg);
static void pre_alarm_timer_cb(TimerHandle_t timer);
static void acked_timer_cb(TimerHandle_t timer);

/* ============== 报警缓存操作 (调用前必须持有 s_ctx.mutex) ============== */

static void alarm_cache_push(const ble_alarm_t *alarm)
{
    if (s_cache_count < ALARM_CACHE_SIZE) {
        uint8_t idx = (s_cache_head + s_cache_count) % ALARM_CACHE_SIZE;
        s_cache[idx].event_id    = alarm->event_id;
        s_cache[idx].alarm_type  = alarm->alarm_type;
        s_cache[idx].value       = alarm->value;
        s_cache[idx].timestamp   = alarm->timestamp;
        s_cache[idx].battery     = alarm->battery;
        s_cache[idx].retry_count = 0;
        s_cache[idx].acked       = false;
        s_cache[idx].valid       = true;
        s_cache_count++;
    } else {
        ESP_LOGW(TAG, "Cache full, dropping oldest unacked entry");
        s_cache[s_cache_head].event_id    = alarm->event_id;
        s_cache[s_cache_head].alarm_type  = alarm->alarm_type;
        s_cache[s_cache_head].value       = alarm->value;
        s_cache[s_cache_head].timestamp   = alarm->timestamp;
        s_cache[s_cache_head].battery     = alarm->battery;
        s_cache[s_cache_head].retry_count = 0;
        s_cache[s_cache_head].acked       = false;
        s_cache[s_cache_head].valid       = true;
        s_cache_head = (s_cache_head + 1) % ALARM_CACHE_SIZE;
    }
}

static void alarm_cache_ack_by_id(uint32_t event_id)
{
    for (uint8_t i = 0; i < s_cache_count; i++) {
        uint8_t idx = (s_cache_head + i) % ALARM_CACHE_SIZE;
        if (s_cache[idx].valid && s_cache[idx].event_id == event_id) {
            s_cache[idx].acked = true;
            return;
        }
    }
}

static uint8_t alarm_cache_get_unacked(alarm_cache_entry_t *out, uint8_t max_count)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_cache_count && n < max_count; i++) {
        uint8_t idx = (s_cache_head + i) % ALARM_CACHE_SIZE;
        if (s_cache[idx].valid && !s_cache[idx].acked &&
            s_cache[idx].retry_count < ALARM_RETRY_MAX) {
            out[n++] = s_cache[idx];
        }
    }
    return n;
}

static void alarm_cache_increment_retry(uint32_t event_id)
{
    for (uint8_t i = 0; i < s_cache_count; i++) {
        uint8_t idx = (s_cache_head + i) % ALARM_CACHE_SIZE;
        if (s_cache[idx].valid && s_cache[idx].event_id == event_id) {
            s_cache[idx].retry_count++;
            return;
        }
    }
}

/* ============== 状态切换核心 (调用前必须已持有互斥锁) ============== */

static void enter_state(alarm_state_t new_state)
{
    alarm_state_t old_state = s_ctx.state;
    if (old_state == new_state) {
        return;
    }

    ESP_LOGI(TAG, "State: %d -> %d (type=%d)", old_state, new_state, s_ctx.current_alarm.type);
    s_ctx.state = new_state;

    /* 停止所有定时器 (安全调用，即使未运行) */
    xTimerStop(s_ctx.pre_alarm_timer, 0);
    xTimerStop(s_ctx.acked_timer, 0);

    /* 离开 ALARMING 状态时停止循环播放 */
    if (old_state == ALARM_STATE_ALARMING) {
        s_loop_active = false;
        audio_play_stop();
    }

    switch (new_state) {
    case ALARM_STATE_IDLE:
        ws2812_blink_stop();
        ws2812_off();
        audio_play_stop();
        break;

    case ALARM_STATE_PRE_ALARM:
        /* 黄色闪烁，启动 15 秒倒计时 */
        ws2812_blink_start(255, 165, 0, BLINK_YELLOW_INTERVAL);
        xTimerStart(s_ctx.pre_alarm_timer, 0);
        alarm_audio_play_async("pre_alarm_fall");
        break;

    case ALARM_STATE_ALARMING:
        /* 红色快闪 + 发送 BLE Alarm + 循环播报 */
        ws2812_blink_start(255, 0, 0, BLINK_RED_INTERVAL);
        send_ble_alarm();
        audio_play_stop();  /* 停止 PRE_ALARM 阶段残留的异步音频 */
        s_loop_wav = get_alarm_wav(s_ctx.current_alarm.type);
        if (s_loop_wav != NULL) {
            s_loop_active = true;
            xTaskCreate(alarm_loop_task, "alarm_loop", ALARM_LOOP_STACK,
                        NULL, 3, &s_loop_task);
        }
        break;

    case ALARM_STATE_ACKED:
        /* 绿色常亮，2 秒后自动回 IDLE */
        ws2812_blink_stop();
        ws2812_set_color(0, 255, 0);
        xTimerStart(s_ctx.acked_timer, 0);
        break;
    }

    /* 发布状态变更事件 */
    publish_alarm_state_event();
}

/* ============== BLE Alarm 发送 ============== */

static void send_ble_alarm(void)
{
    s_ctx.event_id++;

    ble_alarm_t pkt = {0};
    pkt.event_id  = s_ctx.event_id;
    pkt.alarm_type = alert_to_ble_alarm_type(s_ctx.current_alarm.type);
    pkt.value     = s_ctx.current_alarm.value;
    pkt.battery   = BLE_BATTERY_DEFAULT;
    pkt.timestamp = ble_get_unix_timestamp();

    esp_err_t ret = ble_notify_alarm(&pkt);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BLE Alarm sent: id=%lu type=%d",
                 (unsigned long)pkt.event_id, pkt.alarm_type);
    } else {
        ESP_LOGW(TAG, "BLE Alarm failed: %s", esp_err_to_name(ret));
    }

    /* 缓存报警（无论 Notify 是否成功，断连时也需要缓存以便补发） */
    alarm_cache_push(&pkt);
}

/* ============== 事件总线状态发布 ============== */

static void publish_alarm_state_event(void)
{
    event_data_t evt = {0};
    evt.health_alert.type      = s_ctx.current_alarm.type;
    evt.health_alert.level     = s_ctx.current_alarm.level;
    evt.health_alert.value     = s_ctx.current_alarm.value;
    evt.health_alert.timestamp = s_ctx.current_alarm.timestamp;
    event_publish(EVT_ALARM_STATE, &evt);
}

/* ============== 定时器回调 ============== */

/**
 * PRE_ALARM 倒计时到期 -> 升级到 ALARMING
 * 注意: 定时器回调运行在 Timer Service 任务上下文。
 * 为避免阻塞定时器任务，这里采用非阻塞拿锁；失败后短延时重试。
 */
static void pre_alarm_timer_cb(TimerHandle_t timer)
{
    if (xSemaphoreTake(s_ctx.mutex, 0) == pdTRUE) {
        if (s_ctx.state == ALARM_STATE_PRE_ALARM) {
            ESP_LOGW(TAG, "Pre-alarm timeout, escalating to ALARMING");
            enter_state(ALARM_STATE_ALARMING);
        }
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    /* Keep timer callback non-blocking: retry shortly instead of waiting in timer task. */
    if (xTimerChangePeriod(timer, pdMS_TO_TICKS(TIMER_RETRY_MS), 0) != pdPASS) {
        ESP_LOGW(TAG, "pre_alarm retry schedule failed");
    }
}

/**
 * ACKED 超时 -> 回到 IDLE
 */
static void acked_timer_cb(TimerHandle_t timer)
{
    if (xSemaphoreTake(s_ctx.mutex, 0) == pdTRUE) {
        if (s_ctx.state == ALARM_STATE_ACKED) {
            ESP_LOGI(TAG, "ACKED timeout, returning to IDLE");
            enter_state(ALARM_STATE_IDLE);
        }
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    if (xTimerChangePeriod(timer, pdMS_TO_TICKS(TIMER_RETRY_MS), 0) != pdPASS) {
        ESP_LOGW(TAG, "acked retry schedule failed");
    }
}

/* ============== 事件总线回调 ============== */

static void on_health_alert(const event_t *event, void *user_data)
{
    (void)user_data;
    const health_alert_t *alert = &event->data.health_alert;

    /* 仅处理 WARNING 及以上级别 */
    if (alert->level < ALERT_LEVEL_WARNING) {
        return;
    }

    alarm_data_t data = {
        .type      = alert->type,
        .level     = alert->level,
        .value     = alert->value,
        .timestamp = alert->timestamp,
    };

    alarm_trigger(alert->type, &data);
}

static void on_fall_detected(const event_t *event, void *user_data)
{
    (void)user_data;
    (void)event;

    alarm_data_t data = {
        .type      = ALERT_TYPE_FALL,
        .level     = ALERT_LEVEL_ALARM,
        .value     = 0,
        .timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
    };

    alarm_trigger(ALERT_TYPE_FALL, &data);
}

/* ============== BLE 重连补发 ============== */

static void on_ble_conn_change(const event_t *event, void *user_data)
{
    (void)user_data;
    uint8_t conn_state = event->data.raw[0];
    if (conn_state == BLE_CONN_STATE_CONNECTED) {
        xTaskCreate(alarm_retry_task, "alarm_retry", 3072, NULL, 2, NULL);
    }
}

static void alarm_retry_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));

    alarm_cache_entry_t entries[ALARM_CACHE_SIZE];

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    uint8_t count = alarm_cache_get_unacked(entries, ALARM_CACHE_SIZE);
    xSemaphoreGive(s_ctx.mutex);

    ESP_LOGI(TAG, "Retry: %d unacked alarms to resend", count);

    for (uint8_t i = 0; i < count; i++) {
        if (!ble_is_connected()) break;

        ble_alarm_t pkt = {
            .event_id   = entries[i].event_id,
            .alarm_type = entries[i].alarm_type,
            .value      = entries[i].value,
            .battery    = entries[i].battery,
            .timestamp  = entries[i].timestamp,
        };

        esp_err_t ret = ble_notify_alarm(&pkt);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Retried alarm id=%lu (%d/%d)",
                     (unsigned long)entries[i].event_id,
                     entries[i].retry_count + 1, ALARM_RETRY_MAX);
        }

        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        alarm_cache_increment_retry(entries[i].event_id);
        xSemaphoreGive(s_ctx.mutex);

        vTaskDelay(pdMS_TO_TICKS(ALARM_RETRY_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Retry task done");
    vTaskDelete(NULL);
}

/* ============== 公共 API ============== */

esp_err_t alarm_manager_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* 创建互斥锁 */
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* 创建预报警倒计时定时器 (单次触发) */
    s_ctx.pre_alarm_timer = xTimerCreate(
        "pre_alarm",
        pdMS_TO_TICKS(PRE_ALARM_TIMEOUT_MS),
        pdFALSE,   /* 单次 */
        NULL,
        pre_alarm_timer_cb
    );
    if (s_ctx.pre_alarm_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create pre_alarm timer");
        return ESP_ERR_NO_MEM;
    }

    /* 创建 ACKED 超时定时器 (单次触发) */
    s_ctx.acked_timer = xTimerCreate(
        "acked",
        pdMS_TO_TICKS(ACKED_TIMEOUT_MS),
        pdFALSE,   /* 单次 */
        NULL,
        acked_timer_cb
    );
    if (s_ctx.acked_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create acked timer");
        return ESP_ERR_NO_MEM;
    }

    /* 初始化状态 */
    s_ctx.state    = ALARM_STATE_IDLE;
    s_ctx.event_id = 0;

    /* 订阅事件总线 */
    event_subscribe(EVT_HEALTH_ALERT, on_health_alert, NULL);
    event_subscribe(EVT_FALL_DETECTED, on_fall_detected, NULL);
    event_subscribe(EVT_BLE_CONN, on_ble_conn_change, NULL);

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Alarm manager initialized");
    return ESP_OK;
}

void alarm_trigger(alert_type_t type, alarm_data_t *data)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex in alarm_trigger");
        return;
    }

    /* 填充报警数据 */
    if (data != NULL) {
        s_ctx.current_alarm = *data;
    } else {
        memset(&s_ctx.current_alarm, 0, sizeof(alarm_data_t));
        s_ctx.current_alarm.timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    s_ctx.current_alarm.type = type;

    /* 状态转换逻辑 */
    switch (s_ctx.state) {
    case ALARM_STATE_IDLE:
        if (type == ALERT_TYPE_FALL) {
            enter_state(ALARM_STATE_PRE_ALARM);
        } else {
            enter_state(ALARM_STATE_ALARMING);
        }
        break;

    case ALARM_STATE_PRE_ALARM:
        /* 预报警期间收到新的非跌倒报警，直接升级 */
        if (type != ALERT_TYPE_FALL) {
            enter_state(ALARM_STATE_ALARMING);
        }
        break;

    case ALARM_STATE_ALARMING:
        /* 已在报警中，更新数据并重新发送 BLE + 通知订阅者 */
        send_ble_alarm();
        publish_alarm_state_event();
        break;

    case ALARM_STATE_ACKED:
        /* ACKED 状态收到新报警，重新进入 ALARMING */
        enter_state(ALARM_STATE_ALARMING);
        break;
    }

    xSemaphoreGive(s_ctx.mutex);
}

void alarm_cancel(void)
{
    if (!s_ctx.initialized) {
        return;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex in alarm_cancel");
        return;
    }

    if (s_ctx.state == ALARM_STATE_PRE_ALARM || s_ctx.state == ALARM_STATE_ALARMING) {
        ESP_LOGI(TAG, "Alarm cancelled from state %d", s_ctx.state);
        enter_state(ALARM_STATE_IDLE);
    }

    xSemaphoreGive(s_ctx.mutex);
}

void alarm_ack(uint32_t event_id)
{
    if (!s_ctx.initialized) {
        return;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex in alarm_ack");
        return;
    }

    alarm_cache_ack_by_id(event_id);

    if (s_ctx.state == ALARM_STATE_ALARMING) {
        ESP_LOGI(TAG, "Alarm acknowledged");
        enter_state(ALARM_STATE_ACKED);
    }

    xSemaphoreGive(s_ctx.mutex);
}

alarm_state_t alarm_get_state(void)
{
    if (!s_ctx.initialized) {
        return ALARM_STATE_IDLE;
    }

    alarm_state_t state;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = s_ctx.state;
        xSemaphoreGive(s_ctx.mutex);
    } else {
        state = s_ctx.state;  /* 降级: 无锁读取 */
    }
    return state;
}

bool alarm_cache_find(uint32_t event_id, alarm_cache_entry_t *out)
{
    if (!s_ctx.initialized || out == NULL) {
        return false;
    }
    bool found = false;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (uint8_t i = 0; i < s_cache_count; i++) {
            uint8_t idx = (s_cache_head + i) % ALARM_CACHE_SIZE;
            if (s_cache[idx].valid && s_cache[idx].event_id == event_id) {
                *out = s_cache[idx];
                found = true;
                break;
            }
        }
        xSemaphoreGive(s_ctx.mutex);
    }
    return found;
}
