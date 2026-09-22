#ifndef ASR_CREDENTIAL_H
#define ASR_CREDENTIAL_H

/* C11 落地：ASR API Key 存储于 Windows 凭据管理器（DPAPI，绑定用户+机器）。
 * TargetName 冻结：StickS3/ZhipuASR/ApiKey（测试用 StickS3Test/ 前缀隔离）。
 * 纪律：Blob 为 UTF-8 Key 字节（不含 NUL）；指纹 = SHA-256 尾 8 位十六进制，
 * 仅用于 UI 识别与轮换检测——哈希不可用于恢复原文。 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define ASR_CRED_TARGET L"StickS3/ZhipuASR/ApiKey"
#define ASR_CRED_TARGET_TEST L"StickS3Test/ZhipuASR/ApiKey"

typedef enum {
    ASR_CRED_OK = 0,
    ASR_CRED_NOT_FOUND = -1, /* 凭据不存在（CredRead/Delete） */
    ASR_CRED_ERROR = -2,     /* 其他 Win32/CNG 失败 */
    ASR_CRED_PARAM = -3,
    ASR_CRED_NOMEM = -4
} asr_cred_status_t;

/* 存储（覆盖既有值） */
asr_cred_status_t asr_cred_store_named(const wchar_t *target, const char *key);
/* 读取：out 为 NUL 结尾 UTF-8；调用方负责用后清零 */
asr_cred_status_t asr_cred_load_named(const wchar_t *target, char *out, size_t cap);
asr_cred_status_t asr_cred_delete_named(const wchar_t *target);

/* 默认 TargetName 封装 */
asr_cred_status_t asr_cred_store(const char *key);
asr_cred_status_t asr_cred_load(char *out, size_t cap);
asr_cred_status_t asr_cred_delete(void);

/* SHA-256 尾 8 位十六进制指纹（out 需 ≥9 字节，NUL 结尾） */
asr_cred_status_t asr_cred_fingerprint(const char *key, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* ASR_CREDENTIAL_H */
