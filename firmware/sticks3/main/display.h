/**
 * M2 状态屏（硬件证据增量）：ST7789P3 135×240 SPI + M5PM1(0x6E) L3B 供电轨。
 * 引脚/偏移/反色均按 M5StickS3_硬件知识文档.md §3.1/§3.2（M5GFX 实测值）。
 * 本增量：点亮 + 自检图案（用户目视确认硬件通路）+ 状态底色轮询；
 * 富文本 UI 按《语音状态屏》设计文档后续增量进入。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t display_init(void);

/* 请求切到下一页（M4-S4.6 侧键短按用；由显示任务在下次轮询处理） */
void display_next_page(void);

/* 背光通断（M4-S4.5 息屏用；BL=GPIO38） */
void display_backlight(bool on);

/* 电池电量（M4-S4.5）：由电源任务周期更新，状态页底部显示 */
void display_set_battery(int pct, bool charging);
