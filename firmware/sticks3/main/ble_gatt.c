#include "ble_gatt.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble_gatt";

/* 冻结 UUID（M2-BLE线协议冻结.md §1）。BLE_UUID128_INIT 按小端字节序展开。 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x07, 0xaf, 0x31, 0xce, 0x6a, 0x8a, 0xb8, 0xbe,
                     0xd6, 0x53, 0x4a, 0x33, 0x80, 0x7f, 0x58, 0x22);

static const ble_uuid128_t s_chr_audio_uuid =
    BLE_UUID128_INIT(0xf3, 0xe4, 0xaf, 0xc8, 0xeb, 0x66, 0x98, 0xab,
                     0xcc, 0x55, 0x49, 0xd5, 0x61, 0xe3, 0xd5, 0x54);

static const ble_uuid128_t s_chr_ctrl_uuid =
    BLE_UUID128_INIT(0xc0, 0x62, 0xb1, 0xc6, 0xbb, 0xec, 0x6c, 0xb5,
                     0x0d, 0x5d, 0xd5, 0x5c, 0xf7, 0x07, 0xbe, 0x14);

static const ble_uuid128_t s_chr_event_uuid =
    BLE_UUID128_INIT(0x9c, 0xc7, 0x57, 0xae, 0xb3, 0xa1, 0x2f, 0xad,
                     0xb5, 0x53, 0xa0, 0x43, 0x2f, 0x04, 0xd7, 0xa1);

#define DEVICE_NAME "StickS3"
#define PREFERRED_MTU 247 /* 冻结 §2：首选 247；<65 时会话层拒绝开始 */

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_audio_val_handle;
static uint16_t s_event_val_handle;
static bool s_audio_subscribed;
static bool s_event_subscribed;
static uint16_t s_link_mtu;
static uint16_t s_host_mtu;   /* READY{mtu} 上报值 */
static uint8_t s_heartbeat_seq;
static uint8_t s_state_snapver;
static uint8_t s_state_bitmap; /* 临时位图：bit0=busy bit1=valid（M4 冻结） */
static ble_gatt_ctrl_cb_t s_ctrl_cb;

static int chr_audio_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);
static int chr_ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int chr_event_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static const struct ble_gatt_chr_def s_chrs[] = {
    {.uuid = &s_chr_audio_uuid.u,
     .access_cb = chr_audio_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
     .val_handle = &s_audio_val_handle},
    {.uuid = &s_chr_ctrl_uuid.u,
     .access_cb = chr_ctrl_access,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC},
    {.uuid = &s_chr_event_uuid.u,
     .access_cb = chr_event_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
     .val_handle = &s_event_val_handle},
    {0},
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &s_svc_uuid.u,
     .characteristics = s_chrs},
    {0},
};

bool ble_gatt_connected(void) {
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

uint16_t ble_gatt_mtu(void) {
    return s_link_mtu;
}

bool ble_gatt_audio_subscribed(void) {
    return s_audio_subscribed;
}

void ble_gatt_set_ctrl_cb(ble_gatt_ctrl_cb_t cb) {
    s_ctrl_cb = cb;
}

bool ble_gatt_host_state_valid(void) {
    return (s_state_bitmap & 0x02u) != 0u;
}

bool ble_gatt_host_busy(void) {
    return (s_state_bitmap & 0x01u) != 0u;
}

static esp_err_t notify_value(uint16_t val_handle, bool subscribed,
                              const uint8_t *data, size_t len) {
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !subscribed) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const int rc = ble_gatts_notify_custom(s_conn_handle, val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_gatt_notify_audio(const uint8_t *data, size_t len) {
    return notify_value(s_audio_val_handle, s_audio_subscribed, data, len);
}

esp_err_t ble_gatt_notify_event(const uint8_t *data, size_t len) {
    return notify_value(s_event_val_handle, s_event_subscribed, data, len);
}

static int chr_audio_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* 读返回空值：本特征只承载 Notify。 */
    (void)conn_handle; (void)attr_handle; (void)arg;
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR ? 0 : BLE_ATT_ERR_UNLIKELY;
}

static int chr_event_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR ? 0 : BLE_ATT_ERR_UNLIKELY;
}

