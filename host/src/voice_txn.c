#include "voice_txn.h"

#include <string.h>

void vt_init(vt_txn_t *vt) {
    if (vt == NULL) return;
    memset(vt, 0, sizeof(*vt));
    vt->state = VT_IDLE;
}

vt_state_t vt_state(const vt_txn_t *vt) { return vt == NULL ? VT_IDLE : vt->state; }

uint32_t vt_txn_id(const vt_txn_t *vt) { return vt == NULL ? 0 : vt->txn_id; }

uint32_t vt_attempt_id(const vt_txn_t *vt) { return vt == NULL ? 0 : vt->attempt_id; }

int64_t vt_expiry_ms(const vt_txn_t *vt) { return vt == NULL ? 0 : vt->expiry_ms; }

int vt_asr_calls(const vt_txn_t *vt) { return vt == NULL ? 0 : vt->asr_calls; }

uint32_t vt_samples(const vt_txn_t *vt) { return vt == NULL ? 0 : vt->samples; }

const char *vt_text(const vt_txn_t *vt) { return vt == NULL ? "" : vt->text; }

uint32_t vt_cancelled_id(const vt_txn_t *vt) {
    return vt == NULL ? 0 : vt->cancelled_id;
}

/* 清空在途事务回到 IDLE（含文本清零——有界内存纪律） */
static void vt_reset(vt_txn_t *vt) {
    vt->state = VT_IDLE;
    vt->samples = 0;
    vt->expiry_ms = 0;
    memset(vt->text, 0, sizeof(vt->text));
}

vt_status_t vt_start_recording(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    if (vt->state != VT_IDLE) return VT_ERR_ACTIVE;
    vt->txn_id += 1;
    vt->attempt_id = 0;
    vt->samples = 0;
    vt->asr_calls = 0;
    vt->expiry_ms = 0;
    vt->text[0] = '\0';
    vt->state = VT_RECORDING;
    return VT_OK;
}

vt_status_t vt_stop_recording(vt_txn_t *vt, int64_t now, uint32_t samples) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    if (vt->state != VT_RECORDING) return VT_ERR_WRONG_STATE;
    vt->samples = samples;
    if (samples < VT_MIN_SAMPLES) {
        /* TX-18：过短丢弃，ASR 请求数 0 */
        vt_reset(vt);
        return VT_OK;
    }
    vt->state = VT_DRAINING;
    return VT_OK;
}

vt_status_t vt_asr_start(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    if (vt->state == VT_IDLE) return VT_ERR_NO_TXN;
    if (vt->state != VT_DRAINING) return VT_ERR_WRONG_STATE;
    vt->attempt_id += 1;
    vt->asr_calls += 1; /* 自动重试不存在：每次进入 TRANSCRIBING 恰好一次调用 */
    vt->state = VT_TRANSCRIBING;
    return VT_OK;
}

vt_status_t vt_asr_result(vt_txn_t *vt, int64_t now, const char *text_utf8) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    if (text_utf8 == NULL) return VT_ERR_PARAM;
    if (vt->state != VT_TRANSCRIBING) return VT_ERR_NO_TXN;
    /* TX-18：空结果不填入，事务结束（调用已发生） */
    if (text_utf8[0] == '\0') {
        vt_reset(vt);
        return VT_OK;
    }
    /* 有界拷贝（超长截断——保留文本上界） */
    strncpy(vt->text, text_utf8, sizeof(vt->text) - 1);
    vt->text[sizeof(vt->text) - 1] = '\0';
    vt->state = VT_APPEND_PENDING;
    return VT_OK;
}

vt_status_t vt_asr_failed(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    if (vt->state != VT_TRANSCRIBING) return VT_ERR_WRONG_STATE;
    vt->state = VT_RETRY_PENDING;
    /* TX-06：TTL 以首次失败时刻起算，重试再失败不滑动 */
    if (vt->expiry_ms == 0) {
        vt->expiry_ms = now + VT_TTL_MS;
    }
    return VT_OK;
}

vt_status_t vt_retry_asr(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    if (vt->state == VT_IDLE) return VT_ERR_NO_TXN;
    if (vt->state != VT_RETRY_PENDING) return VT_ERR_WRONG_STATE;
    /* 防御性惰性到期：即便宿主漏 tick 也不复活过期内容 */
    if (now >= vt->expiry_ms) {
        vt_reset(vt);
        return VT_ERR_NO_TXN;
    }
    vt->attempt_id += 1;
    vt->asr_calls += 1;
    vt->state = VT_TRANSCRIBING;
    return VT_OK;
}

vt_status_t vt_append_applied(vt_txn_t *vt) {
    if (vt == NULL) return VT_ERR_PARAM;
    if (vt->state == VT_IDLE) return VT_ERR_NO_TXN;
    if (vt->state != VT_APPEND_PENDING) return VT_ERR_WRONG_STATE;
    vt->state = VT_FILLED;
    return VT_OK;
}

vt_status_t vt_cancel(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    switch (vt->state) {
    case VT_RECORDING:
    case VT_DRAINING:
    case VT_TRANSCRIBING:
    case VT_APPEND_PENDING:
        vt->cancelled_id = vt->txn_id; /* C03 墓碑：迟到回调按 ID 拒绝 */
        vt_reset(vt);
        return VT_OK;
    case VT_IDLE:
        return VT_ERR_NO_TXN;
    case VT_FILLED:
    case VT_RETRY_PENDING:
    default:
        /* FILLED：草稿不撤销（TX-11）；RETRY_PENDING：内容受 TTL 保护，
         * 取消应改走到期清理，不静默丢弃用户刚录的内容 */
        return VT_ERR_WRONG_STATE;
    }
}

vt_status_t vt_finish(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    (void)now;
    if (vt->state == VT_IDLE) return VT_ERR_NO_TXN;
    if (vt->state != VT_FILLED) return VT_ERR_WRONG_STATE;
    vt_reset(vt);
    return VT_OK;
}

vt_status_t vt_tick(vt_txn_t *vt, int64_t now) {
    if (vt == NULL) return VT_ERR_PARAM;
    if (vt->state == VT_RETRY_PENDING && now >= vt->expiry_ms) {
        vt_reset(vt); /* TX-05：到期清理，重试不再可用 */
        return VT_CLEANED;
    }
    return VT_OK;
}
