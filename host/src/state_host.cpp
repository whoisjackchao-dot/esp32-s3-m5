// state_host.cpp —— M4 S4.2 生产 host（常驻）。
// 流程：读 bridge.json → 连接 StickS3 BLE（CTRL 特征）→ 订阅桥接 SSE /v1/state/stream
//       → 每收到 BR-03 真实快照，按 snapshotRevision 去重 → state_merge → s3p_state_encode
//       → BLE 写 STATE(0x12)。
// 不写 READY、不启动录音会话；桥接/BLE 断开自动重连。
// 测试用：环境变量 STICKS3_HOST_SECONDS=N 令其在 N 秒后自行退出。
#include <windows.h>
#include <winhttp.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#pragma comment(lib, "winhttp.lib")

extern "C" {
#include "s3p_state.h"
#include "state_merge.h"
}
#include "bridge_snapshot.h"
#include "sse_parse.h"
#include "tray_parse.h"

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage::Streams;

static const wchar_t* SVC_STR = L"{22587f80-334a-53d6-beb8-8a6ace31af07}";
static const wchar_t* CTRL_STR = L"{14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0}";

static uint32_t g_last_rev = 0;
static uint32_t g_pushes = 0;

static void say(const char* msg) {
    std::printf("[state_host] %s\n", msg);
    std::fflush(stdout);
}

static bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

static std::string find_bridge_json() {
    const char* e = std::getenv("STICKS3_BRIDGE_JSON");
    if (e && *e) return e;
    const char* up = std::getenv("USERPROFILE");
    std::string c[3];
    if (up) c[0] = std::string(up) + "\\.local\\share\\sticks3-opencode\\bridge.json";
    c[1] = "\\\\wsl.localhost\\Ubuntu\\home\\home\\.local\\share\\sticks3-opencode\\bridge.json";
    c[2] = "\\\\wsl$\\Ubuntu\\home\\home\\.local\\share\\sticks3-opencode\\bridge.json";
    for (const auto& p : c) {
        std::string s;
        if (!p.empty() && read_file(p, s)) return p;
    }
    return {};
}

static bool parse_http_url(const std::string& url, std::string& host, int& port) {
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = url.substr(prefix.size());
    const auto colon = rest.find(':');
    if (colon == std::string::npos) return false;
    host = rest.substr(0, colon);
    port = std::atoi(rest.substr(colon + 1).c_str());
    return (host == "127.0.0.1" || host == "localhost") && port > 0 && port <= 65535;
}

static uint64_t parse_addr_from_id(const winrt::hstring& id) {
    const std::wstring s(id.c_str(), id.size());
    const auto pos = s.find(L"_");
    if (pos == std::wstring::npos) return 0;
    uint64_t addr = 0;
    int n = 0;
    for (auto i = pos + 1; i < s.size() && n < 12; ++i) {
        const wchar_t c = s[i];
        int v;
        if (c >= L'0' && c <= L'9') v = c - L'0';
        else if (c >= L'a' && c <= L'f') v = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') v = c - L'A' + 10;
        else break;
        addr = (addr << 4) | (uint64_t)v;
        ++n;
    }
    return n == 12 ? addr : 0;
}

static bool connect_ctrl(GattCharacteristic& out) {
    const winrt::guid svcGuid(SVC_STR);
    auto devs = DeviceInformation::FindAllAsync(GattDeviceService::GetDeviceSelectorFromUuid(svcGuid)).get();
    if (devs.Size() == 0) return false;
    auto di = devs.GetAt(0);
    if (!di.Pairing().IsPaired()) {
        auto pr = di.Pairing().PairAsync(DevicePairingProtectionLevel::None).get();
        if (pr.Status() != DevicePairingResultStatus::Paired &&
            pr.Status() != DevicePairingResultStatus::AlreadyPaired) {
            return false;
        }
    }
    const uint64_t addr = parse_addr_from_id(di.Id());
    if (addr == 0) return false;
    auto ble = BluetoothLEDevice::FromBluetoothAddressAsync(addr, BluetoothAddressType::Public).get();
    if (!ble) return false;
    auto session = GattSession::FromDeviceIdAsync(BluetoothDeviceId::FromId(ble.DeviceId())).get();
    if (session) {
        session.MaintainConnection(true);
        for (int i = 0; i < 50 && ble.ConnectionStatus() != BluetoothConnectionStatus::Connected; ++i) {
            Sleep(100);
        }
    }
    GattDeviceService svc{nullptr};
    auto try_disc = [&](BluetoothCacheMode mode) -> bool {
        try {
            auto res = ble.GetGattServicesForUuidAsync(svcGuid, mode).get();
            if (res.Status() == GattCommunicationStatus::Success && res.Services().Size() > 0) {
                svc = res.Services().GetAt(0);
                return true;
            }
        } catch (...) {
        }
        return false;
    };
    if (!try_disc(BluetoothCacheMode::Uncached) && !try_disc(BluetoothCacheMode::Cached)) return false;
    const winrt::guid ctrlGuid(CTRL_STR);
    auto cr = svc.GetCharacteristicsForUuidAsync(ctrlGuid, BluetoothCacheMode::Uncached).get();
    if (cr.Status() != GattCommunicationStatus::Success || cr.Characteristics().Size() == 0) {
        cr = svc.GetCharacteristicsForUuidAsync(ctrlGuid, BluetoothCacheMode::Cached).get();
    }
    if (cr.Status() != GattCommunicationStatus::Success || cr.Characteristics().Size() == 0) return false;
    out = cr.Characteristics().GetAt(0);
    return true;
}

