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
#include "retry_store.h"
#include "s3p_proto.h"
#include "s3p_reasm.h"
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
static const wchar_t* AUDIO_STR = L"{54d5e361-d549-55cc-ab98-66ebc8afe4f3}";
static const wchar_t* EVENT_STR = L"{a1d7042f-43a0-53b5-ad2f-a1b3ae57c79c}";

static uint32_t g_last_rev = 0;
static uint32_t g_pushes = 0;

static GattCharacteristic g_audio{nullptr};
static GattCharacteristic g_event{nullptr};
static s3p_reasm_t* g_reasm = nullptr;
static uint8_t g_reasm_buf[256 * 1024];
static uint8_t g_fb[512];
static HANDLE g_end_evt = nullptr;
static uint32_t g_last_rec = 0;
static uint32_t g_last_frames = 0;
static uint32_t g_last_samples = 0;
static uint32_t g_last_summary = 0;
static int g_last_end_rc = -1;
static bool g_session_captured = false;
static retry_store g_retry;
static uint8_t g_held[256 * 1024 + 64];
static size_t g_held_len = 0;

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

static void on_audio(GattCharacteristic const&, GattValueChangedEventArgs const& args) {
    auto buf = args.CharacteristicValue();
    const uint8_t* p = buf.data();
    const uint32_t n = buf.Length();
    if (!g_reasm || n < 2 || p[1] != S3P_PROTOCOL_VERSION) return;
    switch (p[0]) {
    case S3P_PKT_BEGIN:
        if (n >= S3P_BEGIN_LEN) {
            const uint32_t rec = s3p_get_u32(p + 2);
            const uint16_t fc = s3p_get_u16(p + 6);
            const uint16_t sr = s3p_get_u16(p + 8);
            if (s3p_reasm_begin(g_reasm, rec, fc, sr) == S3P_REASM_OK) {
                g_last_rec = rec;
                std::printf("[state_host] session BEGIN rec=%lu frames=%u\n", (unsigned long)rec, (unsigned)fc);
                std::fflush(stdout);
            }
        }
        break;
    case S3P_PKT_AUDIO:
        if (n >= S3P_AUDIO_HDR_LEN) {
            s3p_reasm_feed(g_reasm, s3p_get_u32(p + 2), s3p_get_u32(p + 6), p[10], p[11],
                           p + S3P_AUDIO_HDR_LEN, (uint16_t)(n - S3P_AUDIO_HDR_LEN));
        }
        break;
    case S3P_PKT_END:
        if (n >= S3P_END_LEN) {
            g_last_frames = s3p_get_u32(p + 6);
            g_last_samples = s3p_get_u32(p + 10);
            g_last_summary = s3p_get_u32(p + 14);
            g_last_end_rc = (int)s3p_reasm_end(g_reasm, s3p_get_u32(p + 2), g_last_frames,
                                               g_last_samples, g_last_summary);
            g_session_captured = true;
            if (g_last_end_rc == (int)S3P_REASM_OK) {
                g_held_len = 0;
                if (sizeof(g_held) >= 12) {
                    std::memcpy(g_held, "S3PO", 4);
                    std::memcpy(g_held + 4, &g_last_rec, 4);
                    std::memcpy(g_held + 8, &g_last_frames, 4);
                    size_t off = 12;
                    for (uint32_t s = 0; s < g_last_frames && off + 6 <= sizeof(g_held); ++s) {
                        const size_t l = s3p_reasm_read_frame(g_reasm, s, g_fb, sizeof(g_fb));
                        const uint16_t l16 = (uint16_t)l;
                        if (off + 6 + l > sizeof(g_held)) break;
                        std::memcpy(g_held + off, &s, 4);
                        off += 4;
                        std::memcpy(g_held + off, &l16, 2);
                        off += 2;
                        if (l) {
                            std::memcpy(g_held + off, g_fb, l);
                            off += l;
                        }
                    }
                    g_held_len = off;
                }
                retry_store_note(&g_retry, RETRY_FAILED, true, false, "", false, GetTickCount());
                std::printf("[state_host] retry: audio held (%zu B) can_recognize=%d\n", g_held_len,
                            (int)retry_can_recognize(&g_retry, GetTickCount()));
                std::fflush(stdout);
            }
            const char* capdir = std::getenv("STICKS3_CAPTURE_DIR");
            std::printf("[state_host] session END frames=%lu samples=%lu rc=%d\n",
                        (unsigned long)g_last_frames, (unsigned long)g_last_samples, g_last_end_rc);
            std::fflush(stdout);
            if (capdir) {
                char path[300];
                std::snprintf(path, sizeof(path), "%s\\rec_%lu.s3pcap", capdir, (unsigned long)g_last_rec);
                FILE* f = std::fopen(path, "wb");
                if (f) {
                    const uint32_t magic = 0x4F503353; /* "S3PO" */
                    std::fwrite(&magic, 4, 1, f);
                    std::fwrite(&g_last_rec, 4, 1, f);
                    std::fwrite(&g_last_frames, 4, 1, f);
                    for (uint32_t s = 0; s < g_last_frames; ++s) {
                        const size_t l = s3p_reasm_read_frame(g_reasm, s, g_fb, sizeof(g_fb));
                        const uint16_t l16 = (uint16_t)l;
                        std::fwrite(&s, 4, 1, f);
                        std::fwrite(&l16, 2, 1, f);
                        if (l) std::fwrite(g_fb, 1, l, f);
                    }
                    std::fclose(f);
                    std::printf("[state_host] captured -> %s\n", path);
                    std::fflush(stdout);
                }
            }
            if (g_end_evt) SetEvent(g_end_evt);
        }
        break;
    default:
        break;
    }
}

