// StickS3 x OpenCode — M3 故事 4：纵切第三步（ASR 真实调用 → 桥接追加草稿）
// 输入：16kHz 单声道 WAV；输出：text 追加到绑定 OpenCode 会话草稿（不提交）。
// Key：环境变量 STICKS3_ASR_KEY（隐藏输入注入，用后清除，不进命令行）。
// 桥接：环境变量同 host_entry（URL/TOKEN/TUI_EPOCH/SESSION_ID）。
// 语义：ASR_OK→追加；EMPTY_TEXT→不填入退出；失败→按类报错退出（手动重试由
// 重新运行承担，M05 状态机将在单进程宿主增量中接管协调）。
#include "asr_adapter.h"
#include "asr_transport.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace {

const char* status_name(asr_status_t s) {
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

// 环境变量必须走宽字符 API：ANSI 版本会把 CJK 文本变成 '?'（host_entry 实测缺陷）
std::string envOrUtf8(const wchar_t* name, bool& ok) {
    wchar_t buf[2048];
    const DWORD n = GetEnvironmentVariableW(name, buf, sizeof(buf) / sizeof(wchar_t));
    if (n == 0 || n >= sizeof(buf) / sizeof(wchar_t)) { ok = false; return {}; }
    ok = true;
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), s.data(), len, nullptr, nullptr);
    return s;
}

