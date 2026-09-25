/**
 * M02 BLE GATT 服务端（NimBLE 外设）——按 contracts/M2-BLE线协议冻结.md §1/§2。
 * 服务 22587f80-334a-53d6-beb8-8a6ace31af07；三特征全部要求加密链路（Just Works SC 配对）。
 * 本增量：广播 + GATT 表 + MTU + READY/HEARTBEAT 控制面收发；音频会话流（BEGIN/AUDIO/END）
 * 由 Opus 编码增量接入，经 ble_gatt_notify_audio() 下发。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "s3p_state.h"

esp_err_t ble_gatt_init(void);

/* 控制面回调：收到合法 Control Write（msgType 0x10/0x11/0x12）后由 GATT 层调用。
 * 用于 READY 触发音频会话，避免 ble_gatt 反向依赖会话层。 */
typedef void (*ble_gatt_ctrl_cb_t)(uint8_t msg_type, const uint8_t *data, uint16_t len);
void ble_gatt_set_ctrl_cb(ble_gatt_ctrl_cb_t cb);

/* 链路断开回调：会话层据此立即中止在途语音事务（Q05 不重放）。 */
typedef void (*ble_gatt_disconnect_cb_t)(void);
void ble_gatt_set_disconnect_cb(ble_gatt_disconnect_cb_t cb);

bool ble_gatt_connected(void);
uint16_t ble_gatt_mtu(void);
bool ble_gatt_audio_subscribed(void);

/* 主机 STATE(0x12) 快照（M2 纵切临时位图，M4 冻结） */
bool ble_gatt_host_state_valid(void);
bool ble_gatt_host_busy(void);
uint8_t ble_gatt_host_status(void);
/* STATE 位图（含 S3P_FLAG_EXPIRED）；无效时为 0。 */
uint8_t ble_gatt_host_flags(void);
/* 最近一次 STATE 完整快照；无有效快照时返回 NULL。 */
const s3p_state_snapshot *ble_gatt_host_snapshot(void);

/* 设备→主机通知（Audio Notify / Device Event 特征）。
 * 未连接或未订阅时返回 ESP_ERR_INVALID_STATE，不静默丢弃调用。 */
esp_err_t ble_gatt_notify_audio(const uint8_t *data, size_t len);
esp_err_t ble_gatt_notify_event(const uint8_t *data, size_t len);
