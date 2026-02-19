/**
 * @file voice_cmd.c
 * @brief Voice command module - ESP-SR integration
 *
 * Phase 3 (3.3-C): Full integration with command response
 *
 * Architecture:
 *   feed_task  -> reads INMP441 mic via I2S, feeds AFE
 *   detect_task -> fetches from AFE, runs WakeNet/MultiNet, handles responses
 *
 * Single mic, no AEC, no reference channel.
 * I2S resource sharing: feed_task pauses during audio playback.
 */

#include "voice_cmd.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "audio_player.h"
#include "simple_tts.h"
#include "event_bus.h"
#include "alarm_manager.h"
#include "health_monitor.h"
#include "pedometer.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

static const char *TAG = "voice_cmd";

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */
static volatile bool   s_initialized = false;
static volatile bool   s_running     = false;
static TaskHandle_t    s_feed_task   = NULL;
static TaskHandle_t    s_detect_task = NULL;
static voice_cmd_cb_t  s_user_cb    = NULL;

/* Feed task pause/resume synchronization */
#define FEED_BIT_PAUSE   (1 << 0)
#define FEED_BIT_PAUSED  (1 << 1)
#define FEED_BIT_RESUME  (1 << 2)
static EventGroupHandle_t s_feed_event = NULL;

/* Reference count for pause/resume (supports nested hook + manual calls) */
static volatile int s_pause_count = 0;

/* ESP-SR handles */
static const esp_afe_sr_iface_t *s_afe_iface = NULL;
static esp_afe_sr_data_t        *s_afe_data  = NULL;
static const esp_mn_iface_t     *s_mn_iface  = NULL;
static model_iface_data_t       *s_mn_data   = NULL;
static srmodel_list_t            *s_models   = NULL;

/* MultiNet listening timeout (ms) */
#define MN_DETECT_TIMEOUT_MS  6000

/* ------------------------------------------------------------------ */
/*  Command mapping                                                    */
/* ------------------------------------------------------------------ */
static voice_cmd_id_t map_command_id(int mn_cmd_id)
{
    switch (mn_cmd_id) {
    case 0:  return VOICE_CMD_HELP;          /* "jiu ming" */
    case 1:  return VOICE_CMD_QUERY_HR;      /* "cha xun xin lv" */
    case 2:  return VOICE_CMD_QUERY_STEPS;   /* "cha xun bu shu" */
    case 3:  return VOICE_CMD_QUERY_TEMP;    /* "cha xun wen du" */
    case 4:  return VOICE_CMD_CALL_FAMILY;   /* "hu jiao jia ren" */
    default: return VOICE_CMD_NONE;
    }
}

/* ------------------------------------------------------------------ */
/*  Speech commands registration                                       */
/* ------------------------------------------------------------------ */
static esp_err_t register_speech_commands(void)
{
    esp_err_t ret;

    ret = esp_mn_commands_alloc(s_mn_iface, s_mn_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to alloc speech commands: %s", esp_err_to_name(ret));
        return ret;
    }

    /* command_id, Pinyin string */
    esp_mn_commands_add(0, "jiu ming");
    esp_mn_commands_add(1, "cha xun xin lv");
    esp_mn_commands_add(2, "cha xun bu shu");
    esp_mn_commands_add(3, "cha xun wen du");
    esp_mn_commands_add(4, "hu jiao jia ren");

    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        ESP_LOGW(TAG, "%d speech commands failed to parse", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGW(TAG, "  bad phrase: id=%d str='%s'",
                     err->phrases[i]->command_id,
                     err->phrases[i]->string);
        }
    }

    esp_mn_commands_print();
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  I2S pause/resume helpers for audio playback                        */
/* ------------------------------------------------------------------ */
static void pause_feed_task(void)
{
    if (s_feed_event == NULL || s_feed_task == NULL) {
        return;
    }

    s_pause_count++;
    if (s_pause_count > 1) {
        /* Already paused by a previous caller — skip actual pause */
        return;
    }

    /* Signal feed task to pause */
    xEventGroupSetBits(s_feed_event, FEED_BIT_PAUSE);
    /* Wait for feed task to acknowledge pause (release I2S) */
    xEventGroupWaitBits(s_feed_event, FEED_BIT_PAUSED,
                        pdTRUE, pdTRUE, pdMS_TO_TICKS(3000));
}

