/**
 * @file button.c
 * @brief 按键驱动实现
 *
 * 使用 GPIO 中断 + FreeRTOS 定时器实现消抖和长按检测
 */

#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "button";

/* 按键 GPIO 定义 */
#define BUTTON_SW1_GPIO     GPIO_NUM_7
#define BUTTON_SW2_GPIO     GPIO_NUM_6

/* 按键参数 */
#define BUTTON_DEBOUNCE_MS      50      // 消抖时间
#define BUTTON_LONG_PRESS_MS    1000    // 长按阈值
#define BUTTON_SCAN_PERIOD_MS   20      // 扫描周期

/* 按键状态 */
typedef struct {
    gpio_num_t gpio;
    uint32_t press_tick;        // 按下时的 tick
    bool is_pressed;            // 当前是否按下
    bool long_press_fired;      // 长按事件是否已触发
    button_cb_t cb[BUTTON_EVENT_MAX];  // 回调函数
} button_state_t;

static button_state_t s_buttons[BUTTON_ID_MAX];
static TimerHandle_t s_scan_timer = NULL;

/**
 * @brief 读取按键状态（低电平有效）
 */
static bool button_read_level(button_id_t id)
{
    if (id >= BUTTON_ID_MAX) {
        return false;
    }
    return gpio_get_level(s_buttons[id].gpio) == 0;
}

/**
 * @brief 触发按键回调
 */
static void button_fire_event(button_id_t id, button_event_t event)
{
    if (id < BUTTON_ID_MAX && s_buttons[id].cb[event] != NULL) {
        s_buttons[id].cb[event](id, event);
    }
}

/**
 * @brief 按键扫描定时器回调
 */
static void button_scan_timer_cb(TimerHandle_t timer)
{
    uint32_t now = xTaskGetTickCount();

    for (int i = 0; i < BUTTON_ID_MAX; i++) {
        bool pressed = button_read_level(i);
        button_state_t *btn = &s_buttons[i];

        if (pressed && !btn->is_pressed) {
            /* 按键刚按下 */
            btn->is_pressed = true;
            btn->press_tick = now;
            btn->long_press_fired = false;
        }
        else if (pressed && btn->is_pressed) {
            /* 按键持续按下，检查长按 */
            uint32_t duration = (now - btn->press_tick) * portTICK_PERIOD_MS;
            if (duration >= BUTTON_LONG_PRESS_MS && !btn->long_press_fired) {
                btn->long_press_fired = true;
                ESP_LOGI(TAG, "Button %d long press", i);
                button_fire_event(i, BUTTON_EVENT_LONG_PRESS);
            }
        }
        else if (!pressed && btn->is_pressed) {
            /* 按键释放 */
            uint32_t duration = (now - btn->press_tick) * portTICK_PERIOD_MS;
            btn->is_pressed = false;

            /* 如果未触发长按且按下时间超过消抖时间，则为短按 */
            if (!btn->long_press_fired && duration >= BUTTON_DEBOUNCE_MS) {
                ESP_LOGI(TAG, "Button %d short press", i);
                button_fire_event(i, BUTTON_EVENT_SHORT_PRESS);
            }
        }
    }
}

/**
 * @brief 初始化按键驱动
 */
esp_err_t button_init(void)
{
    ESP_LOGI(TAG, "Initializing button driver");

    /* 初始化按键状态 */
    memset(s_buttons, 0, sizeof(s_buttons));
    s_buttons[BUTTON_ID_SW1].gpio = BUTTON_SW1_GPIO;
    s_buttons[BUTTON_ID_SW2].gpio = BUTTON_SW2_GPIO;

    /* 配置 GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_SW1_GPIO) | (1ULL << BUTTON_SW2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 创建扫描定时器 */
    s_scan_timer = xTimerCreate(
        "btn_scan",
        pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS),
        pdTRUE,  /* 自动重载 */
        NULL,
        button_scan_timer_cb
    );
    if (s_scan_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create scan timer");
        return ESP_ERR_NO_MEM;
    }

    /* 启动定时器 */
    if (xTimerStart(s_scan_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start scan timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Button driver initialized");
    return ESP_OK;
}

/**
 * @brief 注册按键事件回调
 */
void button_register_cb(button_id_t id, button_event_t event, button_cb_t cb)
{
    if (id < BUTTON_ID_MAX && event < BUTTON_EVENT_MAX) {
        s_buttons[id].cb[event] = cb;
    }
}

/**
 * @brief 反初始化按键驱动
 */
void button_deinit(void)
{
    if (s_scan_timer != NULL) {
        xTimerStop(s_scan_timer, 0);
        xTimerDelete(s_scan_timer, 0);
        s_scan_timer = NULL;
    }
    ESP_LOGI(TAG, "Button driver deinitialized");
}
