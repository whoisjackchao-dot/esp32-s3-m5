// StickS3 x OpenCode - M1 最小 Windows 入口
// 依据 contracts/M1-桥接契约冻结.md：env 传凭据（不进命令行/日志），一次 append，无自动重试。
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace {

// 环境变量必须走宽字符 API：ANSI 版本会把 CJK 文本变成 '?'（实测缺陷）
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

void printOutcome(const DWORD status, const std::string& body) {
    std::printf("HTTP %lu\n%s\n", static_cast<unsigned long>(status), body.c_str());
    if (body.find("\"applied\"") != std::string::npos) std::printf("结果: 已填入（未提交）\n");
    else if (body.find("\"processing\"") != std::string::npos) std::printf("结果: 处理中\n");
    else if (body.find("\"uncertain\"") != std::string::npos) std::printf("结果: 结果不确定，请人工检查\n");
    else if (status == 409) std::printf("结果: 已拒绝\n");
    else if (status == 429) std::printf("结果: 回执容量已满\n");
    else if (status == 401) std::printf("结果: 认证失败\n");
    else if (status == 415 || status == 400) std::printf("结果: 请求被拒\n");
    else std::printf("结果: 未识别的响应\n");
}

} // namespace

int wmain() {
    bool ok = false;
    const std::string url     = envOrUtf8(L"STICKS3_BRIDGE_URL", ok);
    const bool hasUrl = ok;
    const std::string token   = envOrUtf8(L"STICKS3_BRIDGE_TOKEN", ok);
    const bool hasToken = ok;
    const std::string epoch   = envOrUtf8(L"STICKS3_TUI_EPOCH", ok);
    const bool hasEpoch = ok;
    const std::string session = envOrUtf8(L"STICKS3_SESSION_ID", ok);
    const bool hasSession = ok;
    const std::string text    = envOrUtf8(L"STICKS3_TEXT", ok);
    const bool hasText = ok;

    if (!hasUrl || !hasToken || !hasEpoch || !hasSession || !hasText) {
        std::fprintf(stderr, "缺少环境变量:");
        if (!hasUrl) std::fprintf(stderr, " STICKS3_BRIDGE_URL");
        if (!hasToken) std::fprintf(stderr, " STICKS3_BRIDGE_TOKEN");
        if (!hasEpoch) std::fprintf(stderr, " STICKS3_TUI_EPOCH");
        if (!hasSession) std::fprintf(stderr, " STICKS3_SESSION_ID");
        if (!hasText) std::fprintf(stderr, " STICKS3_TEXT");
        std::fprintf(stderr, "\n");
        return 2;
    }

    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) { std::fprintf(stderr, "STICKS3_BRIDGE_URL 必须以 http:// 开头\n"); return 2; }
    const std::string rest = url.substr(prefix.size());
    const auto colon = rest.find(L':');
    if (colon == std::string::npos) { std::fprintf(stderr, "STICKS3_BRIDGE_URL 缺少端口\n"); return 2; }
    const std::string host = rest.substr(0, colon);
    const int port = std::atoi(rest.substr(colon + 1).c_str());
    if (host != "127.0.0.1" && host != "localhost") { std::fprintf(stderr, "仅允许 loopback 地址\n"); return 2; }
    if (port <= 0 || port > 65535) { std::fprintf(stderr, "端口非法\n"); return 2; }

    char checkBuf[8] = {};
    if (GetEnvironmentVariableA("STICKS3_CHECK", checkBuf, sizeof(checkBuf)) > 0) {
        const std::string masked = token.substr(0, 4) + "****";
        std::printf("配置检查:\n  bridge : %s\n  token  : %s\n  epoch  : %s\n  session: %s\n  text   : %zu 字节\n",
                    url.c_str(), masked.c_str(), epoch.c_str(), session.c_str(), text.size());
        return 0;
    }

    const std::string opID = randomOperationID();
    std::string body = "{\"protocolVersion\":1,\"tuiEpoch\":\"" + jsonEscape(epoch)
                     + "\",\"sessionID\":\"" + jsonEscape(session)
                     + "\",\"operationID\":\"" + opID
                     + "\",\"transactionGeneration\":1,\"text\":\"" + jsonEscape(text) + "\"}";

    const std::wstring whost(host.begin(), host.end());
    HINTERNET httpSession = WinHttpOpen(L"sticks3-host/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!httpSession) { std::fprintf(stderr, "WinHttpOpen 失败\n"); return 1; }
    HINTERNET connect = WinHttpConnect(httpSession, whost.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!connect) { std::fprintf(stderr, "WinHttpConnect 失败\n"); return 1; }
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/v1/draft/append", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { std::fprintf(stderr, "WinHttpOpenRequest 失败\n"); return 1; }

    // 契约 §5：APPEND_DEADLINE = 10 秒（接收等待上限）
    DWORD receiveTimeoutMs = 10000;
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &receiveTimeoutMs, sizeof(receiveTimeoutMs));

    const std::wstring wheaders = L"Content-Type: application/json\r\nAuthorization: Bearer "
                                + std::wstring(token.begin(), token.end()) + L"\r\n";
    const BOOL sent = WinHttpSendRequest(request, wheaders.c_str(), static_cast<DWORD>(wheaders.size()),
                                         reinterpret_cast<LPVOID>(body.data()), static_cast<DWORD>(body.size()),
                                         static_cast<DWORD>(body.size()), 0);
    if (!sent) { std::fprintf(stderr, "发送失败（桥接未运行或网络错误）\n"); return 1; }
    if (!WinHttpReceiveResponse(request, nullptr)) { std::fprintf(stderr, "接收响应失败\n"); return 1; }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

    std::string responseBody;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
        responseBody.append(chunk, 0, read);
    }

    printOutcome(statusCode, responseBody);
    const bool applied = statusCode == 200 && responseBody.find("\"applied\"") != std::string::npos;
    return applied ? 0 : 1;
}