static void resume_feed_task(void)
{
    if (s_feed_event == NULL || s_feed_task == NULL) {
        return;
    }

    if (s_pause_count <= 0) {
        return;
    }

    s_pause_count--;
    if (s_pause_count > 0) {
        /* Still held by another caller — skip actual resume */
        return;
    }

    /* Signal feed task to resume */
    xEventGroupSetBits(s_feed_event, FEED_BIT_RESUME);
}

/* ------------------------------------------------------------------ */
/*  Command response: play audio feedback                              */
/* ------------------------------------------------------------------ */

static void handle_command_response(voice_cmd_id_t cmd)
{
    /* Pause feed task so we can use I2S for speaker.
     * This is ref-counted — hooks in audio_i2s_acquire_tx will also call
     * pause, but as a nested (noop) call thanks to s_pause_count. */
    pause_feed_task();

    switch (cmd) {
    case VOICE_CMD_HELP:
        ESP_LOGI(TAG, "Response: triggering manual alarm");
        alarm_trigger(ALERT_TYPE_MANUAL, NULL);
        /* alarm_trigger starts async audio (alarm_help.wav).
         * Wait for it to finish before resuming feed task. */
        audio_wait_done(10000);
        break;

    case VOICE_CMD_QUERY_HR: {
        health_status_t status = health_get_status();
        ESP_LOGI(TAG, "Response: heart rate = %u bpm", status.heart_rate);
        tts_speak_heart_rate(status.heart_rate);
        tts_speak_spo2(status.spo2);
        break;
    }

    case VOICE_CMD_QUERY_STEPS: {
        uint32_t steps = pedometer_get_steps();
        ESP_LOGI(TAG, "Response: steps = %lu", (unsigned long)steps);
        tts_speak_steps(steps);
        break;
    }

    case VOICE_CMD_QUERY_TEMP: {
        health_status_t status = health_get_status();
        ESP_LOGI(TAG, "Response: temperature = %.1f C", status.temperature);
        tts_speak_temperature(status.temperature);
        break;
    }

    case VOICE_CMD_CALL_FAMILY:
        ESP_LOGI(TAG, "Response: calling family");
        alarm_trigger(ALERT_TYPE_CALL_FAMILY, NULL);
        /* alarm_trigger starts async audio (call_family.wav).
         * Wait for it to finish — do NOT call audio_play_wav again,
         * as that would compete for I2S TX with the async task. */
        audio_wait_done(10000);
        break;

    default:
        audio_play_wav("cmd_not_recognized");
        break;
    }

    /* Resume feed task to re-acquire I2S RX */
    resume_feed_task();
}

/* ------------------------------------------------------------------ */
/*  Publish voice command event to event bus                           */
/* ------------------------------------------------------------------ */
static void publish_voice_cmd_event(voice_cmd_id_t cmd)
{
    event_data_t evt_data;
    memset(&evt_data, 0, sizeof(evt_data));
    evt_data.voice_cmd.cmd_id = (int32_t)cmd;
    evt_data.voice_cmd.timestamp = get_timestamp_ms();
    event_publish(EVT_VOICE_CMD, &evt_data);
}

