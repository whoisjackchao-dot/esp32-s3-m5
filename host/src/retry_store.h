#pragma once
// M4 S4.7d —— 重试资格/TTL 纯逻辑（内存态；不落盘，符合 C02/SEC-04）。
#include <stdbool.h>
#include <stdint.h>

#define RETRY_TTL_MS 300000u

typedef enum {
    RETRY_NONE = 0,
    RETRY_APPLIED = 1,
    RETRY_REJECTED = 2,
    RETRY_UNCERTAIN = 3,
    RETRY_FAILED = 4
} retry_status;

typedef struct {
    bool has_audio;
    bool has_text;
    retry_status status;
    bool target_valid;
    uint32_t at_ms;
    char target_session[64];
} retry_record;

typedef struct {
    bool has_rec;
    retry_record rec;
} retry_store;

void retry_store_init(retry_store *s);

void retry_store_note(retry_store *s, retry_status status, bool has_audio, bool has_text,
                      const char *target_session, bool target_valid, uint32_t now_ms);

bool retry_can_recognize(const retry_store *s, uint32_t now_ms);

bool retry_can_append(const retry_store *s, uint32_t now_ms);

void retry_store_expire(retry_store *s, uint32_t now_ms);
