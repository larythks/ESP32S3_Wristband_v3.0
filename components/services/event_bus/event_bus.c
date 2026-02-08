/**
 * @file event_bus.c
 * @brief 事件总线实现
 */

#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "event_bus";

// 订阅者结构
typedef struct {
    event_handler_t handler;
    void *user_data;
} subscriber_t;

// 事件总线上下文
typedef struct {
    QueueHandle_t event_queue;
    TaskHandle_t dispatch_task;
    subscriber_t subscribers[EVT_TYPE_MAX][EVENT_BUS_MAX_SUBSCRIBERS];
    SemaphoreHandle_t mutex;
    bool initialized;
} event_bus_ctx_t;

static event_bus_ctx_t s_ctx = {0};

// 事件分发任务栈大小
#define EVENT_DISPATCH_TASK_STACK   4096
#define EVENT_DISPATCH_TASK_PRIO    5

/**
 * @brief 事件分发任务
 */
static void event_dispatch_task(void *arg)
{
    event_t event;

    ESP_LOGI(TAG, "Event dispatch task started");

    while (1) {
        if (xQueueReceive(s_ctx.event_queue, &event, portMAX_DELAY) == pdTRUE) {
            // 获取互斥锁
            if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 遍历该事件类型的所有订阅者
                for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
                    subscriber_t *sub = &s_ctx.subscribers[event.type][i];
                    if (sub->handler != NULL) {
                        sub->handler(&event, sub->user_data);
                    }
                }
                xSemaphoreGive(s_ctx.mutex);
            }
        }
    }
}

/**
 * @brief 初始化事件总线
 */
esp_err_t event_bus_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Event bus already initialized");
        return ESP_OK;
    }

    // 创建事件队列
    s_ctx.event_queue = xQueueCreate(EVENT_BUS_QUEUE_DEPTH, sizeof(event_t));
    if (s_ctx.event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    // 创建互斥锁
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vQueueDelete(s_ctx.event_queue);
        return ESP_ERR_NO_MEM;
    }

    // 清空订阅者列表
    memset(s_ctx.subscribers, 0, sizeof(s_ctx.subscribers));

    // 创建事件分发任务
    BaseType_t ret = xTaskCreate(
        event_dispatch_task,
        "evt_dispatch",
        EVENT_DISPATCH_TASK_STACK,
        NULL,
        EVENT_DISPATCH_TASK_PRIO,
        &s_ctx.dispatch_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dispatch task");
        vSemaphoreDelete(s_ctx.mutex);
        vQueueDelete(s_ctx.event_queue);
        return ESP_ERR_NO_MEM;
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Event bus initialized");
    return ESP_OK;
}

/**
 * @brief 发布事件
 */
esp_err_t event_publish(event_type_t type, const event_data_t *data)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (type >= EVT_TYPE_MAX) {
        ESP_LOGE(TAG, "Invalid event type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    event_t event = {
        .type = type
    };

    if (data != NULL) {
        memcpy(&event.data, data, sizeof(event_data_t));
    } else {
        memset(&event.data, 0, sizeof(event_data_t));
    }

    // 发送到队列
    BaseType_t ret = xQueueSend(s_ctx.event_queue, &event, pdMS_TO_TICKS(10));
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, event dropped");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/**
 * @brief 订阅事件
 */
esp_err_t event_subscribe(event_type_t type, event_handler_t handler, void *user_data)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (type >= EVT_TYPE_MAX || handler == NULL) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NO_MEM;

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 查找空闲槽位
        for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
            if (s_ctx.subscribers[type][i].handler == NULL) {
                s_ctx.subscribers[type][i].handler = handler;
                s_ctx.subscribers[type][i].user_data = user_data;
                ret = ESP_OK;
                ESP_LOGI(TAG, "Subscribed to event %d", type);
                break;
            }
        }
        xSemaphoreGive(s_ctx.mutex);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No free slot for event %d", type);
    }

    return ret;
}

/**
 * @brief 取消订阅事件
 */
esp_err_t event_unsubscribe(event_type_t type, event_handler_t handler)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Event bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (type >= EVT_TYPE_MAX || handler == NULL) {
        ESP_LOGE(TAG, "Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
            if (s_ctx.subscribers[type][i].handler == handler) {
                s_ctx.subscribers[type][i].handler = NULL;
                s_ctx.subscribers[type][i].user_data = NULL;
                ret = ESP_OK;
                ESP_LOGI(TAG, "Unsubscribed from event %d", type);
                break;
            }
        }
        xSemaphoreGive(s_ctx.mutex);
    }

    return ret;
}
