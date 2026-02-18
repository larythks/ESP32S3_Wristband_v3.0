/**
 * @file audio_player.c
 * @brief Audio player implementation - SPIFFS + I2S + WAV playback
 */

#include "audio_player.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/i2s_common.h"
#include "driver/gpio.h"
#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG = "audio_player";

/* ------------------------------------------------------------------ */
/*  Hardware pin definitions                                          */
/* ------------------------------------------------------------------ */
#define AUDIO_I2S_NUM        I2S_NUM_0
#define AUDIO_BCLK_GPIO      GPIO_NUM_17
#define AUDIO_WS_GPIO        GPIO_NUM_16
#define AUDIO_DOUT_GPIO      GPIO_NUM_18   /* MAX98357A DIN */
#define AUDIO_DIN_GPIO       GPIO_NUM_15   /* INMP441 SD   */

/* ------------------------------------------------------------------ */
/*  WAV header structure (44 bytes, standard PCM)                     */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    char     riff_tag[4];       /* "RIFF" */
    uint32_t file_size;         /* file size - 8 */
    char     wave_tag[4];       /* "WAVE" */
    char     fmt_tag[4];        /* "fmt " */
    uint32_t fmt_size;          /* 16 for PCM */
    uint16_t audio_format;      /* 1 = PCM */
    uint16_t num_channels;      /* 1 = mono, 2 = stereo */
    uint32_t sample_rate;       /* e.g. 16000 */
    uint32_t byte_rate;         /* sample_rate * num_channels * bits/8 */
    uint16_t block_align;       /* num_channels * bits/8 */
    uint16_t bits_per_sample;   /* 16 */
    char     data_tag[4];       /* "data" */
    uint32_t data_size;         /* PCM data byte count */
} wav_header_t;

/* ------------------------------------------------------------------ */
/*  Module-level state                                                */
/* ------------------------------------------------------------------ */
static SemaphoreHandle_t s_i2s_mutex   = NULL;
static i2s_chan_handle_t s_i2s_handle  = NULL;
static volatile bool     s_playing     = false;
static volatile bool     s_stop_req    = false;
static bool              s_spiffs_mounted = false;

/* I2S mode tracking (needed to distinguish TX/RX in release) */
typedef enum { I2S_MODE_NONE, I2S_MODE_TX, I2S_MODE_RX } i2s_mode_t;
static i2s_mode_t s_i2s_mode = I2S_MODE_NONE;

/* TX acquire/release hooks (for pausing mic feed during playback) */
static audio_i2s_hook_t s_pre_tx_hook  = NULL;
static audio_i2s_hook_t s_post_tx_hook = NULL;

/* Playback completion event group */
static EventGroupHandle_t s_play_event = NULL;
#define PLAY_BIT_DONE  (1 << 0)

