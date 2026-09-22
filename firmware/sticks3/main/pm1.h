/**
 * M5PM1（I²C 0x6E）电源管理最小驱动——寄存器语义取自 M5Unified 源码
 * （M5PM1_Class.cpp / M5Unified.cpp）：
 *   REG 0x00 DEVICE_ID / 0x09 I2C_CFG / 0x0A WDT_CNT / 0x10 GPIO_MODE / 0x11 GPIO_OUT
 * 关键事实：ES8311 使能 = PM1 GPIO3（0x11 bit3）；LCD L3B 供电 = PM1 GPIO2（0x11 bit2）。
 * M5Unified 每次启动禁用 PM1 看门狗并重新使能 ES8311——不复刻该序列会在
 * PM1 状态复位后失去编码器（实测 ES8311 NACK、I²C 扫描 0x18 消失）。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t pm1_init(void);

/* GPIO_OUT(0x11) 位置位/清零（RMW，不动其他位） */
esp_err_t pm1_bit(uint8_t bit, bool on);

/* ES8311 供电/使能（PM1 GPIO3） */
esp_err_t pm1_es8311(bool on);

/* LCD L3B 供电轨（PM1 GPIO2） */
esp_err_t pm1_l3b_lcd(bool on);
