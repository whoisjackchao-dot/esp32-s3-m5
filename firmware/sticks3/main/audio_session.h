/**
 * M02 音频会话流：采音（audio_capture）→ Opus 编码 → S3P BEGIN/AUDIO/END 经 GATT 下发。
 * 触发：主机 READY（冻结 §3.2，M2 仅要求 READY/HEARTBEAT 可往返）+ Audio Notify 已订阅。
 * 会话参数冻结 §3.2：16kHz/60ms/960 样本每帧；END.summary = FNV-1a(全部 Opus 载荷字节,
 * seed 0x811c9dc5)——主机按黄金向量要求独立计算比对（TEST-API-02）。
 * MTU < 65 显式拒绝（ERROR 事件 + 不发 BEGIN），不得静默截断（冻结 §2）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_session_init(void);

/* 会话进行中（供状态屏轮询） */
bool audio_session_active(void);

/* ble_gatt 控制面回调入口：msgType 0x10(READY) 请求启动一次录音会话 */
void audio_session_on_ctrl(uint8_t msg_type, const uint8_t *data, uint16_t len);
