#include "s3p_reasm.h"

#include <string.h>

/* 单事务设计：静态单例；数据缓冲由调用方提供（create）。 */
struct s3p_reasm {
    uint8_t *data;
    size_t data_capacity;
    size_t append_pos;
    uint32_t recording_id;
    uint16_t frame_count;
    uint16_t sample_rate;
    s3p_reasm_state_t state;
    size_t arena_pos;
    struct frame_meta {
        uint32_t offset;      /* 到达序存储偏移 */
        uint32_t byte_len;
        uint32_t hash_off;    /* 哈希槽在哈希竞技场中的偏移；0xFFFFFFFF = 未初始化 */
        uint16_t frag_count;
        uint16_t frags_received;
        uint8_t initialized;
    } frames[S3P_REASM_MAX_FRAMES];
};

/* 哈希槽竞技场：每帧按宣告 frag_count 分配 frag_count × 4 字节（内容冲突检测用）。
 * 上限 64KiB 覆盖 500 帧 × 17 片（MTU65）最坏情形；耗尽按 CAPACITY 显式失败。 */
static uint8_t g_hash_arena[64 * 1024];

static uint32_t frag_hash(const uint8_t *p, uint16_t len) {
    /* FNV-1a | 1：0 作为"未收到"哨兵（FNV 空串基址 0x811c9dc5 != 0） */
    uint32_t hash = 0x811c9dc5u;
    for (uint16_t i = 0; i < len; i += 1) {
        hash ^= p[i];
        hash = 0x01000193u * hash;
    }
    return hash | 1u;
}

s3p_reasm_t *s3p_reasm_create(uint8_t *buffer, size_t capacity) {
    static struct s3p_reasm instance;
    static uint8_t initialized;
    if (!initialized) {
        initialized = 1;
        instance.state = S3P_REASM_STATE_IDLE;
    }
    instance.data = buffer;
    instance.data_capacity = capacity;
    instance.append_pos = 0;
    instance.recording_id = 0u;
    instance.frame_count = 0u;
    instance.sample_rate = 0u;
    instance.state = S3P_REASM_STATE_IDLE;
    instance.arena_pos = 0;
    memset(instance.frames, 0, sizeof(instance.frames));
    return &instance;
}

void s3p_reasm_destroy(s3p_reasm_t *r) {
    if (r) r->state = S3P_REASM_STATE_IDLE;
}

s3p_reasm_rc_t s3p_reasm_begin(s3p_reasm_t *r, uint32_t recording_id, uint16_t frame_count, uint16_t sample_rate) {
    if (!r) return S3P_REASM_NO_SESSION;
    if (frame_count == 0u || frame_count > S3P_REASM_MAX_FRAMES) return S3P_REASM_CAPACITY;
    memset(r->frames, 0, sizeof(r->frames));
    r->append_pos = 0;
    r->arena_pos = 0;
    r->recording_id = recording_id;
    r->frame_count = frame_count;
    r->sample_rate = sample_rate;
    r->state = S3P_REASM_STATE_ACTIVE;
    return S3P_REASM_OK;
}

s3p_reasm_rc_t s3p_reasm_feed(s3p_reasm_t *r, uint32_t recording_id, uint32_t frame_seq,
                              uint8_t frag_index, uint8_t frag_count,
                              const uint8_t *payload, uint16_t len) {
    if (!r || r->state != S3P_REASM_STATE_ACTIVE) return S3P_REASM_NO_SESSION;
    if (recording_id != r->recording_id) return S3P_REASM_FOREIGN;
    if (frame_seq >= (uint32_t)r->frame_count) return S3P_REASM_SEQ_OVERFLOW;
    if (frag_count == 0u || frag_index >= frag_count) return S3P_REASM_CONFLICT;

    struct frame_meta *meta = &r->frames[frame_seq];

    if (!meta->initialized) {
        /* 首片：分配哈希槽（竞技场耗尽 → CAPACITY 显式失败，BLE-15） */
        if (r->arena_pos + (size_t)frag_count * 4u > sizeof(g_hash_arena)) {
            r->state = S3P_REASM_STATE_FAILED;
            return S3P_REASM_CAPACITY;
        }
        /* 数据容量前置检查：写入前显式失败（BLE-15），不静默丢弃 */
        if (r->append_pos + (size_t)len > r->data_capacity) {
            r->state = S3P_REASM_STATE_FAILED;
            return S3P_REASM_CAPACITY;
        }
        meta->offset = (uint32_t)r->append_pos;
        meta->hash_off = (uint32_t)r->arena_pos;
        meta->frag_count = frag_count;
        meta->initialized = 1u;
        memset(g_hash_arena + r->arena_pos, 0, (size_t)frag_count * 4u);
        r->arena_pos += (size_t)frag_count * 4u;
    } else {
        if (frag_index >= meta->frag_count) return S3P_REASM_CONFLICT;
    }

    uint32_t *slot = (uint32_t *)(g_hash_arena + meta->hash_off) + frag_index;
    const uint32_t hash = frag_hash(payload, len);
    if (*slot != 0u) {
        /* BLE-07/08：同片同内容 → DUPLICATE 忽略；内容冲突 → 整段失败 */
        if (*slot == hash) return S3P_REASM_DUPLICATE;
        r->state = S3P_REASM_STATE_FAILED;
        return S3P_REASM_CONFLICT;
    }

    if (r->append_pos + (size_t)len > r->data_capacity) {
        r->state = S3P_REASM_STATE_FAILED;
        return S3P_REASM_CAPACITY;
    }
    memcpy(r->data + r->append_pos, payload, len);
    r->append_pos += (size_t)len;
    meta->byte_len += (uint32_t)len;
    *slot = hash;
    meta->frags_received += 1;
    return S3P_REASM_OK;
}

