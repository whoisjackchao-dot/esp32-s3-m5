/* asr_adapter 单元测试（M3 Selective TDD：RED 先行）
 * 用例对应：ASR-05（数据最小化，黄金字节）、TX-18（空文本）、
 * TX-07（20s 超时分类入口）、快速指南 §4（错误码分类）。
 * 构建运行（Windows/MSVC）：
 *   vcvars64 && cl /nologo /W4 /Fe:test_asr_adapter.exe asr_adapter_test.c asr_adapter.c && test_asr_adapter.exe
 */
#include "asr_adapter.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        checks += 1;                                                                                                   \
        if (!(cond)) {                                                                                                 \
            failures += 1;                                                                                             \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                     \
        }                                                                                                              \
    } while (0)

#define TEST(name) printf("== %s ==\n", name)

static const char B[] = ASR_BOUNDARY;

/* 字节转义辅助：把期望体拼出来（与实现独立的手工推导期望值） */
static size_t build_expected(uint8_t *out, size_t cap, const uint8_t *wav, size_t wav_len) {
    size_t off = 0;
    int n = 0;
    const char *part1 =
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
    const char *tail =
        "\r\n--" ASR_BOUNDARY "--\r\n";
    n = snprintf((char *)out, cap, "%s", part1);
    if (n < 0 || (size_t)n >= cap) return 0;
    off = (size_t)n;
    if (off + wav_len + strlen(tail) >= cap) return 0;
    memcpy(out + off, wav, wav_len);
    off += wav_len;
    memcpy(out + off, tail, strlen(tail));
    off += strlen(tail);
    (void)n;
    return off;
}

