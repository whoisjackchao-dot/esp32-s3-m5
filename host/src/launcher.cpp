// launcher.cpp — M4/S4.7 启动器（Windows 侧拉起器，Slice 1）
//
// 冻结依据：contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md
//   D1=A 仅 Windows 侧拉起器；D2=a 常驻监督；D3=a 不自启；D4=a 持句柄并终止；
//   D6=a 描述符发现（env 优先 + 发行版可配）；D7=a 收编子进程输出统一落盘。
// 本 Slice 不含：崩溃重启（Slice 2）、shutdown 端点（D4-b 未选）、一切自启机制。
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kMutexName[] = L"Global\\StickS3Launcher";
constexpr DWORD kDescriptorWaitMs = 10000;
constexpr DWORD kPollMs = 250;
constexpr ULONGLONG kRotateBytes = 1024ull * 1024ull;
constexpr int kKeepRotated = 3;
constexpr unsigned kBackoffBaseMs = 1000;
constexpr unsigned kBackoffCapMs = 30000;
constexpr ULONGLONG kStableResetMs = 60000;

HANDLE g_job = nullptr;
volatile LONG g_stop = 0;

ULONGLONG now_ms() {
    return GetTickCount64();
}

void log_line(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    std::printf("[launcher] %s\n", buf);
    std::fflush(stdout);
}

BOOL WINAPI on_console(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            InterlockedExchange(&g_stop, 1);
            return TRUE;
        default:
            return FALSE;
    }
}

std::wstring env_str(const wchar_t* name) {
    wchar_t buf[1024] = {0};
    const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::wstring p(path, n);
    const size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring(L".") : p.substr(0, cut);
}

bool path_exists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring logs_dir() {
    const std::wstring base = env_str(L"LOCALAPPDATA");
    return base.empty() ? std::wstring() : base + L"\\sticks3\\logs";
}

std::wstring timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    _snwprintf_s(buf, sizeof(buf) / sizeof(buf[0]), _TRUNCATE, L"%04u%02u%02u-%02u%02u%02u",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

void rotate_if_needed(const std::wstring& dir, const std::wstring& child) {
    const std::wstring pattern = dir + L"\\" + child + L"-*.log";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    std::wstring newest;
    ULONGLONG newest_size = 0;
    do {
        const std::wstring name(fd.cFileName);
        if (name > newest) {
            newest = name;
            newest_size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (newest.empty() || newest_size < kRotateBytes) {
        return;
    }
    DeleteFileW((dir + L"\\" + child + L"." + std::to_wstring(kKeepRotated) + L".log").c_str());
    for (int i = kKeepRotated - 1; i >= 1; --i) {
        MoveFileW((dir + L"\\" + child + L"." + std::to_wstring(i) + L".log").c_str(),
                  (dir + L"\\" + child + L"." + std::to_wstring(i + 1) + L".log").c_str());
    }
    MoveFileW((dir + L"\\" + newest).c_str(), (dir + L"\\" + child + L".1.log").c_str());
    log_line("rotate: %ls over %llu bytes -> %ls.1.log", child.c_str(), newest_size, child.c_str());
}

HANDLE open_child_log(const std::wstring& dir, const std::wstring& child, std::wstring* out_path) {
    rotate_if_needed(dir, child);
    const std::wstring path = dir + L"\\" + child + L"-" + timestamp() + L".log";
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    const HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, &sa,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out_path != nullptr) {
        *out_path = path;
    }
    return h;
}

std::wstring resolve_bridge_json() {
    const std::wstring from_env = env_str(L"STICKS3_BRIDGE_JSON");
    if (!from_env.empty() && path_exists(from_env)) {
        return from_env;
    }
    std::wstring distro = env_str(L"STICKS3_WSL_DISTRO");
    if (distro.empty()) {
        distro = L"Ubuntu";
    }
    const std::wstring tail = L"\\home\\home\\.local\\share\\sticks3-opencode\\bridge.json";
    const std::wstring candidates[2] = {L"\\\\wsl.localhost\\" + distro + tail,
                                        L"\\\\wsl$\\" + distro + tail};
    for (const std::wstring& c : candidates) {
        if (path_exists(c)) {
            return c;
        }
    }
    return std::wstring();
}

struct Child {
    std::wstring name;
    PROCESS_INFORMATION pi{};
    HANDLE log = INVALID_HANDLE_VALUE;
    std::wstring log_path;
    bool running = false;
    unsigned restarts = 0;
    ULONGLONG started_ms = 0;
    ULONGLONG next_try_ms = 0;
};

bool start_child(Child* child, const std::wstring& dir, const std::wstring& logs) {
    const std::wstring exe = dir + L"\\" + child->name + L".exe";
    if (!path_exists(exe)) {
        log_line("child missing: %ls", exe.c_str());
        return false;
    }
    if (child->log == INVALID_HANDLE_VALUE) {
        child->log = open_child_log(logs, child->name, &child->log_path);
    }
    if (child->log == INVALID_HANDLE_VALUE) {
        log_line("child log open failed for %ls (err=%lu)", child->name.c_str(), GetLastError());
        return false;
    }
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    si.StartupInfo.hStdOutput = child->log;
    si.StartupInfo.hStdError = child->log;
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_buf(attr_size);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
    bool attr_ok = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size) != 0;
    HANDLE inherit_list[1] = {child->log};
    if (attr_ok) {
        attr_ok = UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_list,
                                            sizeof(inherit_list), nullptr, nullptr) != 0;
    }
    std::wstring cmdline = L"\"" + exe + L"\"";
    std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(L'\0');
    const DWORD flags = CREATE_NO_WINDOW | (attr_ok ? EXTENDED_STARTUPINFO_PRESENT : 0);
    const BOOL ok = CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE, flags,
                                   nullptr, dir.c_str(), &si.StartupInfo, &child->pi);
    if (attr_ok) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
    }
    if (!ok) {
        log_line("child start failed: %ls (err=%lu)", child->name.c_str(), GetLastError());
        CloseHandle(child->log);
        child->log = INVALID_HANDLE_VALUE;
        return false;
    }
    if (g_job != nullptr) {
        AssignProcessToJobObject(g_job, child->pi.hProcess);
    }
    child->running = true;
    child->started_ms = now_ms();
    log_line("started %ls pid=%lu -> %ls", child->name.c_str(), child->pi.dwProcessId,
             child->log_path.c_str());
    return true;
}

