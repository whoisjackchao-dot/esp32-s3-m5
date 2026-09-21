/**
 * StickS3 BLE 线协议（M2 冻结 v1.0）—— 设备端/主机端共享线格式定义。
 *
 * 依据 contracts/M2-BLE线协议冻结.md。所有多字节字段小端。
 * 本头文件为纯定义+纯函数，无 ESP 依赖，可在主机侧独立编译测试。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define S3P_PROTOCOL_VERSION 1u

/* 包类型 */
#define S3P_PKT_BEGIN 0x01u
#define S3P_PKT_AUDIO 0x02u
#define S3P_PKT_END 0x03u
#define S3P_PKT_DEVICE_CANCEL 0x04u
#define S3P_PKT_ERROR 0x05u

/* 错误码（ERROR 包） */
#define S3P_ERR_OPUS 1u
#define S3P_ERR_BUFFER 2u
#define S3P_ERR_MIC_INIT 3u
#define S3P_ERR_PROTOCOL 4u

/* 采样与帧参数（已冻结） */
#define S3P_SAMPLE_RATE 16000u
#define S3P_FRAME_MS 60u
#define S3P_SAMPLES_PER_FRAME (S3P_SAMPLE_RATE * S3P_FRAME_MS / 1000u) /* 960 */
#define S3P_MAX_FRAMES 500u                     /* 30 秒硬上限 */
#define S3P_MIN_VALID_SAMPLES 4800u             /* 300ms 短录音阈值 */
#define S3P_MAX_RECORD_BYTES (256u * 1024u)     /* 单段 Opus 载荷上限 */
#define S3P_MIN_MTU 65u                         /* 低于此值拒绝录音（不截断） */
#define S3P_FRAME_DRAIN_TIMEOUT_MS 2000u

/* 包头偏移（共 2 字节） */
#define S3P_HDR_TYPE 0u
#define S3P_HDR_VERSION 1u

/* BEGIN 包长 = 2 + 12 */
#define S3P_BEGIN_LEN 14u
/* AUDIO 头 = 2 + 12 = 14，载荷跟随 */
#define S3P_AUDIO_HDR_LEN 14u
/* END 包长 = 2 + 16 */
#define S3P_END_LEN 18u

/* --- 小端写入/读取原语 --- */
static inline void s3p_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}
static inline void s3p_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static inline uint16_t s3p_get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t s3p_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* --- BEGIN 序列化：写入 14 字节，返回总长 --- */
static inline size_t s3p_encode_begin(uint8_t *out, uint32_t recording_id, uint16_t frame_count) {
    out[S3P_HDR_TYPE] = S3P_PKT_BEGIN;
    out[S3P_HDR_VERSION] = (uint8_t)S3P_PROTOCOL_VERSION;
    s3p_put_u32(out + 2, recording_id);
    s3p_put_u16(out + 6, frame_count);
    s3p_put_u16(out + 8, (uint16_t)S3P_SAMPLE_RATE);
    out[10] = (uint8_t)S3P_FRAME_MS;
    out[11] = 0u;
    return S3P_BEGIN_LEN;
}

/* --- AUDIO 序列化：写入 14 字节头，返回头长；payload 由调用方跟随 --- */
static inline size_t s3p_encode_audio_header(uint8_t *out, uint32_t recording_id, uint32_t frame_seq,
                                             uint8_t frag_index, uint8_t frag_count, uint16_t frag_len) {
    out[S3P_HDR_TYPE] = S3P_PKT_AUDIO;
    out[S3P_HDR_VERSION] = (uint8_t)S3P_PROTOCOL_VERSION;
    s3p_put_u32(out + 2, recording_id);
    s3p_put_u32(out + 6, frame_seq);
    out[10] = frag_index;
    out[11] = frag_count;
    s3p_put_u16(out + 12, frag_len);
    return S3P_AUDIO_HDR_LEN;
}

/* --- END 序列化：写入 18 字节，返回总长 --- */
static inline size_t s3p_encode_end(uint8_t *out, uint32_t recording_id, uint32_t total_frames,
                                    uint32_t total_samples, uint32_t summary) {
    out[S3P_HDR_TYPE] = S3P_PKT_END;
    out[S3P_HDR_VERSION] = (uint8_t)S3P_PROTOCOL_VERSION;
    s3p_put_u32(out + 2, recording_id);
    s3p_put_u32(out + 6, total_frames);
    s3p_put_u32(out + 10, total_samples);
    s3p_put_u32(out + 14, summary);
    return S3P_END_LEN;
}

/* --- 解析结果 --- */
typedef enum {
    S3P_PARSE_OK = 0,
    S3P_PARSE_BAD_VERSION = 1,
    S3P_PARSE_TOO_SHORT = 2,
    S3P_PARSE_UNKNOWN_TYPE = 3,
} s3p_parse_err_t;

typedef struct {
    uint32_t recording_id;
    uint16_t frame_count;
    uint16_t sample_rate;
    uint8_t frame_ms;
} s3p_begin_t;

typedef struct {
    uint32_t recording_id;
    uint32_t frame_seq;
    uint8_t frag_index;
    uint8_t frag_count;
    uint16_t frag_len;
    /* 载荷指针指向输入缓冲内部，不拷贝 */
    const uint8_t *payload;
} s3p_audio_t;

typedef struct {
    uint32_t recording_id;
    uint32_t total_frames;
    uint32_t total_samples;
    uint32_t summary;
} s3p_end_t;

/* 通用解析：校验版本与最小长度，按类型解出对应结构。返回 S3P_PARSE_OK 或错误码。 */
s3p_parse_err_t s3p_parse(const uint8_t *data, size_t len, s3p_begin_t *begin, s3p_audio_t *audio, s3p_end_t *end);

/* FNV-1a 32：END.summary 的摘要算法（黄金向量独立计算时的参考实现） */
static inline uint32_t s3p_fnv1a32(const uint8_t *data, size_t len) {
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < len; i += 1) {
        hash ^= data[i];
        hash = ((uint32_t)0x01000193u) * hash;
    }
    return hash;
}

#ifdef __cplusplus
}
#endif
