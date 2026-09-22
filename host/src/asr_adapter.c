#include "asr_adapter.h"

#include <stdio.h>
#include <string.h>

const char *asr_content_type(void) {
    return "multipart/form-data; boundary=" ASR_BOUNDARY;
}

size_t asr_multipart_build(const uint8_t *wav, size_t wav_len,
                           uint8_t *out, size_t out_cap) {
    static const char part1[] =
        "--" ASR_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n"
        "\r\n"
        "glm-asr-2512\r\n"
        "--" ASR_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"stream\"\r\n"
        "\r\n"
        "false\r\n"
        "--" ASR_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";
    static const char tail[] = "\r\n--" ASR_BOUNDARY "--\r\n";

    if (out == NULL || (wav == NULL && wav_len != 0)) {
        return 0;
    }
    const size_t p1len = sizeof(part1) - 1;
    const size_t taillen = sizeof(tail) - 1;
    if (wav_len > out_cap || p1len > out_cap - wav_len ||
        taillen > out_cap - wav_len - p1len) {
        return 0;
    }
    memcpy(out, part1, p1len);
    memcpy(out + p1len, wav, wav_len);
    memcpy(out + p1len + wav_len, tail, taillen);
    return p1len + wav_len + taillen;
}

/* —— 极小 JSON 定位器（受限规格，见头文件「已知局限」注释）——
 * 在 body[0..len) 中定位 "key"，跳过 ws/冒号/ws 后取值：
 *   字符串值 → 解转义写入 out（NUL 结尾），返回 1；
 *   数字值   → 十进制文本写入 out，返回 1；
 *   键不存在 → 0；值类型不符/转义非法 → -1；out 不足 → -2。 */
static int jfind_value(const char *body, size_t len, const char *key,
                       char *out, size_t cap, int numbers_ok) {
    const size_t klen = strlen(key);
    size_t i = 0;
    int found = 0;
    char pat[32];
    if (klen + 2 >= sizeof(pat)) return 0;
    pat[0] = '"';
    memcpy(pat + 1, key, klen);
    pat[klen + 1] = '"';
    pat[klen + 2] = '\0';

    while (i + klen + 2 <= len) {
        if (body[i] == '"' && memcmp(body + i, pat, klen + 2) == 0) {
            found = 1;
            i += klen + 2;
            break;
        }
        i += 1;
    }
    if (!found) return 0;

    while (i < len && (body[i] == ' ' || body[i] == '\t' ||
                       body[i] == '\r' || body[i] == '\n')) {
        i += 1;
    }
    if (i >= len || body[i] != ':') return -1;
    i += 1;
    while (i < len && (body[i] == ' ' || body[i] == '\t' ||
                       body[i] == '\r' || body[i] == '\n')) {
        i += 1;
    }
    if (i >= len) return -1;

    size_t o = 0;
    if (body[i] == '"') {
        i += 1;
        while (i < len) {
            unsigned char c = (unsigned char)body[i];
            if (c == '"') {
                if (o + 1 > cap) return -2;
                out[o] = '\0';
                return 1;
            }
            if (c != '\\') {
                if (o + 1 > cap) return -2;
                out[o++] = (char)c;
                i += 1;
                continue;
            }
            if (i + 1 >= len) return -1;
            i += 1;
            char e = body[i];
            switch (e) {
            case '"': case '\\': case '/':
                if (o + 1 > cap) return -2;
                out[o++] = e;
                i += 1;
                break;
            case 'b': if (o + 1 > cap) return -2; out[o++] = '\b'; i += 1; break;
            case 'f': if (o + 1 > cap) return -2; out[o++] = '\f'; i += 1; break;
            case 'n': if (o + 1 > cap) return -2; out[o++] = '\n'; i += 1; break;
            case 'r': if (o + 1 > cap) return -2; out[o++] = '\r'; i += 1; break;
            case 't': if (o + 1 > cap) return -2; out[o++] = '\t'; i += 1; break;
            case 'u': {
                if (i + 4 >= len) return -1;
                unsigned cp = 0;
                for (int k = 1; k <= 4; k += 1) {
                    char h = body[i + k];
                    unsigned v;
                    if (h >= '0' && h <= '9') v = (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') v = (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v = (unsigned)(h - 'A' + 10);
                    else return -1;
                    cp = (cp << 4) | v;
                }
                i += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len &&
                    body[i] == '\\' && body[i + 1] == 'u') {
                    unsigned lo = 0;
                    int ok = 1;
                    if (i + 5 >= len) return -1;
                    for (int k = 2; k <= 5; k += 1) {
                        char h = body[i + k];
                        unsigned v;
                        if (h >= '0' && h <= '9') v = (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v = (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v = (unsigned)(h - 'A' + 10);
                        else { ok = 0; break; }
                        lo = (lo << 4) | v;
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                uint8_t u[4];
                size_t un;
                if (cp < 0x80) { u[0] = (uint8_t)cp; un = 1; }
                else if (cp < 0x800) {
                    u[0] = (uint8_t)(0xC0 | (cp >> 6));
                    u[1] = (uint8_t)(0x80 | (cp & 0x3F));
                    un = 2;
                } else if (cp < 0x10000) {
                    u[0] = (uint8_t)(0xE0 | (cp >> 12));
                    u[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                    u[2] = (uint8_t)(0x80 | (cp & 0x3F));
                    un = 3;
                } else {
                    u[0] = (uint8_t)(0xF0 | (cp >> 18));
                    u[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
                    u[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                    u[3] = (uint8_t)(0x80 | (cp & 0x3F));
                    un = 4;
                }
                if (o + un + 1 > cap) return -2;
                memcpy(out + o, u, un);
                o += un;
                break;
            }
            default:
                return -1;
            }
        }
        return -1;
    }

    /* 数字值（仅在调用方声明可接受数字时；text 必须是字符串） */
    if (!numbers_ok) return -1;
    if (body[i] == '-' || (body[i] >= '0' && body[i] <= '9')) {
        while (i < len && ((body[i] >= '0' && body[i] <= '9') ||
                           body[i] == '-' || body[i] == '+')) {
            if (o + 1 > cap) return -2;
            out[o++] = body[i];
            i += 1;
        }
        if (o + 1 > cap) return -2;
        out[o] = '\0';
        return 1;
    }
    return -1;
}

asr_status_t asr_classify(int http_status, const char *body, size_t body_len,
                          char *text_out, size_t text_cap) {
    if (text_out == NULL || text_cap == 0) {
        return ASR_PARAM;
    }
    text_out[0] = '\0';

    switch (http_status) {
    case 200: {
        if (body == NULL) return ASR_PROTOCOL;
        const int rc = jfind_value(body, body_len, "text", text_out, text_cap, 0);
        if (rc == -2) return ASR_NOMEM;
        if (rc <= 0) return ASR_PROTOCOL;
        return strlen(text_out) == 0 ? ASR_EMPTY_TEXT : ASR_OK;
    }
    case 401:
        return ASR_AUTH;
    case 429: {
        char code[16];
        if (body != NULL) {
            const int rc = jfind_value(body, body_len, "code", code, sizeof(code), 1);
            if (rc == 1 && strcmp(code, "1113") == 0) return ASR_ARREARS;
        }
        return ASR_RATE_LIMITED;
    }
    default:
        if (http_status >= 500 && http_status <= 599) return ASR_SERVER_BUSY;
        return ASR_PROTOCOL;
    }
}
