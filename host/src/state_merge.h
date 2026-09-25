#pragma once
// M4 S4.2 —— BR-03 精简快照 → STATE(0x12) 字段映射（“合并”）。
// 纯逻辑，与 host 进程形态无关；输出可直接交 s3p_state_encode。
// 依据 contracts/M4-STATE快照冻结.md v1.0 与 HLD §7.4。
#include <stdint.h>

#include "s3p_state.h"

typedef struct {
    uint8_t status;  // S3P_STATUS_*
    uint8_t can_append;
    uint8_t expired;
    uint8_t tool_failed;
    uint8_t child_busy;
    uint32_t revision;
    uint8_t tool_count;
    uint8_t todo_known;  // 0 → 视为无待办（0/0）
    uint8_t todo_completed;
    uint8_t todo_total;
    uint32_t tokens_input;   // 未知用 S3P_STATE_UNKNOWN
    uint32_t tokens_output;  // 未报告用 S3P_STATE_NOT_REPORTED
    uint32_t cost_milli;     // 未知用 S3P_STATE_UNKNOWN
    const char *project;     // NULL/"" = 无
    const char *title;
    const char *model;
} br03_snapshot;

// 映射为 STATE 快照（不含编码）。out 清零后按 §7 字段填充。
void state_merge(s3p_state_snapshot *out, const br03_snapshot *in);