static void on_event(GattCharacteristic const&, GattValueChangedEventArgs const& args) {
    auto buf = args.CharacteristicValue();
    const uint8_t* p = buf.data();
    const uint32_t n = buf.Length();
    if (n >= 2 && (p[0] == S3P_PKT_ERROR || p[0] == S3P_PKT_DEVICE_CANCEL)) {
        std::printf("[state_host] device event type=0x%02x\n", (unsigned)p[0]);
        std::fflush(stdout);
    }
}

static bool connect_chars(GattCharacteristic& out_ctrl, GattCharacteristic& out_audio,
                          GattCharacteristic& out_event) {
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
    auto get_char = [&](const wchar_t* guidStr, GattCharacteristic& out) -> bool {
        const winrt::guid guid(guidStr);
        auto r = svc.GetCharacteristicsForUuidAsync(guid, BluetoothCacheMode::Uncached).get();
        if (r.Status() != GattCommunicationStatus::Success || r.Characteristics().Size() == 0) {
            r = svc.GetCharacteristicsForUuidAsync(guid, BluetoothCacheMode::Cached).get();
        }
        if (r.Status() != GattCommunicationStatus::Success || r.Characteristics().Size() == 0) return false;
        out = r.Characteristics().GetAt(0);
        return true;
    };
    return get_char(CTRL_STR, out_ctrl) && get_char(AUDIO_STR, out_audio) &&
           get_char(EVENT_STR, out_event);
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
    retry_store_init(&g_retry);
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
                if (!connect_chars(ctrl, g_audio, g_event)) {
                    say("ble connect failed; retry");
                    Sleep(2000);
                    continue;
                }
                if (!g_reasm) g_reasm = s3p_reasm_create(g_reasm_buf, sizeof(g_reasm_buf));
                if (!g_end_evt) g_end_evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                g_audio.ValueChanged({on_audio});
                g_event.ValueChanged({on_event});
                auto s1 = g_audio.WriteClientCharacteristicConfigurationDescriptorAsync(
                              GattClientCharacteristicConfigurationDescriptorValue::Notify)
                              .get();
                auto s2 = g_event.WriteClientCharacteristicConfigurationDescriptorAsync(
                              GattClientCharacteristicConfigurationDescriptorValue::Notify)
                              .get();
                if (s1 != GattCommunicationStatus::Success || s2 != GattCommunicationStatus::Success) {
                    say("ble subscribe failed; retry");
                    ctrl = nullptr;
                    Sleep(2000);
                    continue;
                }
            } catch (...) {
                say("ble connect exception; retry");
                Sleep(2000);
                continue;
            }
            say("ble connected + subscribed (CTRL/AUDIO/EVENT)");
            if (std::getenv("STICKS3_HOST_TRIGGER")) {
                const uint8_t rd[4] = {0x10, 1, 0xF7, 0x00};
                Buffer wb(4);
                wb.Length(4);
                std::memcpy(wb.data(), rd, 4);
                (void)ctrl.WriteValueAsync(wb, GattWriteOption::WriteWithResponse).get();
                say("triggered READY (test mode)");
            }
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