/* ------------------------------------------------------------------ */
/*  Feed task: read mic -> feed AFE                                    */
/* ------------------------------------------------------------------ */
static void feed_task(void *arg)
{
    ESP_LOGI(TAG, "Feed task started");

    int chunksize = s_afe_iface->get_feed_chunksize(s_afe_data);
    int total_ch  = s_afe_iface->get_total_channel_num(s_afe_data);
    int num_samples = chunksize * total_ch;

    /* I2S RX is 32-bit and captures interleaved L+R even in MONO mode.
     * Read 2x samples so we can de-interleave and extract left channel only.
     * This gives us exactly num_samples left-channel frames per read. */
    int read_size = num_samples * 2 * sizeof(int32_t);
    int32_t *raw_buf = malloc(read_size);
    if (raw_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate raw feed buffer (%d bytes)", read_size);
        vTaskDelete(NULL);
        return;
    }

    /* 16-bit buffer for AFE feed (converted from 32-bit) */
    int16_t *feed_buf = malloc(num_samples * sizeof(int16_t));
    if (feed_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate feed buffer (%d bytes)",
                 (int)(num_samples * sizeof(int16_t)));
        free(raw_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Feed: chunksize=%d total_ch=%d read_size=%d",
             chunksize, total_ch, read_size);

    /* Acquire I2S RX for microphone input */
    esp_err_t ret = audio_i2s_acquire_rx();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire I2S RX: %s", esp_err_to_name(ret));
        free(raw_buf);
        free(feed_buf);
        vTaskDelete(NULL);
        return;
    }

    uint32_t feed_count = 0;
    uint32_t err_count  = 0;

    while (s_running) {
        /* Check if we need to pause for audio playback */
        EventBits_t bits = xEventGroupGetBits(s_feed_event);
        if (bits & FEED_BIT_PAUSE) {
            /* Release I2S so speaker can use it */
            audio_i2s_release();
            ESP_LOGI(TAG, "Feed paused (I2S released for speaker)");

            /* Notify detect task that we've paused */
            xEventGroupClearBits(s_feed_event, FEED_BIT_PAUSE);
            xEventGroupSetBits(s_feed_event, FEED_BIT_PAUSED);

            /* Wait for resume signal */
            xEventGroupWaitBits(s_feed_event, FEED_BIT_RESUME,
                                pdTRUE, pdTRUE, portMAX_DELAY);

            /* Re-acquire I2S RX */
            ret = audio_i2s_acquire_rx();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to re-acquire I2S RX: %s", esp_err_to_name(ret));
                break;
            }
            ESP_LOGI(TAG, "Feed resumed (I2S RX re-acquired)");

            /* Reset AFE ring buffer after pause */
            s_afe_iface->reset_buffer(s_afe_data);
            continue;
        }

        size_t bytes_read = 0;
        ret = audio_i2s_read(raw_buf, read_size, &bytes_read, 1000);
        if (ret != ESP_OK || bytes_read == 0) {
            err_count++;
            if (err_count == 1 || (err_count % 500) == 0) {
                ESP_LOGW(TAG, "Feed: i2s_read fail #%lu ret=%s bytes=%u",
                         (unsigned long)err_count, esp_err_to_name(ret),
                         (unsigned)bytes_read);
            }
            continue;
        }

        /* De-interleave L+R and convert 32-bit to 16-bit for ESP-SR.
         * I2S captures interleaved [L0,R0,L1,R1,...] even in MONO mode.
         * INMP441 (L/R=GND) outputs on left channel (even indices).
         * Use >> 14 (4x gain) for low-amplitude INMP441 input. */
        int raw_samples = bytes_read / sizeof(int32_t);
        int samples = 0;
        for (int i = 0; i < raw_samples && samples < num_samples; i += 2) {
            int32_t s = raw_buf[i] >> 14;
            if (s > 32767)  s = 32767;
            if (s < -32768) s = -32768;
            feed_buf[samples++] = (int16_t)s;
        }

        s_afe_iface->feed(s_afe_data, feed_buf);

        /* Diagnostic: log mic amplitude every ~3s (300 chunks * 10ms = 3s) */
        feed_count++;
        if ((feed_count % 300) == 0) {
            int16_t max_amp = 0;
            for (int i = 0; i < samples; i++) {
                int16_t v = feed_buf[i] > 0 ? feed_buf[i] : (int16_t)(-feed_buf[i]);
                if (v > max_amp) max_amp = v;
            }
            ESP_LOGI(TAG, "Feed: #%lu max_amp=%d errs=%lu",
                     (unsigned long)feed_count, max_amp, (unsigned long)err_count);
        }
    }

    audio_i2s_release();
    free(raw_buf);
    free(feed_buf);
    ESP_LOGI(TAG, "Feed task exiting");
    s_feed_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Detect task: fetch from AFE -> WakeNet -> MultiNet                 */
