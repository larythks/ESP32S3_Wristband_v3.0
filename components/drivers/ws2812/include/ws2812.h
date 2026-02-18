/**
 * @file ws2812.h
 * @brief WS2812 RGB LED driver header
 *
 * Uses ESP32-S3 RMT peripheral to drive a single WS2812 LED on GPIO48.
 * Provides color setting and blink functionality via a FreeRTOS task.
 */

#ifndef __WS2812_H__
#define __WS2812_H__

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WS2812 driver (RMT TX channel + bytes encoder)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws2812_init(void);

/**
 * @brief Set WS2812 LED color
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off WS2812 LED (set color to 0,0,0)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws2812_off(void);

/**
 * @brief Start blinking with specified color and interval
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @param interval_ms Blink interval in milliseconds (half-period)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws2812_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t interval_ms);

/**
 * @brief Stop blinking and turn off LED
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws2812_blink_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_H__ */
