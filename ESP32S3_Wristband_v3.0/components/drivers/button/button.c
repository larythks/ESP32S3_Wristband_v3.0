/**
 * @file button.c
 * @brief Button driver implementation.
 *
 * This version uses GPIO edge interrupts to wake a dedicated button task.
 * Debounce, short press and long press state transitions are handled in task
 * context so callbacks are never executed in ISR or timer service context.
 */

#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "button";

/* Button GPIO mapping */
#define BUTTON_SW1_GPIO             GPIO_NUM_7
#define BUTTON_SW2_GPIO             GPIO_NUM_6

/* Timing parameters */
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_LONG_PRESS_MS        1000

/* Internal driver task resources */
#define BUTTON_TASK_STACK_DEPTH     4096
#define BUTTON_TASK_PRIORITY        6
#define BUTTON_QUEUE_LEN            16

typedef enum {
    BUTTON_MSG_EDGE = 0,
    BUTTON_MSG_DEBOUNCE_TIMEOUT,
    BUTTON_MSG_LONG_TIMEOUT,
} button_msg_type_t;

typedef struct {
    button_msg_type_t type;
    button_id_t id;
} button_msg_t;

typedef struct {
    gpio_num_t gpio;
    TickType_t press_tick;
    bool is_pressed;
    bool long_press_fired;
    TimerHandle_t debounce_timer;
    TimerHandle_t long_press_timer;
    button_cb_t cb[BUTTON_EVENT_MAX];
} button_state_t;

static button_state_t s_buttons[BUTTON_ID_MAX];
static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_button_task = NULL;
static bool s_isr_service_installed_by_us = false;
static bool s_isr_handler_added[BUTTON_ID_MAX] = {0};
static bool s_initialized = false;

static const gpio_num_t s_button_gpio_map[BUTTON_ID_MAX] = {
    BUTTON_SW1_GPIO,
    BUTTON_SW2_GPIO,
};

static const char *s_long_timer_names[BUTTON_ID_MAX] = {
    "btn_sw1_long",
    "btn_sw2_long",
};

static const char *s_debounce_timer_names[BUTTON_ID_MAX] = {
    "btn_sw1_db",
    "btn_sw2_db",
};

static uint32_t ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(ticks * portTICK_PERIOD_MS);
}

static bool button_read_level(button_id_t id)
{
    if (id >= BUTTON_ID_MAX) {
        return false;
    }
    return gpio_get_level(s_buttons[id].gpio) == 0;
}

static void button_fire_event(button_id_t id, button_event_t event)
{
    if (id < BUTTON_ID_MAX && event < BUTTON_EVENT_MAX && s_buttons[id].cb[event] != NULL) {
        s_buttons[id].cb[event](id, event);
    }
}

