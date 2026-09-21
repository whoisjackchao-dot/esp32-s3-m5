/**
 * s3p_reasm —— 音频帧重组状态机（主机侧 M04，纯逻辑、无分配、无系统依赖）。
 *
 * 冻结依据：contracts/M2-BLE线协议冻结.md §4 重组规则；测试矩阵 BLE-05..15。
 * 会话模型：单事务（设计：单连接单录音）；create 返回静态单例指针。
 * 所有缓冲由调用方静态提供；容量耗尽显式失败（BLE-15，不静默丢弃）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S3P_REASM_MAX_FRAMES 500u               /* 30s / 60ms（契约 §3.2） */
#define S3P_FRAME_DRAIN_TIMEOUT_MS 2000u        /* 尾帧等待（C07 第 6 条） */

typedef enum {
    S3P_REASM_OK = 0,
    S3P_REASM_DUPLICATE = 1,     /* 同片同内容重复：忽略 */
    S3P_REASM_CONFLICT = 2,      /* 同片内容冲突：整段失败（BLE-08） */
    S3P_REASM_SEQ_OVERFLOW = 3,  /* frameSeq 超出宣告（BLE-13） */
    S3P_REASM_CAPACITY = 4,      /* 容量耗尽：显式失败（BLE-15） */
    S3P_REASM_FOREIGN = 5,       /* recordingID 不符：丢弃（BLE-14） */
    S3P_REASM_NO_SESSION = 6,    /* 未 begin / 会话已终态 */
    S3P_REASM_WAITING = 7,       /* END 已到、缺片等待尾帧窗口 */
    S3P_REASM_FAILED = 8,        /* 会话失败（终态） */
} s3p_reasm_rc_t;

typedef enum {
    S3P_REASM_STATE_IDLE = 0,
    S3P_REASM_STATE_ACTIVE = 1,
    S3P_REASM_STATE_COMPLETE = 2,
    S3P_REASM_STATE_FAILED = 3,
} s3p_reasm_state_t;

typedef struct s3p_reasm s3p_reasm_t;

/* 绑定外部数据缓冲并复位为 IDLE。单例；重复 create 即复位。 */
s3p_reasm_t *s3p_reasm_create(uint8_t *buffer, size_t capacity);
void s3p_reasm_destroy(s3p_reasm_t *r);

/* 开始新录音会话：清空位图/偏移/哈希槽，状态 → ACTIVE */
s3p_reasm_rc_t s3p_reasm_begin(s3p_reasm_t *r, uint32_t recording_id, uint16_t frame_count, uint16_t sample_rate);

/* 喂入一个 AUDIO 分片（payload 不拷贝进额外结构：按到达序追加至数据缓冲） */
s3p_reasm_rc_t s3p_reasm_feed(s3p_reasm_t *r, uint32_t recording_id, uint32_t frame_seq,
                              uint8_t frag_index, uint8_t frag_count,
                              const uint8_t *payload, uint16_t len);

/* END 到达：与 BEGIN 宣告/实际接收一致性裁决（BLE-12）。
 * 完整 → COMPLETE/OK；不完整 → WAITING（尾帧窗口启动）；
 * END 声明与 BEGIN/实际不符 → FAILED。 */
s3p_reasm_rc_t s3p_reasm_end(s3p_reasm_t *r, uint32_t recording_id, uint32_t total_frames,
                             uint32_t total_samples, uint32_t summary);

/* 尾帧窗口裁决：WAITING 态下 elapsed_ms ≥ FRAME_DRAIN_TIMEOUT → FAILED（BLE-06/11） */
s3p_reasm_rc_t s3p_reasm_on_timeout(s3p_reasm_t *r, uint32_t elapsed_ms);

bool s3p_reasm_is_complete(const s3p_reasm_t *r);
s3p_reasm_state_t s3p_reasm_state(const s3p_reasm_t *r);
uint32_t s3p_reasm_received_frames(const s3p_reasm_t *r);
size_t s3p_reasm_total_bytes(const s3p_reasm_t *r);

/* 按 frameSeq 读出帧内容（到达序存储、按 seq 读出）；返回拷贝长度，无此帧返回 0 */
size_t s3p_reasm_read_frame(const s3p_reasm_t *r, uint32_t frame_seq, uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
