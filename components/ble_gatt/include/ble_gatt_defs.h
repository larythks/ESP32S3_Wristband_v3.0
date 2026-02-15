/**
 * @file ble_gatt_defs.h
 * @brief BLE GATT 服务 UUID 定义和数据包结构
 *
 * 服务 UUID: 0000FF00-0000-1000-8000-00805F9B34FB
 * 特征:
 *   Telemetry (FF01) - Notify, 20 bytes
 *   Alarm     (FF02) - Notify, 16 bytes
 *   Command   (FF03) - Write
 *   Status    (FF04) - Read
 */

#ifndef BLE_GATT_DEFS_H
#define BLE_GATT_DEFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== UUID 定义 ============== */

/**
 * 基础 UUID: 0000xxxx-0000-1000-8000-00805F9B34FB
 * 使用 BLE SIG 标准 128-bit 基础 UUID，短 UUID 嵌入 octet 12-13
 *
 * NimBLE ble_uuid128_t 存储为 little-endian 字节序:
 * FB 34 9B 5F 80 00 00 80 00 10 00 00 xx xx 00 00
 */

/* 服务 UUID: 0000FF00-0000-1000-8000-00805F9B34FB */
#define BLE_SVC_CAREBAND_UUID128                            \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,        \
    0x00, 0x10, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00

/* Telemetry 特征 UUID: 0000FF01-... */
#define BLE_CHR_TELEMETRY_UUID128                           \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,        \
    0x00, 0x10, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00

/* Alarm 特征 UUID: 0000FF02-... */
#define BLE_CHR_ALARM_UUID128                               \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,        \
    0x00, 0x10, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00

/* Command 特征 UUID: 0000FF03-... */
#define BLE_CHR_COMMAND_UUID128                             \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,        \
    0x00, 0x10, 0x00, 0x00, 0x03, 0xFF, 0x00, 0x00

/* Status 特征 UUID: 0000FF04-... */
#define BLE_CHR_STATUS_UUID128                              \
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,        \
    0x00, 0x10, 0x00, 0x00, 0x04, 0xFF, 0x00, 0x00

/* ============== 设备名称 ============== */

#define BLE_DEVICE_NAME     "CareBand"

/* ============== 数据包结构 ============== */

/**
 * @brief Telemetry 数据包 (20 bytes)
 *
 * Offset  Size  Field        Type      Unit
 * 0       2     temp         int16     deg C x 10
 * 2       1     heart_rate   uint8     bpm
 * 3       1     spo2         uint8     %
 * 4       4     steps        uint32    count
 * 8       1     battery      uint8     %
 * 9       1     data_valid   uint8     bitmap
 * 10      4     timestamp    uint32    Unix seconds
 * 14      6     reserved     -         -
 */
typedef struct __attribute__((packed)) {
    int16_t  temp;          /* offset 0:  temperature, deg C x 10 */
    uint8_t  heart_rate;    /* offset 2:  bpm */
    uint8_t  spo2;          /* offset 3:  % */
    uint32_t steps;         /* offset 4:  step count */
    uint8_t  battery;       /* offset 8:  battery % */
    uint8_t  data_valid;    /* offset 9:  valid data bitmap */
    uint32_t timestamp;     /* offset 10: Unix seconds */
    uint8_t  reserved[6];   /* offset 14: reserved */
} ble_telemetry_t;

/* Compile-time size check */
_Static_assert(sizeof(ble_telemetry_t) == 20,
               "ble_telemetry_t must be exactly 20 bytes");

/**
 * @brief Alarm 数据包 (16 bytes)
 *
 * Offset  Size  Field        Type      Description
 * 0       4     event_id     uint32    unique event ID
 * 4       1     alarm_type   uint8     1-9
 * 5       2     value        int16     trigger value x 10
 * 7       1     battery      uint8     battery %
 * 8       4     timestamp    uint32    Unix seconds
 * 12      4     reserved     -         -
 */
typedef struct __attribute__((packed)) {
    uint32_t event_id;      /* offset 0:  unique event ID */
    uint8_t  alarm_type;    /* offset 4:  alarm type 1-9 */
    int16_t  value;         /* offset 5:  trigger value x 10 */
    uint8_t  battery;       /* offset 7:  battery % */
    uint32_t timestamp;     /* offset 8:  Unix seconds */
    uint8_t  reserved[4];   /* offset 12: reserved */
} ble_alarm_t;

/* Compile-time size check */
_Static_assert(sizeof(ble_alarm_t) == 16,
               "ble_alarm_t must be exactly 16 bytes");

