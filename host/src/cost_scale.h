#pragma once
// M4 —— 用量 cost(浮点计价单位) → 毫单位整数（纯 C，可单测）。
// 契约字段 cost_milli 语义为「费用 × 1000」（contracts/M4-STATE快照冻结.md §1，offset 19）。
#include <stdint.h>
#include "s3p_state.h"

// 四舍五入到毫单位；负值归 0；超上限夹到哨兵下方(0xFFFFFFFD)，避免撞 UNKNOWN/NOT_REPORTED。
static inline uint32_t cost_to_milli(double value) {
    if (value < 0.0) return 0u;
    const double milli = value * 1000.0;
    if (milli >= (double)S3P_STATE_NOT_REPORTED) return (uint32_t)S3P_STATE_NOT_REPORTED - 1u;
    return (uint32_t)(milli + 0.5);
}
