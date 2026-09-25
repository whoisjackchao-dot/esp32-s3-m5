// state_host.cpp —— M4 S4.2 生产 host（常驻）。
// 流程：读 bridge.json → 连接 StickS3 BLE（CTRL 特征）→ 订阅桥接 SSE /v1/state/stream
//       → 每收到 BR-03 真实快照，按 snapshotRevision 去重 → state_merge → s3p_state_encode
//       → BLE 写 STATE(0x12)。
// 不写 READY、不启动录音会话；桥接/BLE 断开自动重连。
// 测试用：环境变量 STICKS3_HOST_SECONDS=N 令其在 N 秒后自行退出。
#include <winsock2.h>
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
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

extern "C" {
#include "append_ctl.h"
#include "asr_adapter.h"
#include "asr_credential.h"
#include "asr_transport.h"
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
static std::mutex g_voice_mu;
static std::string g_bridge_url;
static std::string g_bridge_token;
static std::string g_epoch_name;
static std::string g_session_id;
static std::string g_last_text;
static std::string g_host_token;
static int g_host_port = 45910;
static volatile LONG g_http_stop = 0;

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

static void host_voice_worker(std::string cap);

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
                if (std::getenv("STICKS3_HOST_VOICE") != nullptr && g_held_len > 12) {
                    std::string cap((const char*)g_held, g_held_len);
                    std::thread(host_voice_worker, cap).detach();
                }
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

static std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return ".";
    const std::wstring w(buf, n);
    const auto pos = w.find_last_of(L"\\/");
    return winrt::to_string(pos == std::wstring::npos ? w : w.substr(0, pos));
}

