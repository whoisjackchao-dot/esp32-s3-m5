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

esp_err_t ble_gatt_init(void);

bool ble_gatt_connected(void);
uint16_t ble_gatt_mtu(void);
bool ble_gatt_audio_subscribed(void);

/* 设备→主机通知（Audio Notify / Device Event 特征）。
 * 未连接或未订阅时返回 ESP_ERR_INVALID_STATE，不静默丢弃调用。 */
esp_err_t ble_gatt_notify_audio(const uint8_t *data, size_t len);
esp_err_t ble_gatt_notify_event(const uint8_t *data, size_t len);
