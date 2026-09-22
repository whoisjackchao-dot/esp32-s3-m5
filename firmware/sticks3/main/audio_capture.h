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

/* I²S RX 通道可用（init 成功后才为 true；供会话层门控） */
bool audio_capture_ready(void);

/* 阻塞读取一帧采样（16bit PCM，samples 个样本）。read_out 返回实际样本数。 */
esp_err_t audio_capture_read(int16_t *samples, size_t count, size_t *read_out);
