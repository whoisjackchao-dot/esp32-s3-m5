// M4 S4.2 —— STATE(0x12) 编解码实现。依据契约 v1.1。
#include "s3p_state.h"

#include <string.h>

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// UTF-8 码点安全截断：返回可放入 cap 的最大字节数（不切断多字节序列）。
static size_t utf8_clip(const char *s, size_t cap) {
    const size_t n = strlen(s);
    if (n <= cap) {
        return n;
    }
    size_t i = 0;
    while (i < cap) {
        const unsigned char c = (unsigned char)s[i];
        const size_t seq = (c < 0x80u) ? 1u : (c < 0xE0u) ? 2u : (c < 0xF0u) ? 3u : 4u;
        if (i + seq > cap) {
            break;
        }
        i += seq;
    }
    return i;
}

size_t s3p_state_encode(const s3p_state_snapshot *s, uint8_t *out, size_t cap) {
    if (!s || !out || cap < S3P_STATE_FIXED_LEN) {
        return 0;
    }
    out[0] = S3P_STATE_MSG_TYPE;
    out[1] = S3P_STATE_VERSION;
    out[2] = s->status;
    out[3] = s->flags;
    wr_u32(out + 4, s->revision);
    out[8] = s->tool_count;
    out[9] = s->todo_completed;
    out[10] = s->todo_total;
    wr_u32(out + 11, s->tokens_input);
    wr_u32(out + 15, s->tokens_output);
    wr_u32(out + 19, s->cost_milli);
    wr_u32(out + 23, s->tokens_reasoning);
    wr_u32(out + 27, s->tokens_cache_read);
    size_t len = S3P_STATE_FIXED_LEN;

    const uint8_t tags[3] = {0x01u, 0x02u, 0x03u};
    const char *texts[3] = {s->project, s->title, s->model};
    const uint8_t bits[3] = {S3P_FLAG_HAS_PROJECT, S3P_FLAG_HAS_TITLE, S3P_FLAG_HAS_MODEL};
    for (int i = 0; i < 3; ++i) {
        if ((s->flags & bits[i]) == 0u) {
            continue;
        }
        const size_t tlen = utf8_clip(texts[i], S3P_STATE_MAX_TEXT);
        if (tlen == 0) {
            continue;
        }
        if (len + 2u + tlen > cap) {
            return 0;
        }
        out[len++] = tags[i];
        out[len++] = (uint8_t)tlen;
        memcpy(out + len, texts[i], tlen);
        len += tlen;
    }
    return len;
}

bool s3p_state_decode(const uint8_t *in, size_t len, s3p_state_snapshot *out) {
    if (!in || !out || len < S3P_STATE_FIXED_LEN) {
        return false;
    }
    if (in[0] != S3P_STATE_MSG_TYPE || in[1] != S3P_STATE_VERSION) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->status = in[2];
    out->flags = in[3];
    out->revision = rd_u32(in + 4);
    out->tool_count = in[8];
    out->todo_completed = in[9];
    out->todo_total = in[10];
    out->tokens_input = rd_u32(in + 11);
    out->tokens_output = rd_u32(in + 15);
    out->cost_milli = rd_u32(in + 19);
    out->tokens_reasoning = rd_u32(in + 23);
    out->tokens_cache_read = rd_u32(in + 27);

    size_t off = S3P_STATE_FIXED_LEN;
    while (off + 2u <= len) {
        const uint8_t tag = in[off];
        const uint8_t tlen = in[off + 1u];
        off += 2u;
        if (off + tlen > len) {
            return false;
        }
        char *dst = NULL;
        if (tag == 0x01u) {
            dst = out->project;
        } else if (tag == 0x02u) {
            dst = out->title;
        } else if (tag == 0x03u) {
            dst = out->model;
        }
        if (dst) {
            const size_t c = tlen > S3P_STATE_MAX_TEXT ? S3P_STATE_MAX_TEXT : tlen;
            memcpy(dst, in + off, c);
            dst[c] = '\0';
        }
        off += tlen;
    }
    return true;
}

// S4.4+ —— 状态 -> 提示音声数策略。
uint8_t s3p_alert_beep_count(uint8_t status) {
    if (status == S3P_STATUS_WAITING) {
        return S3P_ALERT_BEEPS_WAITING;
    }
    if (status == S3P_STATUS_ERROR) {
        return S3P_ALERT_BEEPS_ERROR;
    }
    return 0u;
}
