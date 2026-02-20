/**
 * @file ws2812.c
 * @brief WS2812 RGB LED driver implementation
 *
 * Drives a single WS2812 LED on GPIO48 using the ESP-IDF 5.x RMT TX API.
 * Blink is implemented via a dedicated FreeRTOS task.
 */

#include "ws2812.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws2812";

/* WS2812 GPIO and RMT configuration */
#define WS2812_GPIO_NUM         GPIO_NUM_48
#define WS2812_RMT_RES_HZ      (10 * 1000 * 1000)  /* 10 MHz => 100ns per tick */

/*
 * WS2812 timing (in RMT ticks at 10 MHz / 100ns resolution):
 *   T0H = 0.35us => 3.5 ticks => ~4 ticks
 *   T0L = 0.80us => 8.0 ticks => ~8 ticks
 *   T1H = 0.70us => 7.0 ticks => ~7 ticks
 *   T1L = 0.60us => 6.0 ticks => ~6 ticks
 *   Reset >= 50us => handled by EOT idle level
 */
#define WS2812_T0H_TICKS   4
#define WS2812_T0L_TICKS   8
#define WS2812_T1H_TICKS   7
#define WS2812_T1L_TICKS   6
#define WS2812_BLINK_TASK_STACK 2048
#define WS2812_BLINK_TASK_PRIO  4
#define WS2812_MUTEX_WAIT_MS    5

/* Driver context */
typedef struct {
    rmt_channel_handle_t tx_channel;
    rmt_encoder_handle_t encoder;
    TaskHandle_t blink_task;
    SemaphoreHandle_t mutex;
    uint8_t blink_r;
    uint8_t blink_g;
    uint8_t blink_b;
    uint32_t blink_interval_ms;
    bool blink_enabled;
    bool blink_on;         /* current blink phase: on or off */
    bool initialized;
} ws2812_ctx_t;

static ws2812_ctx_t s_ctx;

/**
 * @brief Send GRB pixel data to the WS2812 via RMT
 */
