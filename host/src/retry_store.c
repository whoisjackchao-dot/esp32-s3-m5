#include "retry_store.h"

#include <string.h>

void retry_store_init(retry_store *s) {
    memset(s, 0, sizeof(*s));
}

void retry_store_note(retry_store *s, retry_status status, bool has_audio, bool has_text,
                      const char *target_session, bool target_valid, uint32_t now_ms) {
    memset(&s->rec, 0, sizeof(s->rec));
    s->rec.has_audio = has_audio;
    s->rec.has_text = has_text;
    s->rec.status = status;
    s->rec.target_valid = target_valid;
    s->rec.at_ms = now_ms;
    if (target_session != NULL && target_session[0] != '\0') {
        size_t n = strlen(target_session);
        if (n >= sizeof(s->rec.target_session)) {
            n = sizeof(s->rec.target_session) - 1u;
        }
        memcpy(s->rec.target_session, target_session, n);
        s->rec.target_session[n] = '\0';
    }
    s->has_rec = true;
}

static bool fresh(const retry_store *s, uint32_t now_ms) {
    return s->has_rec && (uint32_t)(now_ms - s->rec.at_ms) <= RETRY_TTL_MS;
}

bool retry_can_recognize(const retry_store *s, uint32_t now_ms) {
    return fresh(s, now_ms) && s->rec.has_audio && s->rec.status != RETRY_APPLIED;
}

bool retry_can_append(const retry_store *s, uint32_t now_ms) {
    return fresh(s, now_ms) && s->rec.status == RETRY_REJECTED && s->rec.has_text &&
           s->rec.target_valid;
}

void retry_store_expire(retry_store *s, uint32_t now_ms) {
    if (s->has_rec && (uint32_t)(now_ms - s->rec.at_ms) > RETRY_TTL_MS) {
        memset(s, 0, sizeof(*s));
    }
}
