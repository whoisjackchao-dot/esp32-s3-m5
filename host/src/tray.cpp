// tray.cpp — M4/S4.7 托盘（Win32 Shell_NotifyIconW）。
//
// S4.7a：托盘生存周期（图标/消息循环/退出）。
// S4.7b：读 bridge.json（M1 契约 0600 描述文件）→ WinHTTP GET /v1/health → 菜单显示连接态。
// 依据 HLD §11「M07 托盘与凭据」与 Q06 托盘分工；无 WinRT 依赖。
// bridge.json 候选路径可由环境变量 STICKS3_BRIDGE_JSON 覆盖。
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "tray_parse.h"

namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT IDM_STATUS = 1001;
constexpr UINT IDM_REFRESH = 1002;
constexpr UINT IDM_KEY_STORE = 1003;
constexpr UINT IDM_KEY_DELETE = 1004;
constexpr UINT IDM_EXIT = 1005;
constexpr UINT IDM_RETRY_RECOGNIZE = 1006;
constexpr UINT IDM_RETRY_APPEND = 1007;

NOTIFYICONDATAW g_nid{};
UINT g_taskbar_created = 0;
std::wstring g_status = L"状态：未知";
const char* g_status_code = "UNKNOWN";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string read_text_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 65536) {
        CloseHandle(h);
        return {};
    }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return {};
    data.resize(read);
    return data;
}

