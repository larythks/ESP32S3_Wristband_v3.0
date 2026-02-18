/**
 * @file simple_tts.c
 * @brief Simple TTS engine - numeric speech via WAV segment concatenation
 */

#include "simple_tts.h"
#include "audio_player.h"
#include "esp_log.h"

#include <math.h>

static const char *TAG = "simple_tts";

/* ------------------------------------------------------------------ */
/*  WAV file name tables                                              */
/* ------------------------------------------------------------------ */

/** Digit WAV files: num_0 .. num_9 */
static const char *s_num_wav[] = {
    "num_0", "num_1", "num_2", "num_3", "num_4",
    "num_5", "num_6", "num_7", "num_8", "num_9",
};

/* ------------------------------------------------------------------ */
/*  Internal: speak a single digit (0-9)                              */
/* ------------------------------------------------------------------ */
static esp_err_t play_digit(int d)
{
    if (d < 0 || d > 9) {
        return ESP_ERR_INVALID_ARG;
    }
    return audio_play_wav(s_num_wav[d]);
}

/* ------------------------------------------------------------------ */
/*  Public: tts_speak_number  (0 ~ 999)                               */
/* ------------------------------------------------------------------ */
esp_err_t tts_speak_number(int value)
{
    if (value < 0 || value > 999) {
        ESP_LOGE(TAG, "Number out of range: %d", value);
        return ESP_ERR_INVALID_ARG;
    }

    /* Special case: zero */
    if (value == 0) {
        return play_digit(0);
    }

    esp_err_t ret = ESP_OK;

    int hundreds = value / 100;
    int tens     = (value % 100) / 10;
    int ones     = value % 10;

    /* Hundreds digit: e.g. 1 -> "一" "百" */
    if (hundreds > 0) {
        ret = play_digit(hundreds);
        if (ret != ESP_OK) return ret;
        ret = audio_play_wav("num_100");
        if (ret != ESP_OK) return ret;
    }

    /* Tens digit */
    if (tens > 0) {
        /* Normal tens: e.g. 7 -> "七" "十" */
        ret = play_digit(tens);
        if (ret != ESP_OK) return ret;
        ret = audio_play_wav("num_10");
        if (ret != ESP_OK) return ret;
    } else if (hundreds > 0 && ones > 0) {
        /* Zero in tens place with non-zero hundreds and ones: e.g. 105 -> "一百 零 五" */
        ret = play_digit(0);
        if (ret != ESP_OK) return ret;
    }

    /* Ones digit (skip if zero and not the only digit) */
    if (ones > 0) {
        ret = play_digit(ones);
        if (ret != ESP_OK) return ret;
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public: domain-specific speak functions                           */
/* ------------------------------------------------------------------ */

esp_err_t tts_speak_heart_rate(uint8_t bpm)
{
    ESP_LOGI(TAG, "Speaking heart rate: %u bpm", bpm);
    esp_err_t ret;

    /* "当前心率为" */
    ret = audio_play_wav("prefix_hr");
    if (ret != ESP_OK) return ret;

    /* Number */
    ret = tts_speak_number(bpm);
    if (ret != ESP_OK) return ret;

    /* "次每分钟" */
    ret = audio_play_wav("unit_bpm");
    return ret;
}

esp_err_t tts_speak_spo2(uint8_t percent)
{
    ESP_LOGI(TAG, "Speaking SpO2: %u%%", percent);
    esp_err_t ret;

    /* "当前血氧为" */
    ret = audio_play_wav("prefix_spo2");
    if (ret != ESP_OK) return ret;

    /* Number */
    ret = tts_speak_number(percent);
    if (ret != ESP_OK) return ret;

    /* "百分之" */
    ret = audio_play_wav("unit_percent");
    return ret;
}

esp_err_t tts_speak_steps(uint32_t steps)
{
    ESP_LOGI(TAG, "Speaking steps: %lu", (unsigned long)steps);
    esp_err_t ret;

    /* Clamp to 999 for speech */
    int val = (steps > 999) ? 999 : (int)steps;

    /* "当前步数为" */
    ret = audio_play_wav("prefix_steps");
    if (ret != ESP_OK) return ret;

    /* Number */
    ret = tts_speak_number(val);
    if (ret != ESP_OK) return ret;

    /* "步" */
    ret = audio_play_wav("unit_steps");
    return ret;
}

esp_err_t tts_speak_temperature(float temp)
{
    ESP_LOGI(TAG, "Speaking temperature: %.1f", temp);
    esp_err_t ret;

    /* Split into integer and one decimal place */
    int int_part = (int)fabsf(temp);
    int dec_part = ((int)(fabsf(temp) * 10.0f)) % 10;

    /* Clamp integer part to 999 */
    if (int_part > 999) {
        int_part = 999;
    }

    /* "当前环境温度为" */
    ret = audio_play_wav("prefix_temp");
    if (ret != ESP_OK) return ret;

    /* Integer part */
    ret = tts_speak_number(int_part);
    if (ret != ESP_OK) return ret;

    /* "点" */
    ret = audio_play_wav("num_dot");
    if (ret != ESP_OK) return ret;

    /* Decimal digit */
    ret = play_digit(dec_part);
    if (ret != ESP_OK) return ret;

    /* "摄氏度" */
    ret = audio_play_wav("unit_degree");
    return ret;
}