/* ============== 报警类型枚举 ============== */

/**
 * @brief BLE 报警类型 (alarm_type 字段取值)
 * 与 development_plan.md 第 3.1 节 Alarm 数据包定义一致
 */
typedef enum {
    BLE_ALARM_TYPE_NONE         = 0,
    BLE_ALARM_TYPE_TEMP_HIGH    = 1,    /* 体温过高 */
    BLE_ALARM_TYPE_TEMP_LOW     = 2,    /* 体温过低 */
    BLE_ALARM_TYPE_HR_HIGH      = 3,    /* 心率过高 */
    BLE_ALARM_TYPE_HR_LOW       = 4,    /* 心率过低 */
    BLE_ALARM_TYPE_SPO2_LOW     = 5,    /* 血氧过低 */
    BLE_ALARM_TYPE_FALL         = 6,    /* 跌倒 */
    BLE_ALARM_TYPE_MANUAL       = 7,    /* 手动报警 */
    BLE_ALARM_TYPE_SPO2_WARNING = 8,    /* 血氧预警 (WARNING) */
    BLE_ALARM_TYPE_CALL_FAMILY  = 9,    /* 呼叫家人 */
} ble_alarm_type_t;

/* ============== Command 类型枚举 ============== */

/**
 * @brief BLE 命令类型 (cmd_type 字段取值)
 * 与 development_plan.md 第 3.1 节 Command 类型定义一致
 */
typedef enum {
    BLE_CMD_ACK_ALARM       = 0x01,     /* ACK 报警 */
    BLE_CMD_SYNC_TIME       = 0x02,     /* 同步时间 */
    BLE_CMD_REQUEST_REPORT  = 0x03,     /* 请求立即上报 */
    BLE_CMD_MANUAL_MEASURE  = 0x04,     /* 手动测量控制 */
} ble_cmd_type_t;

/* ============== Command 数据包结构体 ============== */

/** ACK 报警: cmd_type(1) + event_id(4) + nonce(4) = 9 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;      /* BLE_CMD_ACK_ALARM (0x01) */
    uint32_t event_id;      /* 报警事件 ID */
    uint32_t nonce;         /* 防重放 nonce */
} ble_cmd_ack_alarm_t;

/** 同步时间: cmd_type(1) + timestamp(4) + nonce(4) = 9 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;      /* BLE_CMD_SYNC_TIME (0x02) */
    uint32_t timestamp;     /* Unix 时间戳 */
    uint32_t nonce;         /* 防重放 nonce */
} ble_cmd_sync_time_t;

/** 请求立即上报: cmd_type(1) + nonce(4) = 5 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;      /* BLE_CMD_REQUEST_REPORT (0x03) */
    uint32_t nonce;         /* 防重放 nonce */
} ble_cmd_request_report_t;

/** 手动测量控制: cmd_type(1) + mode(1) + duration_s(1) + nonce(4) = 7 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;      /* BLE_CMD_MANUAL_MEASURE (0x04) */
    uint8_t  mode;          /* 0=stop, 1=start */
    uint8_t  duration_s;    /* 测量持续秒数 */
    uint32_t nonce;         /* 防重放 nonce */
} ble_cmd_manual_measure_t;

/* ============== Status 数据包 ============== */

/**
 * @brief Status 数据包 (App 主动读取)
 */
typedef struct __attribute__((packed)) {
    uint8_t  device_state;      /* 设备状态 */
    uint8_t  ble_conn_count;    /* BLE 连接数 */
    uint8_t  alarm_state;       /* 报警状态 */
} ble_status_pkt_t;

/* ============== 连接状态枚举 ============== */

typedef enum {
    BLE_CONN_STATE_DISCONNECTED = 0,
    BLE_CONN_STATE_CONNECTED    = 1,
} ble_conn_state_t;

/* ============== Telemetry 上报配置 ============== */

/** Telemetry 上报间隔 (ms) - 常规模式 2 分钟 */
#define BLE_TELEMETRY_INTERVAL_MS   120000

/** Telemetry 上报任务栈大小 */
#define BLE_TELEMETRY_TASK_STACK    3072

/** Telemetry 上报任务优先级 */
#define BLE_TELEMETRY_TASK_PRIORITY 3

/* ============== 默认电池电量 (MVP) ============== */

#define BLE_BATTERY_DEFAULT         100

#ifdef __cplusplus
}
#endif

#endif /* BLE_GATT_DEFS_H */