s3p_reasm_rc_t s3p_reasm_end(s3p_reasm_t *r, uint32_t recording_id, uint32_t total_frames,
                             uint32_t total_samples, uint32_t summary) {
    (void)summary; /* summary 校验由上层按黄金向量比对后传入 0/占位；重组层不重复计算 */
    if (!r || r->state != S3P_REASM_STATE_ACTIVE) return S3P_REASM_NO_SESSION;
    if (recording_id != r->recording_id) return S3P_REASM_FOREIGN;

    /* BLE-12（2026-09-23 裁决，契约 §3.2/§4）：END.totalFrames 为**权威实际帧数**，
     * 必须 ≤ BEGIN 预告上限——契约 §3.2 明写"松开提前结束以 END 为准"，故预告是上限
     * 而非等值约定。三条例外一律显式失败，保留"反静默截短"：
     *   1) 声明多于预告 → 失败（不得声多）；
     *   2) 已收到 frameSeq ≥ 声明值 → 失败（与声明不一致，不静默丢弃已收数据）；
     *   3) 声明少于预告但 [0, 声明) 有缺片 → 不 COMPLETE（转 WAITING，超时失败）。 */
    if (total_frames > (uint32_t)r->frame_count) {
        r->state = S3P_REASM_STATE_FAILED;
        return S3P_REASM_FAILED;
    }
    for (uint16_t i = (uint16_t)total_frames; i < r->frame_count; i += 1) {
        if (r->frames[i].initialized) {
            r->state = S3P_REASM_STATE_FAILED;
            return S3P_REASM_FAILED;
        }
    }
    r->frame_count = (uint16_t)total_frames; /* END 为权威：收窄有效窗口 */
    if (s3p_reasm_is_complete(r)) {
        /* 冻结契约 §3.2：AUDIO 载荷为 Opus 字节，与 totalSamples（16kHz 采样数）
         * 不同单位，无法对账；END.totalSamples 由上层按 TX-18 阈值校验。
         * 旧 append_pos/2 对账是 PCM 假设残留，与契约冲突（实测 recID=1 会话被误判 FAILED）。 */
        (void)total_samples;
        r->state = S3P_REASM_STATE_COMPLETE;
        return S3P_REASM_OK;
    }
    /* BLE-06/11：缺片 → 进入尾帧等待窗口 */
    return S3P_REASM_WAITING;
}

s3p_reasm_rc_t s3p_reasm_on_timeout(s3p_reasm_t *r, uint32_t elapsed_ms) {
    if (!r) return S3P_REASM_NO_SESSION;
    if (r->state == S3P_REASM_STATE_ACTIVE && elapsed_ms >= S3P_FRAME_DRAIN_TIMEOUT_MS) {
        r->state = S3P_REASM_STATE_FAILED;
        return S3P_REASM_FAILED;
    }
    return S3P_REASM_OK;
}

bool s3p_reasm_is_complete(const s3p_reasm_t *r) {
    if (!r) return false;
    if (r->state == S3P_REASM_STATE_COMPLETE) return true;
    if (r->state != S3P_REASM_STATE_ACTIVE) return false;
    return r->frame_count > 0u && s3p_reasm_received_frames(r) == (uint32_t)r->frame_count;
}

s3p_reasm_state_t s3p_reasm_state(const s3p_reasm_t *r) {
    return r ? r->state : S3P_REASM_STATE_IDLE;
}

uint32_t s3p_reasm_received_frames(const s3p_reasm_t *r) {
    if (!r) return 0u;
    uint32_t count = 0;
    for (uint16_t i = 0; i < r->frame_count; i += 1) {
        const struct frame_meta *meta = &r->frames[i];
        if (meta->initialized && meta->frags_received == meta->frag_count) count += 1;
    }
    return count;
}

size_t s3p_reasm_total_bytes(const s3p_reasm_t *r) {
    return r ? r->append_pos : 0u;
}

size_t s3p_reasm_read_frame(const s3p_reasm_t *r, uint32_t frame_seq, uint8_t *out, size_t out_cap) {
    if (!r || frame_seq >= (uint32_t)r->frame_count) return 0u;
    const struct frame_meta *meta = &r->frames[frame_seq];
    if (!meta->initialized || meta->byte_len == 0u) return 0u;
    const size_t len = meta->byte_len < out_cap ? meta->byte_len : out_cap;
    memcpy(out, r->data + meta->offset, len);
    return len;
}
