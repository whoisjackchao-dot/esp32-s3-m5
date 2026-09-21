#include "s3p_proto.h"

s3p_parse_err_t s3p_parse(const uint8_t *data, size_t len, s3p_begin_t *begin, s3p_audio_t *audio, s3p_end_t *end) {
    if (len < 2u) return S3P_PARSE_TOO_SHORT;
    if (data[S3P_HDR_VERSION] != (uint8_t)S3P_PROTOCOL_VERSION) return S3P_PARSE_BAD_VERSION;

    switch (data[S3P_HDR_TYPE]) {
        case S3P_PKT_BEGIN:
            if (len < S3P_BEGIN_LEN) return S3P_PARSE_TOO_SHORT;
            if (begin) {
                begin->recording_id = s3p_get_u32(data + 2);
                begin->frame_count = s3p_get_u16(data + 6);
                begin->sample_rate = s3p_get_u16(data + 8);
                begin->frame_ms = data[10];
            }
            return S3P_PARSE_OK;
        case S3P_PKT_AUDIO: {
            if (len < S3P_AUDIO_HDR_LEN) return S3P_PARSE_TOO_SHORT;
            if (audio) {
                audio->recording_id = s3p_get_u32(data + 2);
                audio->frame_seq = s3p_get_u32(data + 6);
                audio->frag_index = data[10];
                audio->frag_count = data[11];
                audio->frag_len = s3p_get_u16(data + 12);
                /* 载荷越界防护：fragLen 与实际长度一致性由调用方按传输层裁决 */
                audio->payload = data + S3P_AUDIO_HDR_LEN;
            }
            return S3P_PARSE_OK;
        }
        case S3P_PKT_END:
            if (len < S3P_END_LEN) return S3P_PARSE_TOO_SHORT;
            if (end) {
                end->recording_id = s3p_get_u32(data + 2);
                end->total_frames = s3p_get_u32(data + 6);
                end->total_samples = s3p_get_u32(data + 10);
                end->summary = s3p_get_u32(data + 14);
            }
            return S3P_PARSE_OK;
        default:
            return S3P_PARSE_UNKNOWN_TYPE;
    }
}
