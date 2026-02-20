/**
 * @file audio_player.h
 * @brief Audio player module - SPIFFS WAV playback via I2S (MAX98357A)
 *
 * Provides SPIFFS mount, I2S TX/RX resource mutex, and WAV file playback.
 * Speaker (MAX98357A) and microphone (INMP441) share GPIO17(BCLK) and
 * GPIO16(WS), so I2S channels are created/destroyed on demand via the
 * acquire/release API to avoid pin conflicts.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the audio player subsystem
 *
 * Mounts the SPIFFS partition ("storage") at /spiffs and creates the
 * internal FreeRTOS mutex.  Does NOT initialize I2S — that is done
 * lazily via audio_i2s_acquire_tx() / audio_i2s_acquire_rx().
 *
 * @return ESP_OK on success
 */
esp_err_t audio_player_init(void);

/**
 * @brief Play a WAV file from SPIFFS
 *
 * Opens /spiffs/{filename}.wav, parses the 44-byte WAV header, acquires
 * the I2S TX channel, and streams PCM data until EOF or audio_play_stop()
 * is called.  This is a **blocking** call — it returns only after playback
 * finishes or is stopped.
 *
 * @param filename  Base name without path or extension (e.g. "wakeup_ok")
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file missing
 */
esp_err_t audio_play_wav(const char *filename);

/**
 * @brief Stop the current playback (if any)
 *
 * Safe to call from any task.  The blocking audio_play_wav() will return
 * shortly after this is called.
 */
void audio_play_stop(void);

/**
 * @brief Query whether audio is currently playing
 * @return true if audio_play_wav() is in progress
 */
bool audio_is_playing(void);

/**
 * @brief I2S TX hook callback type
 *
 * Used for pre-acquire (pause mic feed) and post-release (resume mic feed).
 */
typedef void (*audio_i2s_hook_t)(void);

/**
 * @brief Register hooks for I2S TX acquire/release
 *
 * Pre-hook is called before acquiring TX (to let RX holder release I2S).
 * Post-hook is called after TX is released (to let RX holder re-acquire).
 * Hooks support reference counting — safe to nest with manual calls.
 *
 * @param pre_acquire  Called before TX mutex take (may be NULL)
 * @param post_release Called after TX mutex give (may be NULL)
 */
void audio_set_i2s_tx_hooks(audio_i2s_hook_t pre_acquire,
                            audio_i2s_hook_t post_release);

/**
 * @brief Play a WAV file asynchronously (non-blocking)
 *
 * Creates an internal task that calls audio_play_wav().
 * Sets s_playing=true BEFORE the task starts to avoid race conditions.
 * Use audio_wait_done() to wait for completion.
 *
 * @param filename  Base name without path or extension (e.g. "alarm_help")
 * @return ESP_OK on success, ESP_ERR_NO_MEM if task creation fails
 */
esp_err_t audio_play_wav_async(const char *filename);

/**
 * @brief Wait for the current playback to finish
 *
 * Blocks until playback ends or timeout.  Works with both sync and async
 * playback.
 *
 * @param timeout_ms  Maximum wait in milliseconds
 * @return true if playback finished, false on timeout
 */
bool audio_wait_done(uint32_t timeout_ms);

/**
 * @brief Acquire the I2S bus for TX (speaker) use
 *
 * Creates an I2S TX channel on I2S_NUM_0, configures standard-mode
 * Philips format (16 kHz, 16-bit, mono), and enables it.
 * Caller must call audio_i2s_release() when done.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if mutex not available
 */
esp_err_t audio_i2s_acquire_tx(void);

/**
 * @brief Acquire the I2S bus for RX (microphone) use
 *
 * Creates an I2S RX channel on I2S_NUM_0, configures standard-mode
 * Philips format (16 kHz, 16-bit, mono), and enables it.
 * Caller must call audio_i2s_release() when done.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if mutex not available
 */
esp_err_t audio_i2s_acquire_rx(void);

/**
 * @brief Read data from the I2S RX channel (microphone)
 *
 * Must be called between audio_i2s_acquire_rx() and audio_i2s_release().
 *
 * @param buf          Destination buffer for PCM data
 * @param len          Number of bytes to read
 * @param bytes_read   [out] Actual bytes read
 * @param timeout_ms   Read timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no RX channel active
 */
esp_err_t audio_i2s_read(void *buf, size_t len, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Release the I2S bus
 *
 * Disables and deletes the current I2S channel, then releases the mutex
 * so the other direction (TX or RX) can use the bus.
 */
void audio_i2s_release(void);

#ifdef __cplusplus
}
#endif