static esp_err_t ws2812_send_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* WS2812 expects GRB byte order */
    uint8_t grb[3] = { g, r, b };

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags.eot_level = 0,  /* low level after transmission => reset signal */
    };

    esp_err_t ret = rmt_transmit(s_ctx.tx_channel, s_ctx.encoder,
                                 grb, sizeof(grb), &tx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_tx_wait_all_done(s_ctx.tx_channel, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_wait_all_done failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* Blink worker task: keeps timer-service task free from LED transmit workload. */
static void ws2812_blink_task(void *arg)
{
    (void)arg;

    while (1) {
        bool enabled = false;
        uint32_t interval_ms = 100;

        if (xSemaphoreTake(s_ctx.mutex, portMAX_DELAY) == pdTRUE) {
            enabled = s_ctx.blink_enabled;
            interval_ms = s_ctx.blink_interval_ms;
            xSemaphoreGive(s_ctx.mutex);
        }

        if (!enabled) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        TickType_t wait_ticks = pdMS_TO_TICKS(interval_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        if (ulTaskNotifyTake(pdTRUE, wait_ticks) > 0) {
            continue;
        }

        bool blink_on;
        uint8_t r, g, b;
        if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
            continue;
        }

        if (!s_ctx.blink_enabled) {
            xSemaphoreGive(s_ctx.mutex);
            continue;
        }

        s_ctx.blink_on = !s_ctx.blink_on;
        blink_on = s_ctx.blink_on;
        r = s_ctx.blink_r;
        g = s_ctx.blink_g;
        b = s_ctx.blink_b;
        xSemaphoreGive(s_ctx.mutex);

        if (blink_on) {
            ws2812_send_rgb(r, g, b);
        } else {
            ws2812_send_rgb(0, 0, 0);
        }
    }
}

esp_err_t ws2812_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "WS2812 already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WS2812 driver on GPIO %d", WS2812_GPIO_NUM);
    memset(&s_ctx, 0, sizeof(s_ctx));

    /* 1. Create RMT TX channel */
    rmt_tx_channel_config_t tx_chan_cfg = {
        .gpio_num = WS2812_GPIO_NUM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = 0,
        .flags.with_dma = 0,
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_chan_cfg, &s_ctx.tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Create bytes encoder with WS2812 timing */
    rmt_bytes_encoder_config_t encoder_cfg = {
        .bit0 = {
            .duration0 = WS2812_T0H_TICKS,
            .level0 = 1,
            .duration1 = WS2812_T0L_TICKS,
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = WS2812_T1H_TICKS,
            .level0 = 1,
            .duration1 = WS2812_T1L_TICKS,
            .level1 = 0,
        },
        .flags.msb_first = 1,
    };

    ret = rmt_new_bytes_encoder(&encoder_cfg, &s_ctx.encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder failed: %s", esp_err_to_name(ret));
        rmt_del_channel(s_ctx.tx_channel);
        s_ctx.tx_channel = NULL;
        return ret;
    }

    /* 3. Enable the RMT channel */
    ret = rmt_enable(s_ctx.tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_ctx.encoder);
        rmt_del_channel(s_ctx.tx_channel);
        s_ctx.encoder = NULL;
        s_ctx.tx_channel = NULL;
        return ret;
    }

    /* 4. Create mutex for blink state updates */
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create ws2812 mutex");
        rmt_disable(s_ctx.tx_channel);
        rmt_del_encoder(s_ctx.encoder);
        rmt_del_channel(s_ctx.tx_channel);
        s_ctx.encoder = NULL;
        s_ctx.tx_channel = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.blink_interval_ms = 500;
    s_ctx.blink_enabled = false;
    s_ctx.blink_on = false;

    /* 5. Create blink task (blocks while blink is disabled) */
    BaseType_t task_ret = xTaskCreate(
        ws2812_blink_task,
        "ws2812_blink",
        WS2812_BLINK_TASK_STACK,
        NULL,
        WS2812_BLINK_TASK_PRIO,
        &s_ctx.blink_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ws2812 blink task");
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        rmt_disable(s_ctx.tx_channel);
        rmt_del_encoder(s_ctx.encoder);
        rmt_del_channel(s_ctx.tx_channel);
        s_ctx.encoder = NULL;
        s_ctx.tx_channel = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* Start with LED off */
    ws2812_send_rgb(0, 0, 0);

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "WS2812 driver initialized");
    return ESP_OK;
}

esp_err_t ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "WS2812 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return ws2812_send_rgb(r, g, b);
}

esp_err_t ws2812_off(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "WS2812 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return ws2812_send_rgb(0, 0, 0);
}

esp_err_t ws2812_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t interval_ms)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "WS2812 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (interval_ms == 0) {
        ESP_LOGE(TAG, "Blink interval must be > 0");
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(WS2812_MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to lock ws2812 mutex in blink_start");
        return ESP_ERR_TIMEOUT;
    }

    /* Set blink parameters */
    s_ctx.blink_r = r;
    s_ctx.blink_g = g;
    s_ctx.blink_b = b;
    s_ctx.blink_interval_ms = interval_ms;
    s_ctx.blink_enabled = true;
    s_ctx.blink_on = true;
    xSemaphoreGive(s_ctx.mutex);

    /* Turn on immediately */
    ws2812_send_rgb(r, g, b);

    /* Wake worker task so new config takes effect immediately */
    xTaskNotifyGive(s_ctx.blink_task);

    ESP_LOGD(TAG, "Blink started: R=%u G=%u B=%u interval=%lu ms",
             r, g, b, (unsigned long)interval_ms);
    return ESP_OK;
}

esp_err_t ws2812_blink_stop(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "WS2812 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(WS2812_MUTEX_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to lock ws2812 mutex in blink_stop");
        return ESP_ERR_TIMEOUT;
    }
    s_ctx.blink_enabled = false;
    s_ctx.blink_on = false;
    xSemaphoreGive(s_ctx.mutex);

    /* Turn off LED */
    ws2812_send_rgb(0, 0, 0);
    xTaskNotifyGive(s_ctx.blink_task);

    ESP_LOGD(TAG, "Blink stopped");
    return ESP_OK;
}
