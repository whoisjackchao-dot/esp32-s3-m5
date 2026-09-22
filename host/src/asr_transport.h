#ifndef ASR_TRANSPORT_H
#define ASR_TRANSPORT_H

/* M06 ASR 适配器——WinHTTP 传输薄封装（C/H 类）。
 * 职责：一次 POST 交换（超时=要求值；Authorization 按需；状态码/响应体带回）。
 * 分类不在此层：拿到 http_status 与 body 后由 asr_classify 裁决。
 * 超时语义：resolve/connect/send/receive 统一为 timeout_ms，
 * WinHTTP 超时（12002）→ ASR_TIMEOUT，其余 WinHTTP 错误 → ASR_TRANSPORT。 */

#include "asr_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *host;          /* 不含端口；UTF-8/ASCII */
    unsigned short port;       /* 80/443 或测试端口 */
    int secure;                /* 1 = HTTPS（WINHTTP_FLAG_SECURE） */
    const char *path;          /* 如 /api/paas/v4/audio/transcriptions */
    const char *method;        /* "GET"/"POST"；NULL → "POST" */
    const char *api_key;       /* 可为 NULL：不发送 Authorization；否则 Bearer <key> */
    const char *content_type;  /* 完整 Content-Type 值（含 boundary） */
    unsigned timeout_ms;       /* resolve/connect/send/receive 总口径 */
} asr_http_req_t;

/* 执行一次 POST 交换。
 * 返回 ASR_OK：交换完成（http_status 已填，可能为 4xx/5xx，分类交给 classify）；
 * 返回其他：交换未完成，http_status 置 0；resp 前缀内容不可信（保证 NUL 结尾）。 */
asr_status_t asr_http_post(const asr_http_req_t *req,
                           const uint8_t *body, size_t body_len,
                           char *resp, size_t resp_cap, int *http_status);

#ifdef __cplusplus
}
#endif

#endif /* ASR_TRANSPORT_H */