static bool push_state(GattCharacteristic& ctrl, const br03_snapshot& in) {
    s3p_state_snapshot snap;
    state_merge(&snap, &in);
    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&snap, buf, sizeof(buf));
    if (n == 0) return false;
    Buffer b((uint32_t)n);
    b.Length((uint32_t)n);
    std::memcpy(b.data(), buf, n);
    const auto r = ctrl.WriteValueAsync(b, GattWriteOption::WriteWithResponse).get();
    if (r != GattCommunicationStatus::Success) return false;
    ++g_pushes;
    std::printf("[state_host] push #%u rev=%lu status=%u flags=0x%02x len=%u\n", g_pushes,
                (unsigned long)snap.revision, (unsigned)snap.status, (unsigned)snap.flags, (unsigned)n);
    std::fflush(stdout);
    return true;
}

static bool deadline_hit(DWORD deadline) {
    return deadline != 0 && GetTickCount() >= deadline;
}

static int run_host() {
    const char* sec = std::getenv("STICKS3_HOST_SECONDS");
    const DWORD deadline = (sec && std::atoi(sec) > 0) ? (GetTickCount() + (DWORD)std::atoi(sec) * 1000u) : 0;

    GattCharacteristic ctrl{nullptr};
    while (!deadline_hit(deadline)) {
        const std::string path = find_bridge_json();
        std::string js, url, token;
        if (path.empty() || !read_file(path, js) || !tray::json_string(js, "url", url) ||
            !tray::json_string(js, "token", token)) {
            say("bridge.json unavailable; retry");
            Sleep(2000);
            continue;
        }
        std::string host;
        int port = 0;
        if (!parse_http_url(url, host, port)) {
            say("bridge url invalid; retry");
            Sleep(2000);
            continue;
        }
        if (!ctrl) {
            try {
                if (!connect_ctrl(ctrl)) {
                    say("ble connect failed; retry");
                    Sleep(2000);
                    continue;
                }
            } catch (...) {
                say("ble connect exception; retry");
                Sleep(2000);
                continue;
            }
            say("ble connected (CTRL characteristic)");
        }

        say("subscribe /v1/state/stream");
        HINTERNET ses = WinHttpOpen(L"sticks3-state-host/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        HINTERNET conn = ses ? WinHttpConnect(ses, std::wstring(host.begin(), host.end()).c_str(),
                                              (INTERNET_PORT)port, 0)
                             : nullptr;
        HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", L"/v1/state/stream", nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                             : nullptr;
        bool subscribed = false;
        if (req) {
            WinHttpSetTimeouts(req, 5000, 5000, 20000, 20000);
            const std::wstring headers =
                L"Authorization: Bearer " + std::wstring(token.begin(), token.end());
            if (WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                subscribed = true;
                std::string acc;
                size_t consumed = 0;
                bool alive = true;
                while (alive && !deadline_hit(deadline)) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail)) break;
                    if (avail == 0) {
                        Sleep(50);
                        continue;
                    }
                    std::string chunk(avail, '\0');
                    DWORD rd = 0;
                    if (!WinHttpReadData(req, chunk.data(), avail, &rd)) break;
                    acc.append(chunk, 0, rd);
                    sse::Event ev;
                    while (sse::next_event(acc, consumed, ev)) {
                        if (ev.name != "snapshot" || ev.data.empty()) continue;
                        br03_snapshot in;
                        if (!bridge::parse_snapshot(ev.data, in)) continue;
                        if (in.revision == g_last_rev) continue;
                        if (push_state(ctrl, in)) {
                            g_last_rev = in.revision;
                        } else {
                            say("STATE write failed; reconnecting BLE");
                            ctrl = nullptr;
                            alive = false;
                            break;
                        }
                    }
                    if (consumed > 0) {
                        acc.erase(0, consumed);
                        consumed = 0;
                    }
                    if (acc.size() > (1u << 16)) acc.clear();
                }
            }
        }
        if (req) WinHttpCloseHandle(req);
        if (conn) WinHttpCloseHandle(conn);
        if (ses) WinHttpCloseHandle(ses);
        if (!subscribed) say("sse subscribe failed; retry");
        Sleep(1500);
    }
    std::printf("[state_host] exit after %u pushes\n", g_pushes);
    return 0;
}

int wmain() {
    winrt::init_apartment();
    try {
        return run_host();
    } catch (winrt::hresult_error const& e) {
        std::printf("[state_host] hresult_error 0x%08lX\n", (unsigned long)e.code());
        return 1;
    }
}
