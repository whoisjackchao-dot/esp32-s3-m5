#ifndef ASR_ADAPTER_H
#define ASR_ADAPTER_H

/* M06 ASR 适配器——纯逻辑层（T 类，Selective TDD）。
 * 范围：multipart 构造、响应分类、文本提取。WinHTTP 传输为薄封装
 * （C/H 类，另行集成）；20s 请求超时由传输层执行，适配器只做 TIMEOUT 分类。
 * 冻结依据：contracts/M2 之外见 功能模块HLD M06 与 快速指南 §2；
 * 数据最小化（ASR-05）：请求体仅含 model / stream / file 三个字段。 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* multipart 边界为固定常量：确定性构造使黄金字节测试可行；
 * 与用户数据冲突概率为零（含完整字段结构）。 */
#define ASR_BOUNDARY "----StickS3ASRBoundary7d1a2c9f"

typedef enum {
    ASR_OK = 0,        /* 200 且 text 非空 */
    ASR_EMPTY_TEXT,    /* 200 但 text 为空串（TX-18：不填入，不发起追加） */
    ASR_AUTH,          /* 401：Key 无效/缺失 */
    ASR_ARREARS,       /* 429 且 code=1113：欠费 */
    ASR_RATE_LIMITED,  /* 429 其余：限速/服务繁忙/未知 429 */
    ASR_SERVER_BUSY,   /* 5xx */
    ASR_TIMEOUT,       /* 传输层 20s 超时（C09/APP_DEADLINE） */
    ASR_PROTOCOL,      /* 非 JSON、缺 text 字段、text 非字符串、未知状态码 */
    ASR_TRANSPORT,     /* 其他传输错误 */
    ASR_PARAM,         /* 调用方参数错误 */
    ASR_NOMEM          /* 输出缓冲不足 */
} asr_status_t;

/* 供 WinHTTP 传输层设置的 Content-Type 头（含固定边界）。 */
const char *asr_content_type(void);

/* 构造 multipart/form-data 请求体（固定边界）。
 * 字段恰好三个：model=glm-asr-2512、stream=false、file(=wav 字节)。
 * 返回写入字节数；参数错误或 out_cap 不足返回 0。 */
size_t asr_multipart_build(const uint8_t *wav, size_t wav_len,
                           uint8_t *out, size_t out_cap);

/* 按 HTTP 状态码与响应体分类。
 * ASR_OK 时将 text（UTF-8，已解转义，NUL 结尾）写入 text_out；
 * 其余状态 text_out 清空。body 可为 NULL；body 无需 NUL 结尾。
 * 已知局限：text/code 键定位为朴素子串匹配，错误体中若嵌套同名键
 * 理论上可误定位——当前响应体形小且结构固定，接受此限制。 */
asr_status_t asr_classify(int http_status, const char *body, size_t body_len,
                          char *text_out, size_t text_cap);

#ifdef __cplusplus
}
#endif

#endif /* ASR_ADAPTER_H */
