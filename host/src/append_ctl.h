#pragma once
// M4 S4.7d —— draft.append 请求构造与结果分类（纯逻辑，host 与 voice_append 共用）。
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APPEND_APPLIED = 0,
    APPEND_PROCESSING = 1,
    APPEND_UNCERTAIN = 2,
    APPEND_REJECTED = 3
} append_outcome;

// 构造 POST /v1/draft/append 的 JSON 请求体（单一固定 schema，与 M1 契约一致）。
// 返回写入字节数（不含结尾 NUL）；参数缺失或 out_cap 不足返回 0。
size_t append_build_request(const char *epoch, const char *session, const char *op_id,
                            const char *text, char *out, size_t cap);

// 按 HTTP 状态与响应体分类结果。
append_outcome append_classify(int http_status, const char *body, size_t len);

#ifdef __cplusplus
}
#endif