int main(void) {
    /* ASR-05：黄金字节——字段恰好三个，顺序/边界/值精确 */
    TEST("ASR-05: multipart golden bytes");
    {
        static const uint8_t wav[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint8_t got[1024];
        uint8_t exp[1024];
        size_t got_len = asr_multipart_build(wav, sizeof(wav), got, sizeof(got));
        size_t exp_len = build_expected(exp, sizeof(exp), wav, sizeof(wav));
        CHECK(got_len == exp_len);
        CHECK(got_len > 0);
        CHECK(memcmp(got, exp, exp_len) == 0);
        /* 属性复核：恰好 3 个字段开（"; name=\"" 精确匹配，避免误匹配 filename=） */
        const char *p = (const char *)got;
        int name_fields = 0;
        for (size_t i = 0; i + 8 < got_len; i += 1)
            if (memcmp(p + i, "; name=\"", 8) == 0) name_fields += 1;
        CHECK(name_fields == 3);
    }

    TEST("ASR-05: multipart empty wav (0 字节文件仍为合法结构)");
    {
        uint8_t got[1024];
        uint8_t exp[1024];
        size_t got_len = asr_multipart_build(NULL, 0, got, sizeof(got)) == 0
                             ? 0
                             : asr_multipart_build((const uint8_t *)"", 0, got, sizeof(got));
        size_t exp_len = build_expected(exp, sizeof(exp), (const uint8_t *)"", 0);
        CHECK(got_len == exp_len);
        CHECK(got_len > 0);
        CHECK(memcmp(got, exp, exp_len) == 0);
    }

    TEST("ASR-05: 输出缓冲不足返回 0");
    {
        static const uint8_t wav[4] = {1, 2, 3, 4};
        uint8_t got[32];
        CHECK(asr_multipart_build(wav, sizeof(wav), got, sizeof(got)) == 0);
        CHECK(asr_multipart_build(NULL, 4, got, sizeof(got)) == 0);
    }

    TEST("Content-Type 常量含边界");
    {
        const char *ct = asr_content_type();
        CHECK(strstr(ct, "multipart/form-data") == ct);
        CHECK(strstr(ct, ASR_BOUNDARY) != NULL);
    }

    TEST("200 + text 非空 → ASR_OK 并提取 text（UTF-8 原样）");
    {
        /* {"text":"你好 API"} */
        static const char body[] = "{\"text\":\"\xE4\xBD\xA0\xE5\xA5\xBD API\"}";
        static const char want[] = "\xE4\xBD\xA0\xE5\xA5\xBD API";
        char text[64];
        asr_status_t st = asr_classify(200, body, sizeof(body) - 1, text, sizeof(text));
        CHECK(st == ASR_OK);
        CHECK(strcmp(text, want) == 0);
    }

    TEST("200 + 转义解码（\\\" \\\\ \\n）");
    {
        /* JSON: {"text":"a\"b\\c\n"} */
        static const char body[] = "{\"text\":\"a\\\"b\\\\c\\n\"}";
        char text[64];
        asr_status_t st = asr_classify(200, body, sizeof(body) - 1, text, sizeof(text));
        CHECK(st == ASR_OK);
        CHECK(memcmp(text, "a\"b\\c\n", 5) == 0);
    }

    TEST("200 + \\u 转义（含 BMP）");
    {
        /* JSON: {"text":"\u4f60\u597d"} → 你好 */
        static const char body[] = "{\"text\":\"\\u4f60\\u597d\"}";
        char text[64];
        asr_status_t st = asr_classify(200, body, sizeof(body) - 1, text, sizeof(text));
        CHECK(st == ASR_OK);
        CHECK(memcmp(text, "\xE4\xBD\xA0\xE5\xA5\xBD", 6) == 0);
    }

    TEST("200 + 代理对 \\ud83d\\ude00 → 4 字节 UTF-8");
    {
        static const char body[] = "{\"text\":\"\\ud83d\\ude00\"}";
        char text[64];
        asr_status_t st = asr_classify(200, body, sizeof(body) - 1, text, sizeof(text));
        CHECK(st == ASR_OK);
        CHECK(memcmp(text, "\xF0\x9F\x98\x80", 4) == 0);
    }

    TEST("200 + text 空串 → ASR_EMPTY_TEXT（TX-18）");
    {
        static const char body[] = "{\"text\":\"\"}";
        char text[64];
        asr_status_t st = asr_classify(200, body, sizeof(body) - 1, text, sizeof(text));
        CHECK(st == ASR_EMPTY_TEXT);
    }

    TEST("200 + 非 JSON / 缺 text / text 非字符串 → ASR_PROTOCOL");
    {
        char text[64];
        CHECK(asr_classify(200, "not-json{{{", 11, text, sizeof(text)) == ASR_PROTOCOL);
        CHECK(asr_classify(200, "{\"txte\":\"x\"}", 12, text, sizeof(text)) == ASR_PROTOCOL);
        CHECK(asr_classify(200, "{\"text\":123}", 12, text, sizeof(text)) == ASR_PROTOCOL);
        CHECK(asr_classify(200, NULL, 0, text, sizeof(text)) == ASR_PROTOCOL);
    }

    TEST("text 缓冲不足 → ASR_NOMEM");
    {
        static const char body[] = "{\"text\":\"\xE4\xBD\xA0\xE5\xA5\xBD API\"}";
        char text[4];
        CHECK(asr_classify(200, body, sizeof(body) - 1, text, sizeof(text)) == ASR_NOMEM);
    }

    TEST("401 → ASR_AUTH");
    {
        static const char body[] = "{\"error\":{\"code\":\"1001\"}}";
        char text[64];
        CHECK(asr_classify(401, body, sizeof(body) - 1, text, sizeof(text)) == ASR_AUTH);
    }

    TEST("429 分类：1113→欠费；1302→限速；数字形式；未知→限速");
    {
        char text[64];
        static const char ar[] = "{\"error\":{\"code\":\"1113\",\"message\":\"m\"}}";
        static const char num[] = "{\"error\":{\"code\":1113}}";
        static const char lim[] = "{\"error\":{\"code\":\"1302\"}}";
        static const char unk[] = "{\"error\":{\"code\":\"9999\"}}";
        CHECK(asr_classify(429, ar, sizeof(ar) - 1, text, sizeof(text)) == ASR_ARREARS);
        CHECK(asr_classify(429, num, sizeof(num) - 1, text, sizeof(text)) == ASR_ARREARS);
        CHECK(asr_classify(429, lim, sizeof(lim) - 1, text, sizeof(text)) == ASR_RATE_LIMITED);
        CHECK(asr_classify(429, unk, sizeof(unk) - 1, text, sizeof(text)) == ASR_RATE_LIMITED);
    }

    TEST("5xx → ASR_SERVER_BUSY；其他状态 → ASR_PROTOCOL");
    {
        char text[64];
        CHECK(asr_classify(500, "{}", 2, text, sizeof(text)) == ASR_SERVER_BUSY);
        CHECK(asr_classify(503, "{}", 2, text, sizeof(text)) == ASR_SERVER_BUSY);
        CHECK(asr_classify(301, "{}", 2, text, sizeof(text)) == ASR_PROTOCOL);
    }

    TEST("参数错误 → ASR_PARAM");
    {
        static const char body[] = "{\"text\":\"x\"}";
        char text[64];
        CHECK(asr_classify(200, body, sizeof(body) - 1, NULL, sizeof(text)) == ASR_PARAM);
        CHECK(asr_classify(200, body, sizeof(body) - 1, text, 0) == ASR_PARAM);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