std::wstring bridge_json_path() {
    wchar_t env[1024];
    const DWORD n = GetEnvironmentVariableW(L"STICKS3_BRIDGE_JSON", env, ARRAYSIZE(env));
    if (n > 0 && n < ARRAYSIZE(env)) return env;
    const wchar_t* candidates[] = {
        L"\\\\wsl.localhost\\Ubuntu\\home\\home\\.local\\share\\sticks3-opencode\\bridge.json",
        L"\\\\wsl$\\Ubuntu\\home\\home\\.local\\share\\sticks3-opencode\\bridge.json",
    };
    for (const wchar_t* c : candidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return {};
}

// GET {url}/v1/health（Bearer token）；返回 200 且响应含 "alive":true。
bool health_ok(const std::string& url, const std::string& token) {
    const std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) return false;
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);

    HINTERNET session = WinHttpOpen(L"StickS3Tray/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    bool ok = false;
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", L"/v1/health", nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (req) {
            const std::wstring header = L"Authorization: Bearer " + widen(token);
            if (WinHttpSendRequest(req, header.c_str(), static_cast<DWORD>(-1),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD status = 0, len = sizeof(status);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                std::string body;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD got = 0;
                    if (!WinHttpReadData(req, chunk.data(), avail, &got) || got == 0) break;
                    body.append(chunk.data(), got);
                }
                ok = (status == 200) && body.find("\"alive\":true") != std::string::npos;
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

std::wstring host_json_path() {
    wchar_t env[1024];
    const DWORD n = GetEnvironmentVariableW(L"STICKS3_HOST_JSON", env, ARRAYSIZE(env));
    if (n > 0 && n < ARRAYSIZE(env)) return env;
    wchar_t lad[1024];
    const DWORD m = GetEnvironmentVariableW(L"LOCALAPPDATA", lad, ARRAYSIZE(lad));
    if (m > 0 && m < ARRAYSIZE(lad)) {
        std::wstring p = std::wstring(lad) + L"\\sticks3\\host.json";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

bool host_post(const std::string& url, const std::string& token, const wchar_t* path,
               DWORD& status_out, std::string& body_out) {
    const std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) return false;
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    HINTERNET session = WinHttpOpen(L"StickS3Tray/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr,
                                    nullptr, 0);
    if (!session) return false;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
    bool ok = false;
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (req) {
            const std::wstring header = L"Authorization: Bearer " + widen(token);
            if (WinHttpSendRequest(req, header.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA,
                                   0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD st = 0, len = sizeof(st);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &st, &len, WINHTTP_NO_HEADER_INDEX);
                status_out = st;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD got = 0;
                    if (!WinHttpReadData(req, chunk.data(), avail, &got) || got == 0) break;
                    body_out.append(chunk.data(), got);
                }
                ok = true;
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

void retry_action(const wchar_t* path, const wchar_t* label) {
    const std::wstring hp = host_json_path();
    if (hp.empty()) {
        MessageBoxW(nullptr, L"找不到 host.json（state_host 未运行？）", label, MB_ICONWARNING);
        return;
    }
    const std::string json = read_text_file(hp);
    std::string url, token;
    if (json.empty() || !tray::json_string(json, "url", url) ||
        !tray::json_string(json, "token", token)) {
        MessageBoxW(nullptr, L"host.json 不可读或字段缺失", label, MB_ICONWARNING);
        return;
    }
    DWORD st = 0;
    std::string body;
    if (!host_post(url, token, path, st, body)) {
        MessageBoxW(nullptr, L"无法连接 state_host", label, MB_ICONERROR);
        return;
    }
    const std::wstring msg = L"HTTP " + std::to_wstring(st) + L"\n" + widen(body);
    MessageBoxW(nullptr, msg.c_str(), label, MB_ICONINFORMATION);
}

void refresh_status() {
    const std::wstring path = bridge_json_path();
    if (path.empty()) {
        g_status = L"状态：未连接（无 bridge.json，插件未运行？）";
        g_status_code = "NO_BRIDGE";
        return;
    }
    const std::string json = read_text_file(path);
    std::string url, token;
    if (json.empty() || !tray::json_string(json, "url", url) || !tray::json_string(json, "token", token)) {
        g_status = L"状态：bridge.json 不可读或字段缺失";
        g_status_code = "DESCRIPTOR_BAD";
        return;
    }
    if (health_ok(url, token)) {
        g_status = L"状态：桥接健康（已连接）";
        g_status_code = "HEALTHY";
    } else {
        g_status = L"状态：桥接不可达（进程不在/端口关）";
        g_status_code = "UNREACHABLE";
    }
}

// 启动同目录下的凭据管理工具（Key 由控制台隐藏输入，不进命令行）。
void launch_cred_tool(const wchar_t* subcommand) {
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';
    const std::wstring tool = std::wstring(exe) + L"asr_cred_tool.exe";
    if (GetFileAttributesW(tool.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"找不到 asr_cred_tool.exe（请先构建 host）", L"StickS3", MB_ICONWARNING);
        return;
    }
    std::wstring cmdline = L"\"" + tool + L"\" " + subcommand;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(tool.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        MessageBoxW(nullptr, L"启动凭据工具失败", L"StickS3", MB_ICONERROR);
    }
}

void add_tray_icon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"StickS3 托盘");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void remove_tray_icon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void show_menu(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, IDM_STATUS, g_status.c_str());
    AppendMenuW(menu, MF_STRING, IDM_REFRESH, L"刷新状态");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_KEY_STORE, L"配置 ASR Key…");
    AppendMenuW(menu, MF_STRING, IDM_KEY_DELETE, L"删除 ASR Key");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_RETRY_RECOGNIZE, L"重试识别");
    AppendMenuW(menu, MF_STRING, IDM_RETRY_APPEND, L"重试填入");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbar_created != 0 && msg == g_taskbar_created) {
        add_tray_icon(hwnd);
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
                show_menu(hwnd);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDM_REFRESH:
                    refresh_status();
                    return 0;
                case IDM_KEY_STORE:
                    launch_cred_tool(L"store");
                    return 0;
                case IDM_KEY_DELETE:
                    launch_cred_tool(L"delete");
                    return 0;
                case IDM_RETRY_RECOGNIZE:
                    retry_action(L"/v1/retry/recognize", L"重试识别");
                    return 0;
                case IDM_RETRY_APPEND:
                    retry_action(L"/v1/retry/append", L"重试填入");
                    return 0;
                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    return 0;
            }
        case WM_DESTROY:
            remove_tray_icon();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
}  // namespace

int wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    const wchar_t* cls = L"StickS3TrayWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = cls;
    if (!RegisterClassW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, cls, L"StickS3", WS_OVERLAPPED, 0, 0, 0, 0,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    refresh_status();
    add_tray_icon(hwnd);
    std::printf("tray: running | status_code=%s\n", g_status_code);
    std::fflush(stdout);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
