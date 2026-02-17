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

#include "freertos/FreeRTOS.h"
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

/* ============== alert_type_t -> ble_alarm_type_t 映射 ============== */

/**
 * @brief 将 event_bus 的 alert_type_t 映射到 BLE 的 ble_alarm_type_t
 *
 * 注意: 两个枚举的值不同！
 *   alert_type_t:     NONE=0, TEMP_HIGH=1, ..., SPO2_WARNING=6, FALL=7, MANUAL=8, CALL_FAMILY=9
 *   ble_alarm_type_t: NONE=0, TEMP_HIGH=1, ..., SPO2_LOW=5, FALL=6, MANUAL=7, SPO2_WARNING=8, CALL_FAMILY=9
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
    case ALERT_TYPE_SPO2_WARNING:   return BLE_ALARM_TYPE_SPO2_WARNING;
    case ALERT_TYPE_FALL:           return BLE_ALARM_TYPE_FALL;
    case ALERT_TYPE_MANUAL:         return BLE_ALARM_TYPE_MANUAL;
    case ALERT_TYPE_CALL_FAMILY:    return BLE_ALARM_TYPE_CALL_FAMILY;
    default:                        return BLE_ALARM_TYPE_NONE;
    }
}

/* ============== 前向声明 ============== */

static void enter_state(alarm_state_t new_state);
static void send_ble_alarm(void);
static void publish_alarm_state_event(void);
static void on_health_alert(const event_t *event, void *user_data);
static void on_fall_detected(const event_t *event, void *user_data);
static void pre_alarm_timer_cb(TimerHandle_t timer);
static void acked_timer_cb(TimerHandle_t timer);

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

    switch (new_state) {
    case ALARM_STATE_IDLE:
        ws2812_blink_stop();
        ws2812_off();
        break;

    case ALARM_STATE_PRE_ALARM:
        /* 黄色闪烁，启动 15 秒倒计时 */
        ws2812_blink_start(255, 165, 0, BLINK_YELLOW_INTERVAL);
        xTimerStart(s_ctx.pre_alarm_timer, 0);
        break;

    case ALARM_STATE_ALARMING:
        /* 红色快闪 + 发送 BLE Alarm */
        ws2812_blink_start(255, 0, 0, BLINK_RED_INTERVAL);
        send_ble_alarm();
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

/* ============== 定时器回调 (仅设标志，符合 CLAUDE.md 规则) ============== */

/**
 * PRE_ALARM 倒计时到期 -> 升级到 ALARMING
 * 注意: 定时器回调运行在 Timer Service 任务上下文，
 * 这里直接操作状态机，需要加锁。由于操作较轻量
 * (互斥锁 + WS2812 RMT + BLE notify)，可接受。
 * 如果栈不够，应增大 CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH。
 */
static void pre_alarm_timer_cb(TimerHandle_t timer)
{
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_ctx.state == ALARM_STATE_PRE_ALARM) {
            ESP_LOGW(TAG, "Pre-alarm timeout, escalating to ALARMING");
            enter_state(ALARM_STATE_ALARMING);
        }
        xSemaphoreGive(s_ctx.mutex);
    }
}

/**
 * ACKED 超时 -> 回到 IDLE
 */
static void acked_timer_cb(TimerHandle_t timer)
{
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_ctx.state == ALARM_STATE_ACKED) {
            ESP_LOGI(TAG, "ACKED timeout, returning to IDLE");
            enter_state(ALARM_STATE_IDLE);
        }
        xSemaphoreGive(s_ctx.mutex);
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

void alarm_ack(void)
{
    if (!s_ctx.initialized) {
        return;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire mutex in alarm_ack");
        return;
    }

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
