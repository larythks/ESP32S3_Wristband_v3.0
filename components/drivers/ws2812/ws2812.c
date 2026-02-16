/**
 * @file ws2812.c
 * @brief WS2812 RGB LED driver implementation
 *
 * Drives a single WS2812 LED on GPIO48 using the ESP-IDF 5.x RMT TX API.
 * Blink is implemented via a FreeRTOS software timer that toggles LED state.
 */

#include "ws2812.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
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

/* Driver context */
typedef struct {
    rmt_channel_handle_t tx_channel;
    rmt_encoder_handle_t encoder;
    TimerHandle_t blink_timer;
    uint8_t blink_r;
    uint8_t blink_g;
    uint8_t blink_b;
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

/**
 * @brief FreeRTOS timer callback for blink
 *
 * Lightweight: only toggles a boolean and calls rmt_transmit (no I2C, no heavy ops).
 */
static void ws2812_blink_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    s_ctx.blink_on = !s_ctx.blink_on;

    if (s_ctx.blink_on) {
        ws2812_send_rgb(s_ctx.blink_r, s_ctx.blink_g, s_ctx.blink_b);
    } else {
        ws2812_send_rgb(0, 0, 0);
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

    /* 4. Create blink timer (initially stopped) */
    s_ctx.blink_timer = xTimerCreate(
        "ws2812_blink",
        pdMS_TO_TICKS(500),   /* default period, changed on blink_start */
        pdTRUE,               /* auto-reload */
        NULL,
        ws2812_blink_timer_cb);

    if (s_ctx.blink_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create blink timer");
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

    /* Stop any existing blink first */
    xTimerStop(s_ctx.blink_timer, 0);

    /* Set blink parameters */
    s_ctx.blink_r = r;
    s_ctx.blink_g = g;
    s_ctx.blink_b = b;
    s_ctx.blink_on = true;

    /* Turn on immediately */
    ws2812_send_rgb(r, g, b);

    /* Update timer period and start */
    xTimerChangePeriod(s_ctx.blink_timer, pdMS_TO_TICKS(interval_ms), 0);
    /* xTimerChangePeriod also starts the timer */

    ESP_LOGI(TAG, "Blink started: R=%u G=%u B=%u interval=%lu ms",
             r, g, b, (unsigned long)interval_ms);
    return ESP_OK;
}

esp_err_t ws2812_blink_stop(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "WS2812 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xTimerStop(s_ctx.blink_timer, 0);
    s_ctx.blink_on = false;

    /* Turn off LED */
    ws2812_send_rgb(0, 0, 0);

    ESP_LOGI(TAG, "Blink stopped");
    return ESP_OK;
}
