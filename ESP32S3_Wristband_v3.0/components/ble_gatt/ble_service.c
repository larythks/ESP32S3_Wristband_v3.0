/**
 * @file ble_service.c
 * @brief BLE GATT 服务实现 - NimBLE 初始化、GATT 注册、广播、Notify
 */

#include "ble_service.h"
#include "ble_security.h"
#include "ble_gatt_defs.h"
#include "event_bus.h"
#include "alarm_manager.h"
#include "ds3231.h"

#include "sensor_service.h"
#include "health_monitor.h"
#include "pedometer.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <limits.h>
#include <math.h>

/* NimBLE store config init (未在公开头文件声明) */
extern void ble_store_config_init(void);

static const char *TAG = "ble_svc";

/* ============== 内部状态 ============== */

static bool s_initialized = false;
static bool s_connected   = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

/* GATT 特征值句柄 (注册时由 NimBLE 填入) */
static uint16_t s_telemetry_val_handle = 0;
static uint16_t s_alarm_val_handle     = 0;
static uint16_t s_command_val_handle   = 0;
static uint16_t s_status_val_handle    = 0;

/* 时间偏移量 (秒): Unix时间戳 = esp_timer秒 + s_time_offset */
static int64_t s_time_offset = 0;

/* Telemetry 动态间隔: 正常模式为事件驱动(portMAX_DELAY) */
static volatile uint32_t s_telemetry_interval_ms = portMAX_DELAY;
static TaskHandle_t s_telemetry_task_handle = NULL;
static SemaphoreHandle_t s_temp_snapshot_mutex = NULL;
static sensor_data_t s_temp_snapshot;
static bool s_temp_snapshot_pending = false;

/* ============== 前向声明 ============== */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_host_task(void *param);
static int ble_start_advertise(void);
static void telemetry_task(void *param);
static esp_err_t send_telemetry_now(void);
static esp_err_t send_telemetry_from_sensor(const sensor_data_t *sensor);
static bool take_temp_snapshot(sensor_data_t *sensor);
static void on_sampling_mode_change(const event_t *event, void *user_data);
static void on_hr_result_ready(const event_t *event, void *user_data);