/* ------------------------------------------------------------------ */
/*  SPIFFS mount                                                      */
/* ------------------------------------------------------------------ */
static esp_err_t audio_spiffs_init(void)
{
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path       = "/spiffs",
        .partition_label = "storage",
        .max_files       = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: total=%u, used=%u", (unsigned)total, (unsigned)used);

    s_spiffs_mounted = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public: audio_player_init                                         */
/* ------------------------------------------------------------------ */
esp_err_t audio_player_init(void)
{
    /* Create mutex (once) */
    if (s_i2s_mutex == NULL) {
        s_i2s_mutex = xSemaphoreCreateMutex();
        if (s_i2s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create I2S mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Create playback event group (once) */
    if (s_play_event == NULL) {
        s_play_event = xEventGroupCreate();
        if (s_play_event == NULL) {
            ESP_LOGE(TAG, "Failed to create play event group");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Mount SPIFFS */
    esp_err_t ret = audio_spiffs_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Audio player initialised");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  I2S acquire / release                                             */
/* ------------------------------------------------------------------ */
esp_err_t audio_i2s_acquire_tx(void)
{
    if (s_i2s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Call pre-hook to let RX holder release I2S (e.g. pause mic feed) */
    if (s_pre_tx_hook) {
        s_pre_tx_hook();
    }

    if (xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "I2S mutex timeout (acquire_tx)");
        return ESP_ERR_TIMEOUT;
    }

    s_i2s_mode = I2S_MODE_TX;

    /* Create TX channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  /* send zeros when idle */

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_i2s_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel TX failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    /* Configure standard mode */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_WS_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_i2s_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode TX failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    ret = i2s_channel_enable(s_i2s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable TX failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    ESP_LOGD(TAG, "I2S TX acquired");
    return ESP_OK;
}

esp_err_t audio_i2s_acquire_rx(void)
{
    if (s_i2s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_i2s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "I2S mutex timeout (acquire_rx)");
        return ESP_ERR_TIMEOUT;
    }

    s_i2s_mode = I2S_MODE_RX;

    /* Create RX channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_NUM, I2S_ROLE_MASTER);

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_i2s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel RX failed: %s", esp_err_to_name(ret));
        s_i2s_mode = I2S_MODE_NONE;
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    /* Configure standard mode for microphone
     * Use 32-bit slot to match INMP441 frame format (BCLK >= 1.024 MHz).
     * Caller must convert 32-bit samples to 16-bit before feeding to ESP-SR. */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = AUDIO_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_i2s_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode RX failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        s_i2s_mode = I2S_MODE_NONE;
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    ret = i2s_channel_enable(s_i2s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable RX failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        s_i2s_mode = I2S_MODE_NONE;
        xSemaphoreGive(s_i2s_mutex);
        return ret;
    }

    /* Mute speaker: drive DOUT pin LOW so MAX98357A receives zeros
     * instead of random noise while I2S clock is active for RX. */
    gpio_set_direction(AUDIO_DOUT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(AUDIO_DOUT_GPIO, 0);

    ESP_LOGD(TAG, "I2S RX acquired (32-bit, DOUT muted)");
    return ESP_OK;
}

esp_err_t audio_i2s_read(void *buf, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    if (s_i2s_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(s_i2s_handle, buf, len, bytes_read, timeout_ms);
}

void audio_i2s_release(void)
{
    i2s_mode_t prev_mode = s_i2s_mode;

    if (s_i2s_handle != NULL) {
        i2s_channel_disable(s_i2s_handle);
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
        ESP_LOGD(TAG, "I2S channel released (was %s)",
                 prev_mode == I2S_MODE_TX ? "TX" : "RX");
    }

    s_i2s_mode = I2S_MODE_NONE;

    /* After TX release, mute speaker to prevent noise during transition */
    if (prev_mode == I2S_MODE_TX) {
        gpio_set_direction(AUDIO_DOUT_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(AUDIO_DOUT_GPIO, 0);
    }

    if (s_i2s_mutex != NULL) {
        xSemaphoreGive(s_i2s_mutex);
    }

    /* Call post-hook after mutex is released (e.g. resume mic feed) */
    if (prev_mode == I2S_MODE_TX && s_post_tx_hook) {
        s_post_tx_hook();
    }
}

/* ------------------------------------------------------------------ */
/*  Public: playback control                                          */
/* ------------------------------------------------------------------ */
bool audio_is_playing(void)
{
    return s_playing;
}

void audio_play_stop(void)
{
    s_stop_req = true;
}

esp_err_t audio_play_wav(const char *filename)
{
    if (filename == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build full path: /spiffs/{filename}.wav */
    char path[64];
    snprintf(path, sizeof(path), "/spiffs/%s.wav", filename);

    /* Open file */
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Read and validate WAV header */
    wav_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        ESP_LOGE(TAG, "Failed to read WAV header: %s", path);
        fclose(fp);
        return ESP_FAIL;
    }

    if (memcmp(hdr.riff_tag, "RIFF", 4) != 0 ||
        memcmp(hdr.wave_tag, "WAVE", 4) != 0 ||
        memcmp(hdr.fmt_tag,  "fmt ", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV header: %s", path);
        fclose(fp);
        return ESP_FAIL;
    }

    if (hdr.audio_format != 1) {
        ESP_LOGE(TAG, "Unsupported WAV format (not PCM): %u", hdr.audio_format);
        fclose(fp);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Playing %s: %luHz %u-bit %uch, data=%lu bytes",
             path,
             (unsigned long)hdr.sample_rate,
             hdr.bits_per_sample,
             hdr.num_channels,
             (unsigned long)hdr.data_size);

    /* Acquire I2S TX */
    esp_err_t ret = audio_i2s_acquire_tx();
    if (ret != ESP_OK) {
        fclose(fp);
        return ret;
    }

    /* If WAV sample rate differs from default 16 kHz, reconfigure clock */
    if (hdr.sample_rate != 16000) {
        i2s_channel_disable(s_i2s_handle);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hdr.sample_rate);
        i2s_channel_reconfig_std_clock(s_i2s_handle, &clk_cfg);
        i2s_channel_enable(s_i2s_handle);
    }

    /* Stream PCM data to I2S */
    s_playing  = true;
    s_stop_req = false;

    uint8_t buf[1024];
    uint32_t bytes_remaining = hdr.data_size;
    size_t bytes_written = 0;

    while (bytes_remaining > 0 && !s_stop_req) {
        size_t to_read = bytes_remaining < sizeof(buf) ? bytes_remaining : sizeof(buf);
        size_t n = fread(buf, 1, to_read, fp);
        if (n == 0) {
            break;  /* EOF or read error */
        }

        ret = i2s_channel_write(s_i2s_handle, buf, n, &bytes_written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed: %s", esp_err_to_name(ret));
            break;
        }

        bytes_remaining -= n;
    }

    s_playing = false;

    /* Signal completion to anyone waiting via audio_wait_done() */
    if (s_play_event) {
        xEventGroupSetBits(s_play_event, PLAY_BIT_DONE);
    }

    /* Cleanup */
    fclose(fp);
    audio_i2s_release();

    if (s_stop_req) {
        ESP_LOGI(TAG, "Playback stopped by request");
        s_stop_req = false;
    } else {
        ESP_LOGI(TAG, "Playback finished: %s", path);
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Hook registration                                                 */
/* ------------------------------------------------------------------ */
void audio_set_i2s_tx_hooks(audio_i2s_hook_t pre_acquire,
                            audio_i2s_hook_t post_release)
{
    s_pre_tx_hook  = pre_acquire;
    s_post_tx_hook = post_release;
    ESP_LOGI(TAG, "I2S TX hooks registered");
}

/* ------------------------------------------------------------------ */
/*  Async playback wrapper                                            */
/* ------------------------------------------------------------------ */
#define ASYNC_PLAY_TASK_STACK  4096
#define ASYNC_PLAY_TASK_PRIO   3

static void async_play_task(void *arg)
{
    const char *filename = (const char *)arg;
    if (filename != NULL) {
        audio_play_wav(filename);
    }
    vTaskDelete(NULL);
}

esp_err_t audio_play_wav_async(const char *filename)
{
    if (filename == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Stop any current playback */
    audio_play_stop();

    /* Pre-set playing state to avoid race in audio_wait_done() */
    s_playing = true;
    if (s_play_event) {
        xEventGroupClearBits(s_play_event, PLAY_BIT_DONE);
    }

    BaseType_t ret = xTaskCreate(async_play_task, "audio_async",
                                 ASYNC_PLAY_TASK_STACK,
                                 (void *)filename,
                                 ASYNC_PLAY_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create async play task");
        s_playing = false;
        if (s_play_event) {
            xEventGroupSetBits(s_play_event, PLAY_BIT_DONE);
        }
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Wait for playback completion                                      */
/* ------------------------------------------------------------------ */
bool audio_wait_done(uint32_t timeout_ms)
{
    if (s_play_event == NULL) {
        return true;
    }

    /* Nothing playing and not pending — return immediately */
    if (!s_playing) {
        return true;
    }

    EventBits_t bits = xEventGroupWaitBits(s_play_event, PLAY_BIT_DONE,
                                           pdTRUE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & PLAY_BIT_DONE) != 0;
}
