/* asr_transport 集成测试（M3，C/H 类）：真实 HTTP over loopback → mock-asr。
 * 前置：mock 已在 <host>:18765 运行（bun serve.ts 18765 0.0.0.0）。
 * 用例：传输+分类纵切（ok/empty/401/429/500/invalid/超时）+ 认证头与
 * 数据最小化审计（ASR-05：multipart 字段、Authorization 形态）。
 * 运行：test_asr_transport.exe [host]（默认 127.0.0.1）
 */
#include "asr_transport.h"

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

static char g_host[128] = "127.0.0.1";

/* 经传输层配置 mock 场景（用被测 HTTP 客户端本身打控制面） */
static asr_status_t mock_config(const char *json) {
    asr_http_req_t r;
    memset(&r, 0, sizeof(r));
    r.host = g_host;
    r.port = 18765;
    r.secure = 0;
    r.path = "/__mock/config";
    r.method = "POST";
    r.content_type = "application/json";
    r.timeout_ms = 5000;
    char resp[256];
    int status = 0;
    return asr_http_post(&r, (const uint8_t *)json, strlen(json),
                         resp, sizeof(resp), &status);
}

static asr_status_t mock_get_last(char *resp, size_t cap) {
    asr_http_req_t r;
    memset(&r, 0, sizeof(r));
    r.host = g_host;
    r.port = 18765;
    r.secure = 0;
    r.path = "/__mock/last";
    r.method = "GET";
    r.content_type = "application/json";
    r.timeout_ms = 5000;
    int status = 0;
    return asr_http_post(&r, NULL, 0, resp, cap, &status);
}

static size_t make_wav(uint8_t *buf, size_t cap) {
    size_t n = cap < 512 ? cap : 512;
    for (size_t i = 0; i < n; i += 1) buf[i] = (uint8_t)(i * 7);
    return n;
}

/* 一轮完整 ASR 调用（构造→POST→分类） */
static asr_status_t run_asr(const char *api_key, unsigned timeout_ms,
                            char *text, size_t text_cap, int *http_status) {
    static uint8_t wav[512];
    static uint8_t body[8192];
    static char resp[8192];
    size_t wav_len = make_wav(wav, sizeof(wav));
    size_t body_len = asr_multipart_build(wav, wav_len, body, sizeof(body));
    if (body_len == 0) return ASR_PARAM;

    asr_http_req_t r;
    memset(&r, 0, sizeof(r));
    r.host = g_host;
    r.port = 18765;
    r.secure = 0;
    r.path = "/api/paas/v4/audio/transcriptions";
    r.api_key = api_key;
    r.content_type = asr_content_type();
    r.timeout_ms = timeout_ms;

    *http_status = 0;
    asr_status_t st = asr_http_post(&r, body, body_len, resp, sizeof(resp), http_status);
    if (st != ASR_OK) return st;
    return asr_classify(*http_status, resp, strlen(resp), text, text_cap);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        strncpy(g_host, argv[1], sizeof(g_host) - 1);
    }

    char text[128];
    int status = 0;

    TEST("纵切 ok：200 → ASR_OK + text");
    {
        CHECK(mock_config("{\"scenario\":\"ok\",\"delayMs\":0}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_OK);
        CHECK(status == 200);
        CHECK(text[0] != '\0');
    }

    TEST("empty_text → ASR_EMPTY_TEXT");
    {
        CHECK(mock_config("{\"scenario\":\"empty_text\"}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_EMPTY_TEXT);
        CHECK(status == 200);
    }

    TEST("401 → ASR_AUTH");
    {
        CHECK(mock_config("{\"scenario\":\"http_401\"}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_AUTH);
        CHECK(status == 401);
    }

    TEST("429(1113) → ASR_ARREARS");
    {
        CHECK(mock_config("{\"scenario\":\"http_429\"}") == ASR_OK);
        /* 429 场景固定回 code=1113（mock 实现），欠费路径 */
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_ARREARS);
        CHECK(status == 429);
    }

    TEST("500 → ASR_SERVER_BUSY");
    {
        CHECK(mock_config("{\"scenario\":\"http_500\"}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_SERVER_BUSY);
        CHECK(status == 500);
    }

    TEST("invalid_json → ASR_PROTOCOL");
    {
        CHECK(mock_config("{\"scenario\":\"invalid_json\"}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_PROTOCOL);
    }

    TEST("注入 25s 延迟 + 客户端 1.5s 超时 → ASR_TIMEOUT");
    {
        CHECK(mock_config("{\"scenario\":\"ok\",\"delayMs\":25000}") == ASR_OK);
        int st = (int)run_asr("test-key-123", 1500, text, sizeof(text), &status);
        CHECK(st == (int)ASR_TIMEOUT);
        CHECK(status == 0); /* 交换未完成，状态码未设置 */
        /* 恢复场景，避免污染后续用例 */
        CHECK(mock_config("{\"scenario\":\"ok\",\"delayMs\":0}") == ASR_OK);
    }

    TEST("审计：Authorization 形态 + ASR-05 数据最小化 + 边界");
    {
        CHECK(mock_config("{\"scenario\":\"ok\",\"delayMs\":0}") == ASR_OK);
        asr_status_t st = run_asr("test-key-123", 5000, text, sizeof(text), &status);
        CHECK(st == ASR_OK);
        CHECK(status == 200);

        static char resp[8192];
        CHECK(mock_get_last(resp, sizeof(resp)) == ASR_OK);
        CHECK(strstr(resp, "\"path\": \"/api/paas/v4/audio/transcriptions\"") != NULL ||
              strstr(resp, "\"path\":\"/api/paas/v4/audio/transcriptions\"") != NULL);
        CHECK(strstr(resp, "Bearer test-key-123") != NULL);
        CHECK(strstr(resp, ASR_BOUNDARY) != NULL);
        CHECK(strstr(resp, "\"model\": \"glm-asr-2512\"") != NULL ||
              strstr(resp, "\"model\":\"glm-asr-2512\"") != NULL);
        CHECK(strstr(resp, "\"stream\": \"false\"") != NULL ||
              strstr(resp, "\"stream\":\"false\"") != NULL);
        /* 数据最小化：多余字段为空对象（ASR-05） */
        CHECK(strstr(resp, "\"unexpected\": {}") != NULL ||
              strstr(resp, "\"unexpected\":{}") != NULL);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
