#pragma once
// M4 S4.2 —— STATE(0x12) 快照载荷编解码（固件/主机共享，纯 C）。
// 依据冻结契约 contracts/M4-STATE快照冻结.md v1.1；小端。固定段 31B，snapshotVersion=2。
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define S3P_STATE_MSG_TYPE 0x12u
#define S3P_STATE_VERSION 2u
#define S3P_STATE_FIXED_LEN 31u
#define S3P_STATE_MAX_TEXT 32u
#define S3P_STATE_MAX_LEN (S3P_STATE_FIXED_LEN + 3u * (2u + S3P_STATE_MAX_TEXT))

// 数值哨兵（未知/未报告不得伪装为 0）
#define S3P_STATE_UNKNOWN 0xFFFFFFFFu
#define S3P_STATE_NOT_REPORTED 0xFFFFFFFEu

typedef enum {
    S3P_STATUS_UNKNOWN = 0,
    S3P_STATUS_IDLE = 1,
    S3P_STATUS_BUSY = 2,
    S3P_STATUS_RETRY = 3,
    S3P_STATUS_WAITING = 4,
    S3P_STATUS_ERROR = 5
} s3p_state_status;

// 提示音声数策略（供固件 alert 任务与 host 单测共享）。
#define S3P_ALERT_BEEPS_WAITING 2u
#define S3P_ALERT_BEEPS_ERROR 1u
uint8_t s3p_alert_beep_count(uint8_t status);

#define S3P_FLAG_CAN_APPEND (1u << 0)
#define S3P_FLAG_EXPIRED (1u << 1)
#define S3P_FLAG_HAS_PROJECT (1u << 2)
#define S3P_FLAG_HAS_TITLE (1u << 3)
#define S3P_FLAG_HAS_MODEL (1u << 4)
#define S3P_FLAG_HAS_USAGE (1u << 5)
#define S3P_FLAG_TOOL_FAILED (1u << 6)
#define S3P_FLAG_CHILD_BUSY (1u << 7)

typedef struct {
    uint8_t status;  // s3p_state_status
    uint8_t flags;
    uint32_t revision;
    uint8_t tool_count;
    uint8_t todo_completed;
    uint8_t todo_total;
    uint32_t tokens_input;
    uint32_t tokens_output;
    uint32_t cost_milli;
    uint32_t tokens_reasoning;
    uint32_t tokens_cache_read;
    char project[S3P_STATE_MAX_TEXT + 1];
    char title[S3P_STATE_MAX_TEXT + 1];
    char model[S3P_STATE_MAX_TEXT + 1];
} s3p_state_snapshot;

// 编码为完整载荷（含首字节 msgType=0x12）。返回字节数；cap 不足或参数非法返回 0。
size_t s3p_state_encode(const s3p_state_snapshot *s, uint8_t *out, size_t cap);

// 解码完整载荷。版本错/长度不足/TLV 越界返回 false。
bool s3p_state_decode(const uint8_t *in, size_t len, s3p_state_snapshot *out);
