/**
 * M5PM1（I²C 0x6E）电源管理最小驱动——寄存器语义取自 M5GFX/M5Unified 源码与官方 PinMap：
 *   REG 0x00 DEVICE_ID / 0x06 PWR_CFG / 0x09 I2C_CFG / 0x0A WDT_CNT
 *   REG 0x10 GPIO_MODE / 0x11 GPIO_OUT / 0x13 GPIO_DRV / 0x16 GPIO_FUNC
 * 关键事实（2026-09-22 源码核实，纠正此前误记）：
 *   PM1 GPIO2 = L3B 供电轨（LCD/MIC/SPK；ES8311 编解码器所在）；PM1 GPIO3 = SPK 功放使能。
 *   引脚驱动须依次配置 0x16(功能=GPIO) / 0x10(模式=输出) / 0x13(推挽) / 0x11(电平)；
 *   只写 0x11 而不配前三者，引脚不会真正驱动（实测：漏配 → ES8311 无供电 NACK、屏幕空白）。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t pm1_init(void);

/* GPIO_OUT(0x11) 位置位/清零（RMW，不动其他位） */
esp_err_t pm1_bit(uint8_t bit, bool on);

/* SPK 功放使能（PM1 GPIO3） */
esp_err_t pm1_es8311(bool on);

/* L3B 供电轨（PM1 GPIO2）：LCD/麦克风/扬声器，ES8311 供电所在 */
esp_err_t pm1_l3b_lcd(bool on);