/* Control Write（Write Without Response）：首字节 msgType，冻结 §3.2。 */
static int chr_ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t buf[8] = {0};
    uint16_t copy = om_len < sizeof(buf) ? om_len : (uint16_t)sizeof(buf);
    ble_hs_mbuf_to_flat(ctxt->om, buf, copy, NULL);

    switch (buf[0]) {
    case 0x10: /* READY{protocolVersion u8, mtu u16 LE}，共 4 字节 */
        if (om_len != 4) {
            ESP_LOGW(TAG, "READY 长度异常: %u", om_len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_host_mtu = (uint16_t)(buf[2] | (buf[3] << 8));
        ESP_LOGI(TAG, "host READY: protoVer=%u mtu=%u", buf[1], s_host_mtu);
        if (s_ctrl_cb) {
            s_ctrl_cb(buf[0], buf, om_len);
        }
        break;
    case 0x11: /* HEARTBEAT{seq u8}，共 2 字节 */
        if (om_len != 2) {
            ESP_LOGW(TAG, "HEARTBEAT 长度异常: %u", om_len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_heartbeat_seq = buf[1];
        ESP_LOGI(TAG, "host HEARTBEAT: seq=%u", s_heartbeat_seq);
        break;
    case 0x12: /* STATE{快照版本 u8 + 状态位图 u8}，共 4 字节。
                * 位图语义为 M2 纵切草案（bit0=busy bit1=valid），M4 冻结。 */
        if (om_len != 4) {
            ESP_LOGW(TAG, "STATE 长度异常: %u", om_len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        s_state_snapver = buf[2];
        s_state_bitmap = buf[3];
        ESP_LOGI(TAG, "host STATE snapver=%u bitmap=0x%02x (%s)",
                 s_state_snapver, s_state_bitmap,
                 (s_state_bitmap & 0x02u) ? ((s_state_bitmap & 0x01u) ? "BUSY" : "IDLE") : "UNKNOWN");
        break;
    default:
        ESP_LOGW(TAG, "未知控制消息 msgType=0x%02x", buf[0]);
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

static void start_advertising(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    const int rc_fields = ble_gap_adv_set_fields(&fields);
    if (rc_fields != 0) {
        ESP_LOGE(TAG, "adv set fields: %d", rc_fields);
        return;
    }
    struct ble_hs_adv_fields rsp = {0};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (const uint8_t *)name;
    rsp.name_len = (uint8_t)strlen(name);
    rsp.name_is_complete = 1;
    const int rc_rsp = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc_rsp != 0) {
        ESP_LOGE(TAG, "adv rsp fields: %d", rc_rsp);
        return;
    }
    struct ble_gap_adv_params advp = {0};
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    advp.itvl_min = BLE_GAP_ADV_ITVL_MS(20);
    advp.itvl_max = BLE_GAP_ADV_ITVL_MS(30);
    const int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                     &advp, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising: svc %s", "22587f80-334a-53d6-beb8-8a6ace31af07");
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "connect failed: %d", event->connect.status);
            start_advertising();
            return 0;
        }
        s_conn_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "connected, conn=%u（等待主机发起 Just Works 配对）",
                 s_conn_handle);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* 契约 BLE-01/02：陌生新键不被静默替换——删除旧键后重新配对。 */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        ESP_LOGW(TAG, "repeat pairing conn=%u; 旧 bond 已删",
                 event->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGI(TAG, "disconnected reason=0x%02x; 重新广播",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_audio_subscribed = false;
        s_event_subscribed = false;
        s_link_mtu = 0;
        s_host_mtu = 0;
        s_state_bitmap = 0; /* 断开后主机状态失效 → 屏幕回落 UNKNOWN */
        start_advertising();
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "enc change conn=%u status=%d", event->enc_change.conn_handle,
                 event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_MTU:
        s_link_mtu = event->mtu.value;
        ESP_LOGI(TAG, "MTU exchange: %u (conn=%u)", event->mtu.value,
                 event->mtu.conn_handle);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_audio_val_handle) {
            s_audio_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "audio notify %s", s_audio_subscribed ? "ON" : "off");
        } else if (event->subscribe.attr_handle == s_event_val_handle) {
            s_event_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "event notify %s", s_event_subscribed ? "ON" : "off");
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void) {
    const int rc = ble_att_set_preferred_mtu(PREFERRED_MTU);
    ESP_LOGI(TAG, "host synced; preferred MTU %d (rc=%d)", PREFERRED_MTU, rc);
    start_advertising();
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "host reset: %d", reason);
}

static void host_task(void *param) {
    nimble_port_run(); /* 返回即 host 停止 */
    nimble_port_freertos_deinit();
    (void)param;
}

esp_err_t ble_gatt_init(void) {
    /* PHY 校准与 bond 存储都依赖 NVS，必须在控制器启动前就绪。 */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_err = nvs_flash_erase() == ESP_OK ? nvs_flash_init() : nvs_err;
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs flash init: %s", esp_err_to_name(nvs_err));
        return nvs_err;
    }
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble port init: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    /* bond 持久化：NVS_PERSIST 已开，默认重试策略把缺失记录写入 NVS */
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    /* 冻结 §1：Bond=1、MITM=0（Just Works）、LE Secure Connections。
     * sm_bonding 必须显式置 1——NimBLE 默认 0 会让 Windows 配对直接 FAILED。 */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    const int rc_name = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc_name != 0) {
        ESP_LOGE(TAG, "gap name set: %d", rc_name);
        return ESP_FAIL;
    }
    const int rc_svcs = ble_gatts_count_cfg(s_svcs) | ble_gatts_add_svcs(s_svcs);
    if (rc_svcs != 0) {
        ESP_LOGE(TAG, "gatts register: %d", rc_svcs);
        return ESP_FAIL;
    }
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "nimble host started (bond=1, MITM=0, MTU<=%d)", PREFERRED_MTU);
    return ESP_OK;
}
