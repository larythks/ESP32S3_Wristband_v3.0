/**
 * @file simple_tts.h
 * @brief Simple TTS engine - numeric speech via WAV segment concatenation
 *
 * Speaks numeric values by chaining pre-recorded WAV clips stored in SPIFFS.
 * Requires audio_player to be initialised first (audio_player_init).
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Speak an integer value (0~999) as Chinese digits
 *
 * Examples:
 *   75  -> "七" "十" "五"
 *   120 -> "一" "百" "二" "十"
 *   105 -> "一" "百" "零" "五"
 *   0   -> "零"
 *
 * @param value  Integer in range [0, 999]
 * @return ESP_OK on success
 */
esp_err_t tts_speak_number(int value);

/**
 * @brief Speak heart rate: "当前心率为 XX 次每分钟"
 * @param bpm  Heart rate in beats per minute
 */
esp_err_t tts_speak_heart_rate(uint8_t bpm);

/**
 * @brief Speak SpO2: "当前血氧为 XX 百分之"
 * @param percent  SpO2 percentage
 */
esp_err_t tts_speak_spo2(uint8_t percent);

/**
 * @brief Speak step count: "当前步数为 XX 步"
 * @param steps  Step count (clamped to 999 for speech)
 */
esp_err_t tts_speak_steps(uint32_t steps);

/**
 * @brief Speak temperature: "当前环境温度为 XX 点 X 摄氏度"
 * @param temp  Temperature value (one decimal place spoken)
 */
esp_err_t tts_speak_temperature(float temp);

#ifdef __cplusplus
}
#endif
