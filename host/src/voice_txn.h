#ifndef VOICE_TXN_H
#define VOICE_TXN_H

/* M05 语音事务协调——单事务状态机（T 类，Selective TDD）。
 * 冻结行为：单事务（TX-03 全状态拒新）；失败内容保留 5 分钟不滑动
 * （TX-05/06）；ASR 20s 超时→待重试且自动请求数仍 1（TX-07）；手动重试
 * 恰好多一次调用、新尝试 ID（TX-08）；填入重试不再调 ASR（TX-09）；
 * 空结果/过短样本丢弃、ASR 请求数 0（TX-18）；取消后迟到回调拒绝
 * （TX-10/C03）。时钟由调用方注入（单调毫秒，TX-14 由宿主保证）。 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define VT_TTL_MS 300000     /* 失败内容保留 5 分钟（冻结，不滑动） */
#define VT_MIN_SAMPLES 4800u /* TX-18：低于此丢弃，ASR 请求数 0 */
#define VT_TEXT_CAP 4096     /* 保留文本上界（有界内存） */

typedef enum {
    VT_IDLE = 0,
    VT_RECORDING,
    VT_DRAINING,       /* 尾帧等待（松键后，样本已达标） */
    VT_TRANSCRIBING,   /* ASR 在途 */
    VT_RETRY_PENDING,  /* ASR 失败/超时，内容保留待手动重试 */
    VT_APPEND_PENDING, /* ASR 成功，等待/重试填入 */
    VT_FILLED          /* 已填入草稿，待用户手动发送 */
} vt_state_t;

typedef enum {
    VT_OK = 0,
    VT_CLEANED = 1,        /* tick：到期内容已清理（信息性） */
    VT_ERR_NO_TXN = -1,    /* 无事务/已清理（含取消后迟到回调） */
    VT_ERR_ACTIVE = -2,    /* 已有事务在身（TX-03 拒新） */
    VT_ERR_WRONG_STATE = -3,
    VT_ERR_PARAM = -4
} vt_status_t;

typedef struct {
    vt_state_t state;
    uint32_t txn_id;      /* 事务 ID，单调递增 */
    uint32_t attempt_id;  /* ASR 尝试 ID，每次调用递增（TX-08） */
    uint32_t samples;
    int asr_calls;        /* 本事务 ASR 请求数（自动重试为 0，TX-07） */
    int64_t expiry_ms;    /* RETRY_PENDING 清理时刻；其余 0 */
    char text[VT_TEXT_CAP];
    uint32_t cancelled_id; /* 取消墓碑：迟到回调按 ID 拒绝 */
} vt_txn_t;

void vt_init(vt_txn_t *vt);

/* 生命周期（时钟为注入的单调毫秒） */
vt_status_t vt_start_recording(vt_txn_t *vt, int64_t now);
vt_status_t vt_stop_recording(vt_txn_t *vt, int64_t now, uint32_t samples);
vt_status_t vt_asr_start(vt_txn_t *vt, int64_t now);
vt_status_t vt_asr_result(vt_txn_t *vt, int64_t now, const char *text_utf8);
vt_status_t vt_asr_failed(vt_txn_t *vt, int64_t now); /* → RETRY_PENDING */
vt_status_t vt_retry_asr(vt_txn_t *vt, int64_t now);  /* 新尝试，TTL 不滑动 */
vt_status_t vt_append_applied(vt_txn_t *vt);          /* 填入成功 → FILLED */
vt_status_t vt_cancel(vt_txn_t *vt, int64_t now);
vt_status_t vt_finish(vt_txn_t *vt, int64_t now);     /* FILLED → IDLE（草稿保留） */
vt_status_t vt_tick(vt_txn_t *vt, int64_t now);       /* 惰性到期清理 */

/* 查询 */
vt_state_t vt_state(const vt_txn_t *vt);
uint32_t vt_txn_id(const vt_txn_t *vt);
uint32_t vt_attempt_id(const vt_txn_t *vt);
int64_t vt_expiry_ms(const vt_txn_t *vt);
int vt_asr_calls(const vt_txn_t *vt);
uint32_t vt_samples(const vt_txn_t *vt);
const char *vt_text(const vt_txn_t *vt);
uint32_t vt_cancelled_id(const vt_txn_t *vt);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_TXN_H */
