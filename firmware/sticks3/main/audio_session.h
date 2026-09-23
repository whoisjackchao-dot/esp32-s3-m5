/**
 * M02 音频会话流：采音（audio_capture）→ Opus 编码 → S3P BEGIN/AUDIO/END 经 GATT 下发。
 * 触发：正面按键 BtnA 按住启动 / 松开提前结束（Q06）；主机 READY(0x10) 仍为等价启动入口。
 * 会话参数冻结 §3.2：16kHz/60ms/960 样本每帧；BEGIN.frameCount 为预告上限 500，
 * 提前结束时 END.totalFrames 为权威实际帧数（≤ 预告，2026-09-23 裁决）。
 * END.summary = FNV-1a(全部 Opus 载荷字节, seed 0x811c9dc5)——主机按黄金向量独立计算
 * 比对（TEST-API-02）。MTU < 65 显式拒绝（ERROR 事件 + 不发 BEGIN），不得静默截断（§2）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_session_init(void);

/* 会话进行中（供状态屏轮询） */
bool audio_session_active(void);

/* 请求启动一次录音会话（按键按下 / READY）；会话非空闲时忽略。 */
void audio_session_request_start(void);

/* 请求提前结束当前会话（按键松开）：以 END 实际帧数为准，不补零、不声多。 */
void audio_session_request_stop(void);

/* ble_gatt 控制面回调入口：msgType 0x10(READY) 请求启动一次录音会话 */
void audio_session_on_ctrl(uint8_t msg_type, const uint8_t *data, uint16_t len);

/* 链路断开时由 BLE 层调用：立即中止在途会话，禁止残余帧经新链路重放（Q05）。 */
void audio_session_abort(void);
