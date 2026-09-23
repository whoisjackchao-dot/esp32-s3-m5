/**
 * M01 正面按键 BtnA（GPIO11，低电平按下）：按住录音 / 松开结束。
 * 分工依据 Q06（2026-09-15 已批准）：正面按住/松开录音；长按阈值与去抖**待冻结**。
 * 引脚依据 M5StickS3_硬件知识文档 §7.1（官方 PinMap + M5Unified 按键状态逻辑）。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef void (*button_event_cb_t)(void);

/* 配置 GPIO 输入（上拉）并启动去抖轮询任务 */
esp_err_t button_init(void);

bool button_is_pressed(void);

/* 注册按下/松开回调（在 button_init 之后、按键任务运行前设置） */
void button_set_handlers(button_event_cb_t on_press, button_event_cb_t on_release);