/* ------------------------------------------------------------------ */
static void detect_task(void *arg)
{
    ESP_LOGI(TAG, "Detect task started");

    bool mn_listening = false;
    int  mn_chunks    = 0;
    int  mn_max_chunks = 0;

    /* Calculate max chunks for timeout */
    int fetch_chunksize = s_afe_iface->get_fetch_chunksize(s_afe_data);
    int sample_rate     = s_afe_iface->get_samp_rate(s_afe_data);
    if (fetch_chunksize > 0 && sample_rate > 0) {
        float chunk_duration_ms = (float)fetch_chunksize / sample_rate * 1000.0f;
        mn_max_chunks = (int)(MN_DETECT_TIMEOUT_MS / chunk_duration_ms);
    }
    if (mn_max_chunks <= 0) {
        mn_max_chunks = 200;
    }

    ESP_LOGI(TAG, "Detect: fetch_chunksize=%d sample_rate=%d mn_max_chunks=%d",
             fetch_chunksize, sample_rate, mn_max_chunks);

    uint32_t detect_count = 0;
    uint32_t fetch_fail   = 0;

    while (s_running) {
        afe_fetch_result_t *res = s_afe_iface->fetch(s_afe_data);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            fetch_fail++;
            continue;
        }

        /* Diagnostic: log detect task status every ~3s */
        detect_count++;
        if ((detect_count % 300) == 0) {
            ESP_LOGI(TAG, "Detect: #%lu wakeup=%d vad=%d fails=%lu",
                     (unsigned long)detect_count,
                     res->wakeup_state,
                     res->vad_state,
                     (unsigned long)fetch_fail);
        }

        /* ---------- Wake word detection ---------- */
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED ***");

            /* Play wakeup acknowledgment */
            pause_feed_task();
            audio_play_wav("wakeup_ok");
            resume_feed_task();

            /* Disable WakeNet, enable MultiNet listening */
            s_afe_iface->disable_wakenet(s_afe_data);
            mn_listening = true;
            mn_chunks = 0;

            ESP_LOGI(TAG, "Entering command listening mode (timeout=%dms)", MN_DETECT_TIMEOUT_MS);
            continue;
        }

        /* ---------- MultiNet command detection ---------- */
        if (mn_listening) {
            esp_mn_state_t mn_state = s_mn_iface->detect(s_mn_data, res->data);
            mn_chunks++;

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = s_mn_iface->get_results(s_mn_data);

                int cmd_id = mn_result->command_id[0];
                float prob = mn_result->prob[0];
                char *cmd_str = esp_mn_commands_get_string(cmd_id);

                ESP_LOGI(TAG, "*** COMMAND DETECTED: id=%d str='%s' prob=%.3f ***",
                         cmd_id, cmd_str ? cmd_str : "?", prob);

                /* Map to voice_cmd_id_t */
                voice_cmd_id_t vcmd = map_command_id(cmd_id);

                /* Publish event and invoke callback */
                if (vcmd != VOICE_CMD_NONE) {
                    publish_voice_cmd_event(vcmd);
                    if (s_user_cb != NULL) {
                        s_user_cb(vcmd);
                    }
                    /* Execute command response (audio feedback) */
                    handle_command_response(vcmd);
                }

                /* Reset: stop MultiNet, re-enable WakeNet */
                mn_listening = false;
                s_mn_iface->clean(s_mn_data);
                s_afe_iface->enable_wakenet(s_afe_data);
                ESP_LOGI(TAG, "Returning to wake word listening mode");
            }
            else if (mn_state == ESP_MN_STATE_TIMEOUT || mn_chunks >= mn_max_chunks) {
                ESP_LOGI(TAG, "Command listening timed out");

                /* Play "not recognized" feedback */
                pause_feed_task();
                audio_play_wav("cmd_not_recognized");
                resume_feed_task();

                mn_listening = false;
                s_mn_iface->clean(s_mn_data);
                s_afe_iface->enable_wakenet(s_afe_data);
                ESP_LOGI(TAG, "Returning to wake word listening mode");
            }
        }
    }

    ESP_LOGI(TAG, "Detect task exiting");
    s_detect_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
