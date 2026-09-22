#include "asr_transport.h"

#include <stdio.h>
#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>

#pragma comment(lib, "winhttp.lib")

static int is_timeout_error(DWORD err) {
    return err == ERROR_WINHTTP_TIMEOUT ||
           err == ERROR_WINHTTP_OPERATION_CANCELLED * 0 + 12002; /* 显式主路径 */
}

/* UTF-8/ASCII → UTF-16（HeapAlloc，调用方 HeapFree） */
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

asr_status_t asr_http_post(const asr_http_req_t *req,
                           const uint8_t *body, size_t body_len,
                           char *resp, size_t resp_cap, int *http_status) {
    if (req == NULL || req->host == NULL || req->path == NULL ||
        req->content_type == NULL || http_status == NULL ||
        resp == NULL || resp_cap == 0 || (body == NULL && body_len != 0)) {
        return ASR_PARAM;
    }
    *http_status = 0;
    resp[0] = '\0';
    if (req->timeout_ms == 0) return ASR_PARAM;

    DWORD last_err = 0;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    wchar_t *whost = NULL, *wpath = NULL, *wct = NULL, *wauth = NULL, *wmethod = NULL;
    asr_status_t result = ASR_TRANSPORT;

    whost = to_wide(req->host);
    wpath = to_wide(req->path);
    wct = to_wide(req->content_type);
    wmethod = to_wide(req->method != NULL ? req->method : "POST");
    if (whost == NULL || wpath == NULL || wct == NULL || wmethod == NULL) goto done;

    hSession = WinHttpOpen(L"StickS3-ASR", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession == NULL) {
        last_err = GetLastError();
        goto done;
    }

    hConnect = WinHttpConnect(hSession, whost, req->port, 0);
    if (hConnect == NULL) {
        last_err = GetLastError();
        goto done;
    }

    hRequest = WinHttpOpenRequest(hConnect, wmethod, wpath, NULL, NULL, NULL,
                                  req->secure ? WINHTTP_FLAG_SECURE : 0);
    if (hRequest == NULL) {
        last_err = GetLastError();
        goto done;
    }
    if (!WinHttpSetTimeouts(hRequest, (int)req->timeout_ms, (int)req->timeout_ms,
                            (int)req->timeout_ms, (int)req->timeout_ms)) {
        last_err = GetLastError();
        goto done;
    }

    {
        wchar_t hdr[1024];
        int hn = _snwprintf(hdr, 1023, L"Content-Type: %s\r\n", wct);
        if (hn < 0) goto done;
        if (req->api_key != NULL) {
            wauth = to_wide(req->api_key);
            if (wauth == NULL) goto done;
            int an = _snwprintf(hdr + hn, 1023 - hn,
                                L"Authorization: Bearer %s\r\n", wauth);
            if (an < 0) goto done;
            hn += an;
        }
        if (!WinHttpAddRequestHeaders(hRequest, hdr, (DWORD)-1L,
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            last_err = GetLastError();
            goto done;
        }
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0)) {
        last_err = GetLastError();
        goto done;
    }
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        last_err = GetLastError();
        goto done;
    }

    {
        DWORD status_code = 0, size = sizeof(status_code);
        if (!WinHttpQueryHeaders(hRequest,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            last_err = GetLastError();
            goto done;
        }
        *http_status = (int)status_code;

        size_t total = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
                last_err = GetLastError();
                goto done;
            }
            if (avail == 0) break;
            DWORD read = 0;
            uint8_t chunk[4096];
            if (avail > sizeof(chunk)) avail = sizeof(chunk);
            if (!WinHttpReadData(hRequest, chunk, avail, &read)) {
                last_err = GetLastError();
                goto done;
            }
            if (read == 0) break;
            const size_t take = (total + read < resp_cap - 1)
                                    ? read
                                    : (resp_cap - 1 > total ? resp_cap - 1 - total : 0);
            if (take > 0) {
                memcpy(resp + total, chunk, take);
                total += take;
            }
        }
        resp[total] = '\0';
    }

    result = ASR_OK;

done:
    if (result != ASR_OK) {
        result = is_timeout_error(last_err) ? ASR_TIMEOUT : ASR_TRANSPORT;
    }
    if (wauth) HeapFree(GetProcessHeap(), 0, wauth);
    if (wmethod) HeapFree(GetProcessHeap(), 0, wmethod);
    if (wct) HeapFree(GetProcessHeap(), 0, wct);
    if (wpath) HeapFree(GetProcessHeap(), 0, wpath);
    if (whost) HeapFree(GetProcessHeap(), 0, whost);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return result;
}