/* GATT access 回调 */
static int gatt_chr_access_telemetry(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_chr_access_alarm(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_chr_access_command(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_chr_access_status(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ============== UUID 实例 ============== */

static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(BLE_SVC_CAREBAND_UUID128);

static const ble_uuid128_t s_chr_telemetry_uuid =
    BLE_UUID128_INIT(BLE_CHR_TELEMETRY_UUID128);

static const ble_uuid128_t s_chr_alarm_uuid =
    BLE_UUID128_INIT(BLE_CHR_ALARM_UUID128);

static const ble_uuid128_t s_chr_command_uuid =
    BLE_UUID128_INIT(BLE_CHR_COMMAND_UUID128);

static const ble_uuid128_t s_chr_status_uuid =
    BLE_UUID128_INIT(BLE_CHR_STATUS_UUID128);

/* ============== GATT 服务定义表 ============== */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            /* Telemetry (FF01): Notify */
            {
                .uuid       = &s_chr_telemetry_uuid.u,
                .access_cb  = gatt_chr_access_telemetry,
                .val_handle = &s_telemetry_val_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            /* Alarm (FF02): Notify */
            {
                .uuid       = &s_chr_alarm_uuid.u,
                .access_cb  = gatt_chr_access_alarm,
                .val_handle = &s_alarm_val_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            /* Command (FF03): Write (加密连接才允许) */
            {
                .uuid       = &s_chr_command_uuid.u,
                .access_cb  = gatt_chr_access_command,
                .val_handle = &s_command_val_handle,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            /* Status (FF04): Read */
            {
                .uuid       = &s_chr_status_uuid.u,
                .access_cb  = gatt_chr_access_status,
                .val_handle = &s_status_val_handle,
                .flags      = BLE_GATT_CHR_F_READ,
            },
            /* 终止符 */
            { 0 },
        },
    },
    /* 服务数组终止符 */
    { 0 },
};

/* ============== GATT Access 回调实现 ============== */

/**
 * Telemetry 特征 access 回调 (Notify-only, 读写不通过此回调)
 */
static int gatt_chr_access_telemetry(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Notify-only 特征无需处理读写请求 */
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * Alarm 特征 access 回调 (Notify-only)
 */
static int gatt_chr_access_alarm(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * Command 特征 access 回调 (Write)
 * 解析命令类型并分发处理，所有命令均包含 nonce 防重放校验
 */
static int gatt_chr_access_command(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len < 1) {
        ESP_LOGW(TAG, "Command: empty payload");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 读取完整 payload 到本地 buffer */
    uint8_t buf[16] = {0};
    uint16_t copy_len = (om_len > sizeof(buf)) ? sizeof(buf) : om_len;
    ble_hs_mbuf_to_flat(ctxt->om, buf, copy_len, NULL);

    uint8_t cmd_type = buf[0];
    ESP_LOGI(TAG, "Command received: type=0x%02X, len=%u", cmd_type, om_len);

    switch (cmd_type) {
    case BLE_CMD_ACK_ALARM: {
        if (om_len < sizeof(ble_cmd_ack_alarm_t)) {
            ESP_LOGW(TAG, "ACK_ALARM: payload too short (%u < %u)",
                     om_len, (unsigned)sizeof(ble_cmd_ack_alarm_t));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        const ble_cmd_ack_alarm_t *cmd = (const ble_cmd_ack_alarm_t *)buf;
        if (!ble_security_validate_nonce(cmd->nonce)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        ESP_LOGI(TAG, "ACK_ALARM: event_id=%lu, nonce=%lu",
                 (unsigned long)cmd->event_id, (unsigned long)cmd->nonce);
        alarm_ack(cmd->event_id);
        return 0;
    }

    case BLE_CMD_SYNC_TIME: {
        if (om_len < sizeof(ble_cmd_sync_time_t)) {
            ESP_LOGW(TAG, "SYNC_TIME: payload too short (%u < %u)",
                     om_len, (unsigned)sizeof(ble_cmd_sync_time_t));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        const ble_cmd_sync_time_t *cmd = (const ble_cmd_sync_time_t *)buf;
        if (!ble_security_validate_nonce(cmd->nonce)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        int64_t local_sec = (int64_t)(esp_timer_get_time() / 1000000);
        s_time_offset = (int64_t)cmd->timestamp - local_sec;
        ESP_LOGI(TAG, "SYNC_TIME: unix=%lu, offset=%lld",
                 (unsigned long)cmd->timestamp, (long long)s_time_offset);

        /* 将 Unix 时间戳写入 DS3231 RTC */
        {
            uint32_t ts = cmd->timestamp + 8 * 3600;  /* UTC+8 北京时间 */
            uint32_t days = ts / 86400;
            uint32_t daytime = ts % 86400;
            uint8_t hour = daytime / 3600;
            uint8_t minute = (daytime % 3600) / 60;
            uint8_t second = daytime % 60;

            /* 从 1970-01-01 起算日期 */
            uint16_t year = 1970;
            while (1) {
                uint16_t yday = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
                if (days < yday) break;
                days -= yday;
                year++;
            }
            static const uint16_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
            uint8_t month = 0;
            for (month = 0; month < 12; month++) {
                uint16_t md = mdays[month] + ((month == 1 && leap) ? 1 : 0);
                if (days < md) break;
                days -= md;
            }

            ds3231_time_t rtc_time = {
                .year   = year,
                .month  = month + 1,
                .day    = (uint8_t)(days + 1),
                .hour   = hour,
                .minute = minute,
                .second = second,
            };
            /* 1970-01-01 是星期四(4), DS3231 day_of_week: 1=周一 ... 7=周日 */
            uint32_t total_days = ts / 86400;
            uint8_t dow = (uint8_t)((total_days + 3) % 7 + 1);  /* +3: 周四偏移 */
            rtc_time.day_of_week = dow;
            esp_err_t ret = ds3231_set_time(&rtc_time);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "DS3231 synced: %04d-%02d-%02d %02d:%02d:%02d",
                         rtc_time.year, rtc_time.month, rtc_time.day,
                         rtc_time.hour, rtc_time.minute, rtc_time.second);
            } else {
                ESP_LOGW(TAG, "DS3231 set_time failed: %s", esp_err_to_name(ret));
            }
        }
        return 0;
    }

    case BLE_CMD_REQUEST_REPORT: {
        if (om_len < sizeof(ble_cmd_request_report_t)) {
            ESP_LOGW(TAG, "REQUEST_REPORT: payload too short (%u < %u)",
                     om_len, (unsigned)sizeof(ble_cmd_request_report_t));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        const ble_cmd_request_report_t *cmd =
            (const ble_cmd_request_report_t *)buf;
        if (!ble_security_validate_nonce(cmd->nonce)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        ESP_LOGI(TAG, "REQUEST_REPORT: triggering immediate telemetry");
        send_telemetry_now();
        return 0;
    }

    case BLE_CMD_MANUAL_MEASURE: {
        if (om_len < sizeof(ble_cmd_manual_measure_t)) {
            ESP_LOGW(TAG, "MANUAL_MEASURE: payload too short (%u < %u)",
                     om_len, (unsigned)sizeof(ble_cmd_manual_measure_t));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        const ble_cmd_manual_measure_t *cmd =
            (const ble_cmd_manual_measure_t *)buf;
        if (!ble_security_validate_nonce(cmd->nonce)) {
            return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
        }
        if (cmd->mode == 1) {
            ESP_LOGI(TAG, "MANUAL_MEASURE: start, duration=%u s",
                     cmd->duration_s);
            sensor_start_hr_measure();
        } else {
            ESP_LOGI(TAG, "MANUAL_MEASURE: stop");
        }
        return 0;
    }

    default:
        ESP_LOGW(TAG, "Unknown command type: 0x%02X", cmd_type);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * Status 特征 access 回调 (Read)
 */
static int gatt_chr_access_status(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ble_status_pkt_t status = {
            .device_state = 1,   /* running */
            .ble_conn_count = s_connected ? 1 : 0,
            .alarm_state = (uint8_t)alarm_get_state(),
        };

        int rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ============== 广播控制 ============== */

/**
 * 开始 BLE 广播
 */
static int ble_start_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    /*
     * 广播数据 (ADV_IND): Flags + 设备名
     * Flags: 3 bytes, Name "CareBand": 10 bytes = 13 bytes (远低于 31 上限)
     *
     * 128-bit UUID 移到 Scan Response 中，避免广播数据超 31 字节限制 (B-1 fix)
     */
    memset(&fields, 0, sizeof(fields));

    /* 广播标志: general discoverable + BR/EDR not supported */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 广播设备名称 */
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields, rc=%d", rc);
        return rc;
    }

    /* Scan Response: 服务 UUID */
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = &s_svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan rsp fields, rc=%d", rc);
        return rc;
    }

    /* 广播参数: connectable, general discoverable */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Advertising started as \"%s\"", name);
    return 0;
}

/* ============== GAP 事件处理 ============== */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s, status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;

            /* 主动发起安全请求（触发配对/加密） */
            int sec_rc = ble_gap_security_initiate(s_conn_handle);
            if (sec_rc != 0) {
                ESP_LOGW(TAG, "Security initiate failed, rc=%d", sec_rc);
            }

            /* 发布 BLE 连接事件到事件总线 */
            event_data_t evt_data;
            memset(&evt_data, 0, sizeof(evt_data));
            evt_data.raw[0] = BLE_CONN_STATE_CONNECTED;
            event_publish(EVT_BLE_CONN, &evt_data);
        } else {
            /* 连接失败，重新广播 */
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_start_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected, reason=%d (0x%02X)",
                 event->disconnect.reason,
                 event->disconnect.reason);
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

        /* 重置 nonce，允许新连接从 nonce=1 重新开始 */
        ble_security_reset_nonce();

        /* 发布 BLE 断连事件到事件总线 */
        {
            event_data_t evt_data;
            memset(&evt_data, 0, sizeof(evt_data));
            evt_data.raw[0] = BLE_CONN_STATE_DISCONNECTED;
            event_publish(EVT_BLE_CONN, &evt_data);
        }

        /* 断连后重新广播 */
        ble_start_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "Advertising complete");
        /* 广播超时后重新广播 */
        ble_start_advertise();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "MTU updated: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGD(TAG, "Subscribe event: handle=%d, cur_notify=%d, cur_indicate=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        break;

    /* 安全相关事件转发到 ble_security 模块 */
    case BLE_GAP_EVENT_ENC_CHANGE:
    case BLE_GAP_EVENT_REPEAT_PAIRING:
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        return ble_security_gap_event(event);

    default:
        break;
    }

    return 0;
}

/* ============== NimBLE Host 回调 ============== */

/**
 * NimBLE host 与 controller 同步完成回调
 * 在此开始广播
 */
static void ble_on_sync(void)
{
    int rc;
    uint8_t addr_val[6] = {0};

    ESP_LOGI(TAG, "NimBLE host synced");

    /* 使用默认公共地址 */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure BLE address, rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type, rc=%d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(s_own_addr_type, addr_val, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X (type=%u)",
                 addr_val[5], addr_val[4], addr_val[3],
                 addr_val[2], addr_val[1], addr_val[0],
                 s_own_addr_type);
    } else {
        ESP_LOGW(TAG, "Failed to copy BLE address, rc=%d", rc);
    }

    ble_start_advertise();
}

/**
 * NimBLE host 重置回调
 */
static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d", reason);
}

/**
 * NimBLE host 任务入口
 */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    /* nimble_port_run 正常不会返回 */
    nimble_port_freertos_deinit();
}

/* ============== Telemetry 定时上报任务 ============== */

/**
 * 获取当前 Unix 时间戳 (秒)
 * 基于 esp_timer + 时间同步偏移量
 */
uint32_t ble_get_unix_timestamp(void)
{
    int64_t local_sec = (int64_t)(esp_timer_get_time() / 1000000);
    int64_t unix_sec = local_sec + s_time_offset;
    return (unix_sec > 0) ? (uint32_t)unix_sec : 0;
}

/**
 * 立即采集并发送一次 Telemetry 数据
 */
static esp_err_t send_telemetry_from_sensor(const sensor_data_t *sensor)
{
    if (!ble_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    health_status_t health = health_get_status();

    /* 组装 Telemetry 数据包 */
    ble_telemetry_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.temp       = (int16_t)lroundf(sensor->temperature * 10.0f);
    pkt.heart_rate = health.heart_rate;
    pkt.spo2       = health.spo2;
    pkt.steps      = sensor->steps;
    pkt.data_valid = sensor->data_valid;
    pkt.timestamp  = ble_get_unix_timestamp();

    /* 发送 Notify */
    esp_err_t ret = ble_notify_telemetry(&pkt);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Telemetry sent: temp=%d hr=%d spo2=%d steps=%lu ts=%lu",
                 pkt.temp, pkt.heart_rate, pkt.spo2,
                 (unsigned long)pkt.steps, (unsigned long)pkt.timestamp);
    } else {
        ESP_LOGD(TAG, "Telemetry notify skipped, ret=%d", ret);
    }

    return ret;
}

static esp_err_t send_telemetry_now(void)
{
    sensor_data_t sensor;
    esp_err_t ret = sensor_get_latest(&sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get sensor data, ret=%d", ret);
        return ret;
    }

    sensor.steps = pedometer_get_steps();
    return send_telemetry_from_sensor(&sensor);
}

static bool take_temp_snapshot(sensor_data_t *sensor)
{
    if (sensor == NULL || s_temp_snapshot_mutex == NULL) {
        return false;
    }

    bool has_snapshot = false;
    xSemaphoreTake(s_temp_snapshot_mutex, portMAX_DELAY);
    if (s_temp_snapshot_pending) {
        memcpy(sensor, &s_temp_snapshot, sizeof(sensor_data_t));
        s_temp_snapshot_pending = false;
        has_snapshot = true;
    }
    xSemaphoreGive(s_temp_snapshot_mutex);

    return has_snapshot;
}

/**
 * Telemetry 上报任务
 * 正常模式: 纯事件驱动，等待 EVT_HR_RESULT_READY 唤醒后发送
 * 实时模式: 保持 16 秒周期上报
 */
static void telemetry_task(void *param)
{
    ESP_LOGI(TAG, "Telemetry task started (event-driven in normal mode)");

    while (1) {
        uint32_t interval = s_telemetry_interval_ms;
        /* 使用 xTaskNotifyWait 代替 vTaskDelay:
         * - 正常情况等待 interval 超时后发送
         * - 收到 notify 时立即重新计算间隔 */
        xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(interval));

        sensor_data_t sensor;
        if (take_temp_snapshot(&sensor)) {
            send_telemetry_from_sensor(&sensor);
        } else {
            send_telemetry_now();
        }
    }
}

/**
 * 采样模式变更回调 - 切换 Telemetry 上报间隔
 */
static void on_sampling_mode_change(const event_t *event, void *user_data)
{
    (void)user_data;
    sampling_mode_t mode = event->data.sampling.mode;

    if (mode == SAMPLING_MODE_REALTIME) {
        s_telemetry_interval_ms = BLE_TELEMETRY_REALTIME_INTERVAL_MS;
        ESP_LOGI(TAG, "Telemetry interval -> %d ms (realtime)",
                 BLE_TELEMETRY_REALTIME_INTERVAL_MS);
    } else {
        s_telemetry_interval_ms = portMAX_DELAY;
        ESP_LOGI(TAG, "Telemetry interval -> event-driven (normal)");
    }

    /* 唤醒 telemetry 任务，立即应用新间隔 */
    if (s_telemetry_task_handle != NULL) {
        xTaskNotify(s_telemetry_task_handle, 0, eNoAction);
    }
}

/**
 * HR/SpO2 测量完成且数据全部有效回调 - 触发立即上报
 */
static void on_hr_result_ready(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    ESP_LOGI(TAG, "HR result ready, triggering telemetry upload");
    if (s_telemetry_task_handle != NULL) {
        xTaskNotify(s_telemetry_task_handle, 0, eNoAction);
    }
}

/* ============== 公共 API ============== */

esp_err_t ble_service_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "BLE service already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int rc;

    /* 1. 初始化 NimBLE 协议栈 */
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init() failed, ret=%d", ret);
        return ESP_FAIL;
    }

    /* 2. 初始化 BLE Store Config */
    ble_store_config_init();

    /* 3. 设置设备名 */
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name, rc=%d", rc);
        return ESP_FAIL;
    }

    /* 4. 初始化 GAP 和 GATT 标准服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 5. 注册自定义 GATT 服务 */
    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg() failed, rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs() failed, rc=%d", rc);
        return ESP_FAIL;
    }

    /* 6. 配置 BLE 安全参数 */
    ret = ble_security_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE security init failed");
        return ESP_FAIL;
    }

    /* 7. 设置 NimBLE host 回调 */
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    s_temp_snapshot_mutex = xSemaphoreCreateMutex();
    if (s_temp_snapshot_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create temp snapshot mutex");
        return ESP_ERR_NO_MEM;
    }

    /* 8. 启动 NimBLE host 任务 */
    nimble_port_freertos_init(ble_host_task);

    /* 9. 启动 Telemetry 定时上报任务 */
    BaseType_t xret = xTaskCreate(
        telemetry_task,
        "ble_telem",
        BLE_TELEMETRY_TASK_STACK,
        NULL,
        BLE_TELEMETRY_TASK_PRIORITY,
        &s_telemetry_task_handle
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        return ESP_ERR_NO_MEM;
    }

    /* 订阅采样模式变更事件 */
    event_subscribe(EVT_SAMPLING_MODE, on_sampling_mode_change, NULL);

    /* 订阅 HR/SpO2 结果就绪事件，触发立即上报 */
    event_subscribe(EVT_HR_RESULT_READY, on_hr_result_ready, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE service initialized, device name: %s", BLE_DEVICE_NAME);
    return ESP_OK;
}

/**
 * @brief 通用 BLE Notify 发送辅助函数
 */
static esp_err_t ble_notify_raw(uint16_t attr_handle,
                                const void *data, size_t len,
                                const char *name)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for %s", name);
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gatts_notify_custom(s_conn_handle, attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "%s notify failed, rc=%d", name, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_notify_telemetry(const ble_telemetry_t *data)
{
    return ble_notify_raw(s_telemetry_val_handle,
                          data, sizeof(ble_telemetry_t), "telemetry");
}

esp_err_t ble_notify_alarm(const ble_alarm_t *data)
{
    return ble_notify_raw(s_alarm_val_handle,
                          data, sizeof(ble_alarm_t), "alarm");
}

bool ble_is_connected(void)
{
    return s_connected;
}