esp_err_t voice_cmd_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing voice command module (ESP-SR)");

    /* Create event group for feed task synchronization */
    s_feed_event = xEventGroupCreate();
    if (s_feed_event == NULL) {
        ESP_LOGE(TAG, "Failed to create feed event group");
        return ESP_ERR_NO_MEM;
    }

    /* Load models from "model" partition */
    s_models = esp_srmodel_init("model");
    if (s_models == NULL) {
        ESP_LOGE(TAG, "Failed to init SR models from 'model' partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Loaded %d SR models", s_models->num);

    /* Find WakeNet and MultiNet model names */
    char *wn_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_CHINESE);

    if (wn_name == NULL) {
        ESP_LOGE(TAG, "No WakeNet model found");
        return ESP_FAIL;
    }
    if (mn_name == NULL) {
        ESP_LOGE(TAG, "No MultiNet (Chinese) model found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WakeNet model: %s", wn_name);
    ESP_LOGI(TAG, "MultiNet model: %s", mn_name);

    /* Configure AFE for single mic, no AEC */
    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    afe_cfg.aec_init = false;
    afe_cfg.se_init  = true;
    afe_cfg.vad_init = true;
    afe_cfg.wakenet_init = true;
    afe_cfg.voice_communication_init = false;
    afe_cfg.wakenet_model_name = wn_name;
    afe_cfg.wakenet_mode = DET_MODE_90;
    afe_cfg.afe_mode = SR_MODE_LOW_COST;
    afe_cfg.afe_perferred_core = 0;
    afe_cfg.afe_perferred_priority = 5;
    afe_cfg.afe_ringbuf_size = 50;
    afe_cfg.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_cfg.agc_mode = AFE_MN_PEAK_AGC_MODE_2;
    afe_cfg.pcm_config.total_ch_num = 1;
    afe_cfg.pcm_config.mic_num = 1;
    afe_cfg.pcm_config.ref_num = 0;
    afe_cfg.pcm_config.sample_rate = 16000;

    /* Create AFE */
    s_afe_iface = &ESP_AFE_SR_HANDLE;
    s_afe_data = s_afe_iface->create_from_config(&afe_cfg);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE");
        return ESP_FAIL;
    }

    /* Create MultiNet */
    s_mn_iface = esp_mn_handle_from_name(mn_name);
    if (s_mn_iface == NULL) {
        ESP_LOGE(TAG, "Failed to get MultiNet handle for %s", mn_name);
        return ESP_FAIL;
    }

    s_mn_data = s_mn_iface->create(mn_name, MN_DETECT_TIMEOUT_MS);
    if (s_mn_data == NULL) {
        ESP_LOGE(TAG, "Failed to create MultiNet model");
        return ESP_FAIL;
    }

    /* Register speech commands */
    esp_err_t ret = register_speech_commands();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Speech command registration had issues");
    }

    s_initialized = true;

    /* Register I2S TX hooks so any audio_play_wav() call (from alarm_manager,
     * TTS, etc.) automatically pauses/resumes the mic feed task.
     * Combined with ref-counting in pause/resume, this is safe to nest with
     * manual pause/resume calls in handle_command_response(). */
    audio_set_i2s_tx_hooks(pause_feed_task, resume_feed_task);

    ESP_LOGI(TAG, "Voice command module initialized successfully");
    return ESP_OK;
}

esp_err_t voice_cmd_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    s_running = true;

    /* Create feed task (reads mic, feeds AFE) */
    BaseType_t ok = xTaskCreatePinnedToCore(
        feed_task,
        "vc_feed",
        8192,
        NULL,
        5,
        &s_feed_task,
        0       /* core 0 */
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create feed task");
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    /* Create detect task (fetches from AFE, runs WakeNet/MultiNet) */
    ok = xTaskCreatePinnedToCore(
        detect_task,
        "vc_detect",
        8192,
        NULL,
        5,
        &s_detect_task,
        1       /* core 1 */
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create detect task");
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Voice recognition started");
    return ESP_OK;
}

esp_err_t voice_cmd_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    s_running = false;
    ESP_LOGI(TAG, "Voice command stop requested, waiting for tasks...");

    /* If feed task is paused, resume it so it can exit */
    if (s_feed_event != NULL) {
        xEventGroupSetBits(s_feed_event, FEED_BIT_RESUME);
    }

    /* Wait for tasks to self-delete */
    int timeout = 50;
    while ((s_feed_task != NULL || s_detect_task != NULL) && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }

    ESP_LOGI(TAG, "Voice recognition stopped");
    return ESP_OK;
}

void voice_cmd_register_cb(voice_cmd_cb_t cb)
{
    s_user_cb = cb;
}