bool create_kill_on_close_job() {
    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job == nullptr) {
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info,
                                   sizeof(info)) != 0;
}

void stop_children(Child* children, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (children[i].log != INVALID_HANDLE_VALUE) {
            CloseHandle(children[i].log);
            children[i].log = INVALID_HANDLE_VALUE;
        }
    }
    if (g_job != nullptr) {
        CloseHandle(g_job);
        g_job = nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        if (children[i].pi.hProcess != nullptr) {
            WaitForSingleObject(children[i].pi.hProcess, 3000);
            CloseHandle(children[i].pi.hThread);
            CloseHandle(children[i].pi.hProcess);
        }
    }
}

void poll_children(Child* children, size_t count) {
    const ULONGLONG now = now_ms();
    for (size_t i = 0; i < count; ++i) {
        Child& c = children[i];
        if (!c.running || c.pi.hProcess == nullptr) {
            continue;
        }
        if (WaitForSingleObject(c.pi.hProcess, 0) != WAIT_OBJECT_0) {
            continue;
        }
        DWORD code = 0;
        GetExitCodeProcess(c.pi.hProcess, &code);
        CloseHandle(c.pi.hThread);
        CloseHandle(c.pi.hProcess);
        c.pi = PROCESS_INFORMATION{};
        c.running = false;
        const ULONGLONG alive = now - c.started_ms;
        if (alive >= kStableResetMs) {
            c.restarts = 0;
        }
        const unsigned shift = c.restarts < 5 ? c.restarts : 5;
        unsigned delay = kBackoffBaseMs << shift;
        if (delay > kBackoffCapMs) {
            delay = kBackoffCapMs;
        }
        ++c.restarts;
        c.next_try_ms = now + delay;
        log_line("child exited: %ls code=%lu (alive=%llums) -> restart #%u in %ums",
                 c.name.c_str(), code, alive, c.restarts, delay);
    }
}

}  // namespace

int wmain() {
    SetConsoleCtrlHandler(on_console, TRUE);
    log_line("start (Slice 1: single-instance + ordered start + log capture + job cleanup)");

    const HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr) {
        log_line("mutex create failed (err=%lu)", GetLastError());
        return 3;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log_line("already running - exiting");
        CloseHandle(mutex);
        return 2;
    }

    const std::wstring dir = exe_dir();
    const std::wstring logs = logs_dir();
    if (!logs.empty()) {
        CreateDirectoryW((env_str(L"LOCALAPPDATA") + L"\\sticks3").c_str(), nullptr);
        CreateDirectoryW(logs.c_str(), nullptr);
    }
    log_line("exe_dir=%ls", dir.c_str());

    const std::wstring bridge = resolve_bridge_json();
    if (bridge.empty()) {
        log_line("WARN bridge.json not found (env STICKS3_BRIDGE_JSON / STICKS3_WSL_DISTRO)");
    } else {
        log_line("bridge.json=%ls", bridge.c_str());
        SetEnvironmentVariableW(L"STICKS3_BRIDGE_JSON", bridge.c_str());
    }

    if (!create_kill_on_close_job()) {
        log_line("WARN job object unavailable (err=%lu) - children will not be auto-stopped",
                 GetLastError());
    }

    Child children[2] = {{L"state_host"}, {L"tray"}};
    if (!start_child(&children[0], dir, logs)) {
        stop_children(children, 2);
        CloseHandle(mutex);
        return 4;
    }
    const std::wstring descriptor = env_str(L"LOCALAPPDATA") + L"\\sticks3\\host.json";
    DWORD waited = 0;
    while (waited < kDescriptorWaitMs && path_exists(descriptor) == false &&
           InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        Sleep(kPollMs);
        waited += kPollMs;
    }
    log_line("descriptor %s after %lums", path_exists(descriptor) ? "ready" : "NOT ready",
             waited);
    start_child(&children[1], dir, logs);

    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        poll_children(children, 2);
        const ULONGLONG now = now_ms();
        for (size_t i = 0; i < 2; ++i) {
            Child& c = children[i];
            if (!c.running && now >= c.next_try_ms) {
                if (!start_child(&c, dir, logs)) {
                    c.next_try_ms = now + kBackoffCapMs;
                }
            }
        }
        Sleep(kPollMs);
    }
    log_line("stopping");
    stop_children(children, 2);
    CloseHandle(mutex);
    log_line("exit");
    return 0;
}