std::string randomOperationID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    static const char* hex = "0123456789abcdef";
    std::string id = "op_";
    for (int i = 0; i < 29; ++i) { id += hex[gen() & 0xF]; }
    return id;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
                else { out += static_cast<char>(c); }
        }
    }
    return out;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: voice_append.exe <wav path>\n");
        return 2;
    }
    bool ok = false;
    const std::string key     = envOrUtf8(L"STICKS3_ASR_KEY", ok);
    const bool hasKey = ok && !key.empty();
    const std::string url     = envOrUtf8(L"STICKS3_BRIDGE_URL", ok);
    const bool hasUrl = ok;
    const std::string token   = envOrUtf8(L"STICKS3_BRIDGE_TOKEN", ok);
    const bool hasToken = ok;
    const std::string epoch   = envOrUtf8(L"STICKS3_TUI_EPOCH", ok);
    const bool hasEpoch = ok;
    const std::string session = envOrUtf8(L"STICKS3_SESSION_ID", ok);
    const bool hasSession = ok;
    if (!hasKey || !hasUrl || !hasToken || !hasEpoch || !hasSession) {
        std::fprintf(stderr, "缺少环境变量: STICKS3_ASR_KEY / BRIDGE_URL / TOKEN / TUI_EPOCH / SESSION_ID\n");
        return 2;
    }

    // ① 读 WAV
    FILE* f = _wfopen(argv[1], L"rb");
    if (!f) { std::fprintf(stderr, "[FAIL] 打不开音频 %ls\n", argv[1]); return 2; }
    _fseeki64(f, 0, SEEK_END);
    const long long flen = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (flen <= 0 || flen > 25 * 1024 * 1024) {
        std::fprintf(stderr, "[FAIL] 音频大小异常 %lld\n", flen);
        std::fclose(f);
        return 2;
    }
    std::string wav(static_cast<size_t>(flen), '\0');
    if (std::fread(wav.data(), 1, wav.size(), f) != wav.size()) {
        std::fclose(f);
        std::fprintf(stderr, "[FAIL] 音频读取失败\n");
        return 2;
    }
    std::fclose(f);
    std::printf("[1] 音频 %ls (%lld 字节)\n", argv[1], flen);

    // ② multipart + 真实 ASR（20s，契约 C09）
    std::string body(wav.size() + 4096, '\0');
    const size_t bodyLen = asr_multipart_build(
        reinterpret_cast<const uint8_t*>(wav.data()), wav.size(),
        reinterpret_cast<uint8_t*>(body.data()), body.size());
    if (bodyLen == 0) { std::fprintf(stderr, "[FAIL] multipart 构造失败\n"); return 2; }
    body.resize(bodyLen);

    asr_http_req_t req;
    memset(&req, 0, sizeof(req));
    req.host = "open.bigmodel.cn";
    req.port = 443;
    req.secure = 1;
    req.path = "/api/paas/v4/audio/transcriptions";
    req.method = "POST";
    req.api_key = key.c_str();
    req.content_type = asr_content_type();
    req.timeout_ms = 20000;

    std::string resp(65536, '\0');
    int httpStatus = 0;
    const asr_status_t st = asr_http_post(&req, reinterpret_cast<const uint8_t*>(body.data()),
                                          bodyLen, resp.data(), resp.size(), &httpStatus);
    SecureZeroMemory(body.data(), body.size());
    if (st != ASR_OK) {
        std::fprintf(stderr, "[FAIL] ASR 传输: %s (HTTP %d)\n", status_name(st), httpStatus);
        return 1;
    }
    const int textLen = static_cast<int>(resp.size());
    (void)textLen;
    char text[4096];
    const asr_status_t cls = asr_classify(httpStatus, resp.c_str(), strlen(resp.c_str()),
                                          text, sizeof(text));
    printf("[2] ASR HTTP %d class=%s\n", httpStatus, status_name(cls));
    if (cls != ASR_OK) {
        printf("[FAIL] 识别未产出可填入文本（%s）\n", status_name(cls));
        return 1;
    }
    printf("text: %s\n", text);

    // ③ 桥接追加（仅 loopback，契约 BR；10s 接收上限）
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) { std::fprintf(stderr, "BRIDGE_URL 必须以 http:// 开头\n"); return 2; }
    const std::string rest = url.substr(prefix.size());
    const auto colon = rest.find(L':');
    if (colon == std::string::npos) { std::fprintf(stderr, "BRIDGE_URL 缺少端口\n"); return 2; }
    const std::string bhost = rest.substr(0, colon);
    const int bport = std::atoi(rest.substr(colon + 1).c_str());
    if ((bhost != "127.0.0.1" && bhost != "localhost") || bport <= 0 || bport > 65535) {
        std::fprintf(stderr, "桥接地址非法（仅允许 loopback）\n");
        return 2;
    }
    const std::string opID = randomOperationID();
    std::string append = "{\"protocolVersion\":1,\"tuiEpoch\":\"" + jsonEscape(epoch)
                       + "\",\"sessionID\":\"" + jsonEscape(session)
                       + "\",\"operationID\":\"" + opID
                       + "\",\"transactionGeneration\":1,\"text\":\"" + jsonEscape(text) + "\"}";
    const std::wstring whost(bhost.begin(), bhost.end());
    HINTERNET hs = WinHttpOpen(L"sticks3-voice/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) { std::fprintf(stderr, "WinHttpOpen 失败\n"); return 1; }
    HINTERNET conn = WinHttpConnect(hs, whost.c_str(), static_cast<INTERNET_PORT>(bport), 0);
    if (!conn) { std::fprintf(stderr, "WinHttpConnect 失败\n"); return 1; }
    HINTERNET hreq = WinHttpOpenRequest(conn, L"POST", L"/v1/draft/append", nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hreq) { std::fprintf(stderr, "WinHttpOpenRequest 失败\n"); return 1; }
    DWORD recvTimeout = 10000; /* 契约 §5：APPEND_DEADLINE */
    WinHttpSetOption(hreq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &recvTimeout, sizeof(recvTimeout));
    const std::wstring wheaders = L"Content-Type: application/json\r\nAuthorization: Bearer "
                                + std::wstring(token.begin(), token.end()) + L"\r\n";
    const BOOL sent = WinHttpSendRequest(hreq, wheaders.c_str(),
                                         static_cast<DWORD>(wheaders.size()),
                                         reinterpret_cast<LPVOID>(append.data()),
                                         static_cast<DWORD>(append.size()),
                                         static_cast<DWORD>(append.size()), 0);
    if (!sent) { std::fprintf(stderr, "[FAIL] 桥接发送失败\n"); return 1; }
    if (!WinHttpReceiveResponse(hreq, nullptr)) { std::fprintf(stderr, "[FAIL] 桥接响应失败\n"); return 1; }

    DWORD statusCode = 0, sz = sizeof(statusCode);
    WinHttpQueryHeaders(hreq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);
    std::string responseBody;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hreq, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hreq, chunk.data(), avail, &read)) break;
        responseBody.append(chunk, 0, read);
    }
    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(hs);

    printf("[3] 桥接 HTTP %lu\n%s\n", static_cast<unsigned long>(statusCode), responseBody.c_str());
    const bool applied = statusCode == 200 && responseBody.find("\"applied\"") != std::string::npos;
    if (applied) printf("[PASS] 已填入草稿（未提交，请手动发送）\n");
    else if (responseBody.find("\"processing\"") != std::string::npos) printf("结果: 处理中\n");
    else if (responseBody.find("\"uncertain\"") != std::string::npos) printf("结果: 不确定，请人工检查草稿\n");
    else printf("[FAIL] 未 applied\n");
    return applied ? 0 : 1;
}
