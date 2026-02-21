/**
 * @file ble_security.c
 * @brief BLE 安全管理器实现 - SM 配置、nonce 校验、加密事件处理
 */

#include "ble_security.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include <string.h>

static const char *TAG = "ble_sec";

/* Nonce 状态 */
static uint32_t s_last_nonce = 0;

esp_err_t ble_security_init(void)
{
    /* 配置 Security Manager */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;    /* 无输入输出 */
    ble_hs_cfg.sm_bonding = 1;                       /* 启用绑定 */
    ble_hs_cfg.sm_sc = 1;                            /* Secure Connections */
    ble_hs_cfg.sm_mitm = 0;                          /* 无 MITM（NoIO 不支持） */

    /* 密钥分发：分发 LTK 和 IRK */
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    s_last_nonce = 0;

    ESP_LOGI(TAG, "BLE security initialized: IO=NoIO, bonding=on, SC=on");
    return ESP_OK;
}

bool ble_security_validate_nonce(uint32_t nonce)
{
    if (nonce <= s_last_nonce) {
        ESP_LOGW(TAG, "Nonce replay rejected: received=%lu, last=%lu",
                 (unsigned long)nonce, (unsigned long)s_last_nonce);
        return false;
    }
    s_last_nonce = nonce;
    ESP_LOGD(TAG, "Nonce accepted: %lu", (unsigned long)nonce);
    return true;
}

void ble_security_reset_nonce(void)
{
    s_last_nonce = 0;
    ESP_LOGI(TAG, "Nonce reset");
}

bool ble_security_is_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "Connection not found: handle=%d", conn_handle);
        return false;
    }
    return desc.sec_state.encrypted;
}

int ble_security_gap_event(struct ble_gap_event *event)
{
    switch (event->type) {
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: conn_handle=%d, status=%d",
                 event->enc_change.conn_handle,
                 event->enc_change.status);
        if (event->enc_change.status == 0) {
            /* 加密成功，打印安全状态 */
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle,
                                  &desc) == 0) {
                ESP_LOGI(TAG,
                         "Security: encrypted=%d, authenticated=%d, bonded=%d",
                         desc.sec_state.encrypted,
                         desc.sec_state.authenticated,
                         desc.sec_state.bonded);
            }
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* 已有绑定设备请求重新配对 */
        /* 删除旧绑定，接受新配对（实现仅 1 台绑定限制） */
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(TAG, "Deleted old bond for repeat pairing");
        }
        /* 返回 BLE_GAP_REPEAT_PAIRING_RETRY 表示接受重新配对 */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* NoInputNoOutput 模式下，使用 Just Works 配对 */
        ESP_LOGI(TAG, "Passkey action: %d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
            /* Just Works，无需操作 */
        }
        return 0;

    default:
        return -1;  /* 未处理的事件 */
    }
}
