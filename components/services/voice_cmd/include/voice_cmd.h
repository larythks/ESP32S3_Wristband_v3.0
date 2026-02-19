/**
 * @file voice_cmd.h
 * @brief Voice command module - ESP-SR wake word + command recognition
 *
 * Uses INMP441 microphone via I2S RX (shared bus with speaker).
 * Integrates ESP-SR WakeNet (wake word) and MultiNet (command recognition).
 *
 * Workflow:
 *   1. voice_cmd_init() - prepare internal state
 *   2. voice_cmd_start() - launch recognition task
 *   3. Recognition runs continuously: wake word -> command -> callback
 *   4. During audio playback, recognition is paused (I2S mutex)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Voice command IDs
 */
typedef enum {
    VOICE_CMD_NONE = 0,
    VOICE_CMD_HELP,          // "救命" - trigger manual alarm
    VOICE_CMD_QUERY_HR,      // "查询心率"
    VOICE_CMD_QUERY_STEPS,   // "查询步数"
    VOICE_CMD_QUERY_TEMP,    // "查询温度"
    VOICE_CMD_CALL_FAMILY,   // "呼叫家人"
} voice_cmd_id_t;

/**
 * @brief Voice command callback function type
 * @param cmd_id  Recognized command ID
 */
typedef void (*voice_cmd_cb_t)(voice_cmd_id_t cmd_id);

/**
 * @brief Initialize voice command module
 *
 * Prepares internal state.  Does not start recognition.
 * Must be called after audio_player_init().
 *
 * @return ESP_OK on success
 */
esp_err_t voice_cmd_init(void);

/**
 * @brief Start voice recognition task
 *
 * Creates a FreeRTOS task that continuously listens for wake word
 * and speech commands via INMP441 microphone.
 *
 * @return ESP_OK on success
 */
esp_err_t voice_cmd_start(void);

/**
 * @brief Stop voice recognition task
 * @return ESP_OK on success
 */
esp_err_t voice_cmd_stop(void);

/**
 * @brief Register a callback for voice command events
 * @param cb  Callback function
 */
void voice_cmd_register_cb(voice_cmd_cb_t cb);

#ifdef __cplusplus
}
#endif
