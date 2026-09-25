#include "append_ctl.h"

#include <stdio.h>
#include <string.h>

static bool esc(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        char tmp[8];
        const char *rep = NULL;
        switch (*p) {
        case '"': rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\b': rep = "\\b"; break;
        case '\f': rep = "\\f"; break;
        case '\n': rep = "\\n"; break;
        case '\r': rep = "\\r"; break;
        case '\t': rep = "\\t"; break;
        default:
            if (*p < 0x20u) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)*p);
                rep = tmp;
            }
            break;
        }
        const size_t need = rep ? strlen(rep) : 1u;
        if (o + need + 1u > cap) return false;
        if (rep) {
            memcpy(out + o, rep, need);
        } else {
            out[o] = (char)*p;
        }
        o += need;
    }
    out[o] = '\0';
    return true;
}

size_t append_build_request(const char *epoch, const char *session, const char *op_id,
                            const char *text, char *out, size_t cap) {
    if (epoch == NULL || session == NULL || op_id == NULL || text == NULL || out == NULL) return 0;
    char esc_text[8192];
    if (!esc(text, esc_text, sizeof(esc_text))) return 0;
    const int n = snprintf(out, cap,
                           "{\"protocolVersion\":1,\"tuiEpoch\":\"%s\",\"sessionID\":\"%s\","
                           "\"operationID\":\"%s\",\"transactionGeneration\":1,\"text\":\"%s\"}",
                           epoch, session, op_id, esc_text);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

static bool contains(const char *body, size_t len, const char *needle) {
    const size_t nl = strlen(needle);
    if (nl == 0 || nl > len) return false;
    for (size_t i = 0; i + nl <= len; ++i) {
        if (memcmp(body + i, needle, nl) == 0) return true;
    }
    return false;
}

append_outcome append_classify(int http_status, const char *body, size_t len) {
    if (body != NULL) {
        if (http_status == 200 && contains(body, len, "\"applied\"")) return APPEND_APPLIED;
        if (contains(body, len, "\"processing\"")) return APPEND_PROCESSING;
        if (contains(body, len, "\"uncertain\"")) return APPEND_UNCERTAIN;
    }
    return APPEND_REJECTED;
}
