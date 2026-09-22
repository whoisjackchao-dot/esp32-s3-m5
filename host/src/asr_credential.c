#include "asr_credential.h"

#include <windows.h>
#include <wincred.h>
#include <bcrypt.h>

#include <stdlib.h>
#include <stdint.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

static int is_not_found(DWORD err) { return err == ERROR_NOT_FOUND; }

static wchar_t *to_wide(const char *s) {
    if (s == NULL) return NULL;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(wchar_t));
    if (w == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) <= 0) {
        HeapFree(GetProcessHeap(), 0, w);
        return NULL;
    }
    return w;
}

asr_cred_status_t asr_cred_store_named(const wchar_t *target, const char *key) {
    if (target == NULL || key == NULL || *key == '\0') return ASR_CRED_PARAM;
    const size_t klen = strlen(key);
    CREDENTIALW cred;
    memset(&cred, 0, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = (LPWSTR)target;
    cred.CredentialBlobSize = (DWORD)klen;
    cred.CredentialBlob = (LPBYTE)key;
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&cred, 0)) {
        return GetLastError() == ERROR_NOT_FOUND ? ASR_CRED_NOT_FOUND : ASR_CRED_ERROR;
    }
    return ASR_CRED_OK;
}

asr_cred_status_t asr_cred_load_named(const wchar_t *target, char *out, size_t cap) {
    if (target == NULL || out == NULL || cap == 0) return ASR_CRED_PARAM;
    out[0] = '\0';
    PCREDENTIALW p = NULL;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &p)) {
        const DWORD err = GetLastError();
        return is_not_found(err) ? ASR_CRED_NOT_FOUND : ASR_CRED_ERROR;
    }
    asr_cred_status_t result = ASR_CRED_OK;
    const size_t blen = p->CredentialBlobSize;
    if (p->CredentialBlob == NULL || blen == 0) {
        result = ASR_CRED_ERROR;
    } else if (blen + 1 > cap) {
        result = ASR_CRED_NOMEM;
    } else {
        memcpy(out, p->CredentialBlob, blen);
        out[blen] = '\0';
    }
    /* Blob 含 Key 原文：释放前清零（DPAPI 已解密的内存纪律） */
    if (p->CredentialBlob != NULL && p->CredentialBlobSize > 0) {
        SecureZeroMemory(p->CredentialBlob, p->CredentialBlobSize);
    }
    CredFree(p);
    return result;
}

asr_cred_status_t asr_cred_delete_named(const wchar_t *target) {
    if (target == NULL) return ASR_CRED_PARAM;
    if (!CredDeleteW(target, CRED_TYPE_GENERIC, 0)) {
        const DWORD err = GetLastError();
        return is_not_found(err) ? ASR_CRED_NOT_FOUND : ASR_CRED_ERROR;
    }
    return ASR_CRED_OK;
}

asr_cred_status_t asr_cred_store(const char *key) {
    return asr_cred_store_named(ASR_CRED_TARGET, key);
}

asr_cred_status_t asr_cred_load(char *out, size_t cap) {
    return asr_cred_load_named(ASR_CRED_TARGET, out, cap);
}

asr_cred_status_t asr_cred_delete(void) {
    return asr_cred_delete_named(ASR_CRED_TARGET);
}

asr_cred_status_t asr_cred_fingerprint(const char *key, char *out, size_t cap) {
    if (key == NULL || out == NULL || cap < 9) return ASR_CRED_PARAM;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS st;
    uint8_t digest[32];

    st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (st < 0) return ASR_CRED_ERROR;
    st = BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    if (st >= 0) {
        st = BCryptHashData(hash, (PUCHAR)key, (ULONG)strlen(key), 0);
        if (st >= 0) {
            st = BCryptFinishHash(hash, digest, sizeof(digest), 0);
        }
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) return ASR_CRED_ERROR;

    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < 4; i += 1) {
        const uint8_t b = digest[28 + i];
        out[i * 2] = hexd[b >> 4];
        out[i * 2 + 1] = hexd[b & 0x0F];
    }
    out[8] = '\0';
    return ASR_CRED_OK;
}