static bool run_decode(const std::string& in, const std::string& out) {
    const std::string script = exe_dir() + "\\..\\..\\tools\\decode_capture.py";
    std::string cmd = "cmd.exe /c python \"" + script + "\" \"" + in + "\" \"" + out + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

static std::string rand_op_id() {
    static const char* hex = "0123456789abcdef";
    std::string id = "op_";
    for (int i = 0; i < 29; ++i) {
        id += hex[std::rand() & 0xF];
    }
    return id;
}

static bool append_post(const std::string& url, const std::string& token, const std::string& body,
                        unsigned long& status_out, std::string& resp_out) {
    std::string host;
    int port = 0;
    if (!parse_http_url(url, host, port)) return false;
    HINTERNET ses = WinHttpOpen(L"sticks3-state-host/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr,
                                nullptr, 0);
    HINTERNET conn = ses ? WinHttpConnect(ses, std::wstring(host.begin(), host.end()).c_str(),
                                          (INTERNET_PORT)port, 0)
                         : nullptr;
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"POST", L"/v1/draft/append", nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                         : nullptr;
    bool ok = false;
    if (req) {
        DWORD t = 10000;
        WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &t, sizeof(t));
        const std::wstring hdr = L"Content-Type: application/json\r\nAuthorization: Bearer " +
                                 std::wstring(token.begin(), token.end()) + L"\r\n";
        if (WinHttpSendRequest(req, hdr.c_str(), (DWORD)hdr.size(), (LPVOID)body.data(),
                               (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD sc = 0, sz = sizeof(sc);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX);
            status_out = sc;
            for (;;) {
                DWORD av = 0;
                if (!WinHttpQueryDataAvailable(req, &av) || av == 0) break;
                std::string c(av, '\0');
                DWORD rd = 0;
                if (!WinHttpReadData(req, c.data(), av, &rd)) break;
                resp_out.append(c, 0, rd);
            }
            ok = true;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

static void host_voice_worker(std::string cap) {
    char tmp[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmp) == 0) return;
    const std::string s3p = std::string(tmp) + "sticks3_host_rec.s3pcap";
    const std::string wav = std::string(tmp) + "sticks3_host_rec.wav";
    {
        FILE* f = std::fopen(s3p.c_str(), "wb");
        if (!f) {
            say("voice: temp write failed");
            return;
        }
        std::fwrite(cap.data(), 1, cap.size(), f);
        std::fclose(f);
    }
    if (!run_decode(s3p, wav)) {
        say("voice: decode failed");
        std::remove(s3p.c_str());
        return;
    }
    std::printf("[state_host] voice: decoded -> wav\n");
    std::fflush(stdout);
    std::string keybuf;
    const char* envkey = std::getenv("STICKS3_ASR_KEY");
    if (envkey != nullptr && *envkey != '\0') {
        keybuf = envkey;
    } else {
        char cred[512];
        if (asr_cred_load(cred, sizeof(cred)) == ASR_CRED_OK) {
            keybuf = cred;
            SecureZeroMemory(cred, sizeof(cred));
        }
    }
    if (keybuf.empty()) {
        say("voice: ASR skipped (no key: env/cred)");
        std::remove(s3p.c_str());
        std::remove(wav.c_str());
        return;
    }
    const char* key = keybuf.c_str();
    std::string wavb;
    if (!read_file(wav, wavb) || wavb.empty()) {
        say("voice: wav read failed");
        std::remove(s3p.c_str());
        return;
    }
    std::string body(wavb.size() + 4096, '\0');
    const size_t bl = asr_multipart_build((const uint8_t*)wavb.data(), wavb.size(),
                                          (uint8_t*)body.data(), body.size());
    if (bl == 0) {
        say("voice: multipart failed");
        std::remove(s3p.c_str());
        std::remove(wav.c_str());
        return;
    }
    body.resize(bl);
    asr_http_req_t req;
    memset(&req, 0, sizeof(req));
    req.host = "open.bigmodel.cn";
    req.port = 443;
    req.secure = 1;
    req.path = "/api/paas/v4/audio/transcriptions";
    req.method = "POST";
    req.api_key = key;
    req.content_type = asr_content_type();
    req.timeout_ms = 20000;
    std::string resp(65536, '\0');
    int hs = 0;
    const asr_status_t st = asr_http_post(&req, (const uint8_t*)body.data(), bl, resp.data(),
                                          resp.size(), &hs);
    SecureZeroMemory(body.data(), body.size());
    if (st != ASR_OK) {
        std::printf("[state_host] voice: ASR transport failed (%d)\n", (int)st);
        std::fflush(stdout);
        std::remove(s3p.c_str());
        std::remove(wav.c_str());
        return;
    }
    char text[4096];
    const asr_status_t cls = asr_classify(hs, resp.c_str(), strlen(resp.c_str()), text, sizeof(text));
    if (cls != ASR_OK) {
        std::printf("[state_host] voice: ASR class %d (no text)\n", (int)cls);
        std::fflush(stdout);
        std::remove(s3p.c_str());
        std::remove(wav.c_str());
        return;
    }
    std::string sess;
    {
        std::lock_guard<std::mutex> lk(g_voice_mu);
        sess = g_session_id;
        g_last_text = text;
        retry_store_note(&g_retry, RETRY_FAILED, true, true, sess.c_str(), !sess.empty(),
                         GetTickCount());
    }
    const std::string opid = rand_op_id();
    char jb[16384];
    const size_t jl = append_build_request(g_epoch_name.c_str(), sess.c_str(), opid.c_str(), text, jb,
                                           sizeof(jb));
    unsigned long sc = 0;
    std::string rb;
    const bool posted = jl != 0 && append_post(g_bridge_url, g_bridge_token, std::string(jb, jl), sc, rb);
    if (posted) {
        const append_outcome oc = append_classify((int)sc, rb.data(), rb.size());
        std::lock_guard<std::mutex> lk(g_voice_mu);
        const retry_status rs = oc == APPEND_APPLIED     ? RETRY_APPLIED
                                : oc == APPEND_UNCERTAIN ? RETRY_UNCERTAIN
                                                         : RETRY_REJECTED;
        retry_store_note(&g_retry, rs, true, true, sess.c_str(), !sess.empty(), GetTickCount());
        std::printf("[state_host] voice: append outcome=%d http=%lu\n", (int)oc, sc);
        if (oc != APPEND_APPLIED) {
            std::printf("[state_host] voice: append resp=%s\n", rb.substr(0, 200).c_str());
        }
    } else {
        say("voice: append post failed");
    }
    std::fflush(stdout);
    SecureZeroMemory(&keybuf[0], keybuf.size());
    std::remove(s3p.c_str());
    std::remove(wav.c_str());
}

static void http_reply(SOCKET c, int code, const char* body) {
    const char* reason = code == 200 ? "OK"
                         : code == 202 ? "Accepted"
                         : code == 401 ? "Unauthorized"
                         : code == 409 ? "Conflict"
                         : code == 502 ? "Bad Gateway"
                                       : "Not Found";
    char hdr[256];
    const int n = std::snprintf(hdr, sizeof(hdr),
                                "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                                "Content-Length: %d\r\nConnection: close\r\n\r\n",
                                code, reason, (int)std::strlen(body));
    (void)send(c, hdr, n, 0);
    (void)send(c, body, (int)std::strlen(body), 0);
}

static std::string retry_status_json() {
    std::lock_guard<std::mutex> lk(g_voice_mu);
    const uint32_t now = GetTickCount();
    char b[256];
    std::snprintf(b, sizeof(b),
                  "{\"canRecognize\":%s,\"canAppend\":%s,\"hasAudio\":%s,\"hasText\":%s,"
                  "\"status\":%d}",
                  retry_can_recognize(&g_retry, now) ? "true" : "false",
                  retry_can_append(&g_retry, now) ? "true" : "false",
                  g_held_len > 12 ? "true" : "false", g_last_text.empty() ? "false" : "true",
                  (int)g_retry.rec.status);
    return b;
}

static bool bearer_ok(const char* req) {
    const char* p = std::strstr(req, "Authorization: Bearer ");
    if (p == nullptr) return false;
    p += std::strlen("Authorization: Bearer ");
    const char* e = std::strstr(p, "\r\n");
    const size_t n = e ? (size_t)(e - p) : std::strlen(p);
    return g_host_token.size() == n && std::memcmp(p, g_host_token.data(), n) == 0;
}

static void http_handle_conn(SOCKET c) {
    char buf[4096];
    const int n = recv(c, buf, (int)sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';
    char method[8] = {0}, path[128] = {0};
    if (sscanf(buf, "%7s %127s", method, path) != 2) return;
    if (!bearer_ok(buf)) {
        http_reply(c, 401, "{\"error\":\"unauthorized\"}");
        return;
    }
    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/v1/retry/status") == 0) {
        const std::string b = retry_status_json();
        http_reply(c, 200, b.c_str());
        return;
    }
    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/v1/retry/recognize") == 0) {
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_voice_mu);
            ok = retry_can_recognize(&g_retry, GetTickCount()) && g_held_len > 12;
        }
        if (!ok) {
            http_reply(c, 409, "{\"error\":\"not_eligible\"}");
            return;
        }
        std::string cap;
        {
            std::lock_guard<std::mutex> lk(g_voice_mu);
            cap.assign((const char*)g_held, g_held_len);
        }
        std::thread(host_voice_worker, cap).detach();
        http_reply(c, 202, "{\"status\":\"accepted\"}");
        return;
    }
    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/v1/retry/append") == 0) {
        std::string text, sess, url, token, epoch;
        {
            std::lock_guard<std::mutex> lk(g_voice_mu);
            if (!retry_can_append(&g_retry, GetTickCount()) || g_last_text.empty()) {
                http_reply(c, 409, "{\"error\":\"not_eligible\"}");
                return;
            }
            text = g_last_text;
            sess = g_session_id;
            url = g_bridge_url;
            token = g_bridge_token;
            epoch = g_epoch_name;
        }
        const std::string opid = rand_op_id();
        char jb[16384];
        const size_t jl = append_build_request(epoch.c_str(), sess.c_str(), opid.c_str(),
                                               text.c_str(), jb, sizeof(jb));
        unsigned long sc = 0;
        std::string rb;
        if (jl == 0 || !append_post(url, token, std::string(jb, jl), sc, rb)) {
            http_reply(c, 502, "{\"error\":\"append_failed\"}");
            return;
        }
        const append_outcome oc = append_classify((int)sc, rb.data(), rb.size());
        {
            std::lock_guard<std::mutex> lk(g_voice_mu);
            const retry_status rs = oc == APPEND_APPLIED     ? RETRY_APPLIED
                                    : oc == APPEND_UNCERTAIN ? RETRY_UNCERTAIN
                                                             : RETRY_REJECTED;
            retry_store_note(&g_retry, rs, true, true, sess.c_str(), !sess.empty(), GetTickCount());
        }
        char out[128];
        std::snprintf(out, sizeof(out), "{\"outcome\":%d,\"http\":%lu}", (int)oc, sc);
        http_reply(c, 200, out);
        return;
    }
    http_reply(c, 404, "{\"error\":\"not_found\"}");
}

