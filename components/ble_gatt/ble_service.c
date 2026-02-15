/**
 * @file ble_service.c
 * @brief BLE GATT 服务实现 - NimBLE 初始化、GATT 注册、广播、Notify
 */

#include "ble_service.h"
#include "ble_gatt_defs.h"
#include "event_bus.h"

#include "sensor_service.h"
#include "health_monitor.h"
#include "pedometer.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ble_svc";

/* ============== 内部状态 ============== */

static bool s_initialized = false;
static bool s_connected   = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* GATT 特征值句柄 (注册时由 NimBLE 填入) */
static uint16_t s_telemetry_val_handle = 0;
static uint16_t s_alarm_val_handle     = 0;
static uint16_t s_command_val_handle   = 0;
static uint16_t s_status_val_handle    = 0;

/* ============== 前向声明 ============== */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_host_task(void *param);
static int ble_start_advertise(void);
static void telemetry_task(void *param);

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
            /* Command (FF03): Write */
            {
                .uuid       = &s_chr_command_uuid.u,
                .access_cb  = gatt_chr_access_command,
                .val_handle = &s_command_val_handle,
                .flags      = BLE_GATT_CHR_F_WRITE,
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
 */
static int gatt_chr_access_command(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len < 1) {
            ESP_LOGW(TAG, "Command: empty payload");
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t cmd_type = 0;
        ble_hs_mbuf_to_flat(ctxt->om, &cmd_type, 1, NULL);
        ESP_LOGI(TAG, "Command received: type=0x%02X, len=%u", cmd_type, om_len);

        /* TODO: 迭代 2.5 中实现命令解析与执行 */
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
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
            .alarm_state = 0,    /* TODO: 从 alarm_manager 获取 */
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

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
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
            ESP_LOGI(TAG, "Connected, handle=%d", s_conn_handle);

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
        ESP_LOGI(TAG, "Disconnected, reason=%d",
                 event->disconnect.reason);
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

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
        ESP_LOGI(TAG, "Advertising complete");
        /* 广播超时后重新广播 */
        ble_start_advertise();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event: handle=%d, cur_notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;

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
    ESP_LOGI(TAG, "NimBLE host synced");

    /* 使用默认公共地址 */
    int rc = ble_hs_id_infer_auto(0, &(uint8_t){0});
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to infer address type, rc=%d", rc);
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
 * Telemetry 定时上报任务
 * 每 BLE_TELEMETRY_INTERVAL_MS (120秒) 采集传感器数据并通过 Notify 发送
 */
static void telemetry_task(void *param)
{
    ESP_LOGI(TAG, "Telemetry task started, interval=%d ms",
             BLE_TELEMETRY_INTERVAL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BLE_TELEMETRY_INTERVAL_MS));

        /* 未连接时跳过 */
        if (!ble_is_connected()) {
            continue;
        }

        /* 采集传感器数据 */
        sensor_data_t sensor;
        esp_err_t ret = sensor_get_latest(&sensor);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get sensor data, ret=%d", ret);
            continue;
        }

        health_status_t health = health_get_status();

        /* 组装 Telemetry 数据包 */
        ble_telemetry_t pkt;
        memset(&pkt, 0, sizeof(pkt));

        pkt.temp       = (int16_t)(sensor.temperature * 10);
        pkt.heart_rate = health.heart_rate;
        pkt.spo2       = health.spo2;
        pkt.steps      = pedometer_get_steps();
        pkt.battery    = BLE_BATTERY_DEFAULT;
        pkt.data_valid = sensor.data_valid;
        /* TODO: timestamp 目前是系统启动后的毫秒数除以 1000，并非 Unix 时间戳。
         * 需要在实现时间同步（NTP 或手机下发）后，改为真正的 Unix 秒。 */
        pkt.timestamp  = (uint32_t)(sensor.timestamp / 1000);

        /* 发送 Notify */
        ret = ble_notify_telemetry(&pkt);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Telemetry sent: temp=%d hr=%d spo2=%d steps=%lu",
                     pkt.temp, pkt.heart_rate, pkt.spo2,
                     (unsigned long)pkt.steps);
        } else {
            ESP_LOGD(TAG, "Telemetry notify skipped, ret=%d", ret);
        }
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

    /* 2. 设置设备名 */
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name, rc=%d", rc);
        return ESP_FAIL;
    }

    /* 3. 初始化 GAP 和 GATT 标准服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 4. 注册自定义 GATT 服务 */
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

    /* 5. 设置 NimBLE host 回调 */
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* 6. 启动 NimBLE host 任务 */
    nimble_port_freertos_init(ble_host_task);

    /* 7. 启动 Telemetry 定时上报任务 */
    BaseType_t xret = xTaskCreate(
        telemetry_task,
        "ble_telem",
        BLE_TELEMETRY_TASK_STACK,
        NULL,
        BLE_TELEMETRY_TASK_PRIORITY,
        NULL
    );
    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "BLE service initialized, device name: %s", BLE_DEVICE_NAME);
    return ESP_OK;
}

esp_err_t ble_notify_telemetry(const ble_telemetry_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, sizeof(ble_telemetry_t));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for telemetry");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_telemetry_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Telemetry notify failed, rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_notify_alarm(const ble_alarm_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, sizeof(ble_alarm_t));
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for alarm");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_alarm_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Alarm notify failed, rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ble_is_connected(void)
{
    return s_connected;
}