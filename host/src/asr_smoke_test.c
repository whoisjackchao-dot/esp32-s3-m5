/* asr_smoke——M06 真实端点冒烟（已获授权的一次性计费调用）。
 * 用法：asr_smoke.exe <音频路径>   （需环境变量 STICKS3_ASR_KEY）
 * 与 PowerShell 验证脚本同端点/模型/字段；验证 C 传输+分类栈对真实服务
 * 的行为与 mock 一致。响应文本应为非敏感样本。
 * 退出码：0=ASR_OK；1=其他分类；2=本进程前置错误。
 */
#include "asr_adapter.h"
#include "asr_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static const char *status_name(asr_status_t s) {
    switch (s) {
    case ASR_OK: return "ASR_OK";
    case ASR_EMPTY_TEXT: return "ASR_EMPTY_TEXT";
    case ASR_AUTH: return "ASR_AUTH";
    case ASR_ARREARS: return "ASR_ARREARS";
    case ASR_RATE_LIMITED: return "ASR_RATE_LIMITED";
    case ASR_SERVER_BUSY: return "ASR_SERVER_BUSY";
    case ASR_TIMEOUT: return "ASR_TIMEOUT";
    case ASR_PROTOCOL: return "ASR_PROTOCOL";
    case ASR_TRANSPORT: return "ASR_TRANSPORT";
    case ASR_PARAM: return "ASR_PARAM";
    case ASR_NOMEM: return "ASR_NOMEM";
    default: return "UNKNOWN";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: asr_smoke.exe <audio path>\n");
        return 2;
    }
    const char *key = getenv("STICKS3_ASR_KEY");
    if (key == NULL || *key == '\0') {
        printf("[FAIL] env STICKS3_ASR_KEY not set\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        printf("[FAIL] cannot open audio: %s\n", argv[1]);
        return 2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 2;
    }
    const long flen = ftell(f);
    if (flen <= 0 || flen > 25 * 1024 * 1024) {
        fclose(f);
        printf("[FAIL] audio size invalid: %ld\n", flen);
        return 2;
    }
    rewind(f);
    uint8_t *wav = (uint8_t *)malloc((size_t)flen);
    if (wav == NULL || fread(wav, 1, (size_t)flen, f) != (size_t)flen) {
        fclose(f);
        free(wav);
        printf("[FAIL] audio read failed\n");
        return 2;
    }
    fclose(f);
    printf("audio: %s (%ld bytes)\n", argv[1], flen);

    uint8_t *body = (uint8_t *)malloc((size_t)flen + 4096);
    if (body == NULL) {
        free(wav);
        return 2;
    }
    const size_t body_len = asr_multipart_build(wav, (size_t)flen, body,
                                                (size_t)flen + 4096);
    if (body_len == 0) {
        printf("[FAIL] multipart build failed\n");
        free(wav);
        free(body);
        return 2;
    }

    asr_http_req_t r;
    memset(&r, 0, sizeof(r));
    r.host = "open.bigmodel.cn";
    r.port = 443;
    r.secure = 1;
    r.path = "/api/paas/v4/audio/transcriptions";
    r.method = "POST";
    r.api_key = key;
    r.content_type = asr_content_type();
    r.timeout_ms = 20000; /* C09/APP_DEADLINE */

    char *resp = (char *)malloc(65536);
    int status = 0;
    const asr_status_t st = asr_http_post(&r, body, body_len, resp, 65536, &status);
    if (st != ASR_OK) {
        printf("[FAIL] transport: %s (HTTP %d)\n", status_name(st), status);
        free(wav);
        free(body);
        free(resp);
        return 1;
    }

    char *text = (char *)malloc(4096);
    const asr_status_t cls = asr_classify(status, resp, strlen(resp), text, 4096);
    printf("HTTP %d  class=%s\n", status, status_name(cls));
    printf("text: %s\n", text);
    printf(cls == ASR_OK ? "[PASS] real-endpoint smoke OK\n"
                         : "[NON-OK] real-endpoint smoke classification not OK\n");

    /* 纪律：响应缓冲与文本用后清零（含可能的敏感转写，本例为非敏感样本） */
    SecureZeroMemory(resp, 65536);
    SecureZeroMemory(text, 4096);
    free(wav);
    free(body);
    free(resp);
    free(text);
    return cls == ASR_OK ? 0 : 1;
}
