/**
 * M01 麦克风采音通路：ES8311（I²C 0x18）+ I²S 标准 RX。
 * 引脚映射 HW-01/HW-04：MCLK=18 BCLK=17 WS=15 DIN=16 SDA=47 SCL=48。
 * 16kHz / 16bit / 单声道（ES8311 左槽输出）。
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

esp_err_t audio_capture_init(void);

/* 诊断：打印 I2S 驱动实际配置的源时钟/MCLK/BCLK（时基排查用） */
void audio_capture_log_clocks(void);

/* 诊断：实测 I2S 每秒实际交付的样本数（判定 WS 真实速率） */
uint32_t audio_capture_probe_rate(uint32_t window_ms);

/* I²S RX 通道可用（init 成功后才为 true；供会话层门控） */
bool audio_capture_ready(void);

/* 阻塞读取一帧采样（16bit PCM，samples 个样本）。read_out 返回实际样本数。 */
esp_err_t audio_capture_read(int16_t *samples, size_t count, size_t *read_out);