static void http_thread() {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)g_host_port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        WSACleanup();
        say("control endpoint bind failed");
        return;
    }
    std::printf("[state_host] control endpoint on 127.0.0.1:%d\n", g_host_port);
    std::fflush(stdout);
    while (InterlockedCompareExchange(&g_http_stop, 0, 0) == 0) {
        SOCKET c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        http_handle_conn(c);
        closesocket(c);
    }
    closesocket(s);
    WSACleanup();
}

static void write_host_descriptor() {
    const char* lad = std::getenv("LOCALAPPDATA");
    if (lad == nullptr) return;
    const std::string dir = std::string(lad) + "\\sticks3";
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string path = dir + "\\host.json";
    char js[256];
    std::snprintf(js, sizeof(js), "{\"url\":\"http://127.0.0.1:%d\",\"token\":\"%s\"}",
                  g_host_port, g_host_token.c_str());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f != nullptr) {
        std::fwrite(js, 1, std::strlen(js), f);
        std::fclose(f);
        std::printf("[state_host] descriptor -> %s\n", path.c_str());
        std::fflush(stdout);
    }
}

static int run_host() {
    retry_store_init(&g_retry);
    {
        const char* tk = std::getenv("STICKS3_HOST_TOKEN");
        g_host_token = (tk && *tk) ? tk : "sticks3-host";
        const char* hp = std::getenv("STICKS3_HOST_PORT");
        if (hp && std::atoi(hp) > 0) g_host_port = std::atoi(hp);
        std::thread(http_thread).detach();
        write_host_descriptor();
    }
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
        {
            std::string ep;
            std::lock_guard<std::mutex> lk(g_voice_mu);
            g_bridge_url = url;
            g_bridge_token = token;
            if (tray::json_string(js, "tuiEpoch", ep)) g_epoch_name = ep;
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
                        {
                            const std::string sk = "\"sessionID\":\"";
                            const size_t k = ev.data.find(sk);
                            const size_t e = k == std::string::npos
                                                 ? std::string::npos
                                                 : ev.data.find('"', k + sk.size());
                            if (e != std::string::npos) {
                                std::lock_guard<std::mutex> lk(g_voice_mu);
                                g_session_id = ev.data.substr(k + sk.size(), e - (k + sk.size()));
                            }
                        }
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