static void IRAM_ATTR button_gpio_isr_handler(void *arg)
{
    if (s_event_queue == NULL) {
        return;
    }

    button_msg_t msg = {
        .type = BUTTON_MSG_EDGE,
        .id = (button_id_t)(uintptr_t)arg,
    };
    BaseType_t high_task_woken = pdFALSE;

    (void)xQueueSendFromISR(s_event_queue, &msg, &high_task_woken);
    if (high_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void button_debounce_timer_cb(TimerHandle_t timer)
{
    if (s_event_queue == NULL) {
        return;
    }

    button_msg_t msg = {
        .type = BUTTON_MSG_DEBOUNCE_TIMEOUT,
        .id = (button_id_t)(uintptr_t)pvTimerGetTimerID(timer),
    };

    if (xQueueSend(s_event_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Button queue full, drop debounce timeout");
    }
}

static void button_long_press_timer_cb(TimerHandle_t timer)
{
    if (s_event_queue == NULL) {
        return;
    }

    button_msg_t msg = {
        .type = BUTTON_MSG_LONG_TIMEOUT,
        .id = (button_id_t)(uintptr_t)pvTimerGetTimerID(timer),
    };

    if (xQueueSend(s_event_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Button queue full, drop long-press timeout");
    }
}

static void button_handle_edge(button_id_t id)
{
    button_state_t *btn = &s_buttons[id];
    if (btn->debounce_timer == NULL) {
        return;
    }

    (void)xTimerStop(btn->debounce_timer, 0);
    (void)xTimerStart(btn->debounce_timer, 0);
}

static void button_handle_debounce_timeout(button_id_t id, TickType_t now)
{
    button_state_t *btn = &s_buttons[id];
    bool pressed = button_read_level(id);

    if (pressed == btn->is_pressed) {
        return;
    }

    if (pressed) {
        btn->is_pressed = true;
        btn->press_tick = now;
        btn->long_press_fired = false;

        if (btn->long_press_timer != NULL) {
            (void)xTimerStop(btn->long_press_timer, 0);
            (void)xTimerStart(btn->long_press_timer, 0);
        }
        return;
    }

    btn->is_pressed = false;
    if (btn->long_press_timer != NULL) {
        (void)xTimerStop(btn->long_press_timer, 0);
    }

    if (!btn->long_press_fired && ticks_to_ms(now - btn->press_tick) >= BUTTON_DEBOUNCE_MS) {
        ESP_LOGI(TAG, "Button %d short press", id);
        button_fire_event(id, BUTTON_EVENT_SHORT_PRESS);
    }
}

static void button_handle_long_timeout(button_id_t id, TickType_t now)
{
    button_state_t *btn = &s_buttons[id];

    if (!btn->is_pressed || btn->long_press_fired) {
        return;
    }

    if (ticks_to_ms(now - btn->press_tick) >= BUTTON_LONG_PRESS_MS) {
        btn->long_press_fired = true;
        ESP_LOGI(TAG, "Button %d long press", id);
        button_fire_event(id, BUTTON_EVENT_LONG_PRESS);
    }
}

static void button_task(void *arg)
{
    (void)arg;
    button_msg_t msg;

    while (1) {
        if (xQueueReceive(s_event_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg.id >= BUTTON_ID_MAX) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        if (msg.type == BUTTON_MSG_EDGE) {
            button_handle_edge(msg.id);
        } else if (msg.type == BUTTON_MSG_DEBOUNCE_TIMEOUT) {
            button_handle_debounce_timeout(msg.id, now);
        } else if (msg.type == BUTTON_MSG_LONG_TIMEOUT) {
            button_handle_long_timeout(msg.id, now);
        }
    }
}

static void button_cleanup_resources(void)
{
    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        if (s_isr_handler_added[i]) {
            gpio_isr_handler_remove(s_button_gpio_map[i]);
            s_isr_handler_added[i] = false;
        }
        gpio_set_intr_type(s_button_gpio_map[i], GPIO_INTR_DISABLE);
    }

    if (s_isr_service_installed_by_us) {
        gpio_uninstall_isr_service();
        s_isr_service_installed_by_us = false;
    }

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        if (s_buttons[i].debounce_timer != NULL) {
            (void)xTimerStop(s_buttons[i].debounce_timer, 0);
            (void)xTimerDelete(s_buttons[i].debounce_timer, 0);
            s_buttons[i].debounce_timer = NULL;
        }

        if (s_buttons[i].long_press_timer != NULL) {
            (void)xTimerStop(s_buttons[i].long_press_timer, 0);
            (void)xTimerDelete(s_buttons[i].long_press_timer, 0);
            s_buttons[i].long_press_timer = NULL;
        }
    }

    if (s_button_task != NULL) {
        vTaskDelete(s_button_task);
        s_button_task = NULL;
    }

    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
}

esp_err_t button_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Button driver already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing button driver");
    memset(s_buttons, 0, sizeof(s_buttons));

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        s_buttons[i].gpio = s_button_gpio_map[i];
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_SW1_GPIO) | (1ULL << BUTTON_SW2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_event_queue = xQueueCreate(BUTTON_QUEUE_LEN, sizeof(button_msg_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(button_task, "btn_task", BUTTON_TASK_STACK_DEPTH, NULL,
                    BUTTON_TASK_PRIORITY, &s_button_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        s_buttons[i].debounce_timer = xTimerCreate(
            s_debounce_timer_names[i],
            pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS),
            pdFALSE,
            (void *)(uintptr_t)i,
            button_debounce_timer_cb);

        if (s_buttons[i].debounce_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create debounce timer for button %d", i);
            ret = ESP_ERR_NO_MEM;
            goto fail;
        }

        s_buttons[i].long_press_timer = xTimerCreate(
            s_long_timer_names[i],
            pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS),
            pdFALSE,
            (void *)(uintptr_t)i,
            button_long_press_timer_cb);

        if (s_buttons[i].long_press_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create long press timer for button %d", i);
            ret = ESP_ERR_NO_MEM;
            goto fail;
        }
    }

    ret = gpio_install_isr_service(0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "GPIO ISR service already installed, reusing existing service");
        ret = ESP_OK;
    } else if (ret == ESP_OK) {
        s_isr_service_installed_by_us = true;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        goto fail;
    }

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        ret = gpio_isr_handler_add(s_button_gpio_map[i], button_gpio_isr_handler,
                                   (void *)(uintptr_t)i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for button %d: %s", i, esp_err_to_name(ret));
            goto fail;
        }
        s_isr_handler_added[i] = true;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Button driver initialized (GPIO interrupt + task state machine)");
    return ESP_OK;

fail:
    button_cleanup_resources();
    return ret;
}

void button_register_cb(button_id_t id, button_event_t event, button_cb_t cb)
{
    if (id < BUTTON_ID_MAX && event < BUTTON_EVENT_MAX) {
        s_buttons[id].cb[event] = cb;
    }
}

void button_deinit(void)
{
    button_cleanup_resources();
    memset(s_buttons, 0, sizeof(s_buttons));
    s_initialized = false;
    ESP_LOGI(TAG, "Button driver deinitialized");
}
