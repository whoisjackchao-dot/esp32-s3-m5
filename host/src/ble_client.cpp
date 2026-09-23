// ble_client.cpp —— M02/M04 主机端 C++/WinRT 验证客户端（ StickS3 ↔ Windows）。
// 流程：按冻结服务 UUID 发现设备 → （已配对则复用 bond）→ 订阅 Audio Notify / Device Event
// → 写 READY 触发设备测试会话 → 通知喂入 s3p_reasm 状态机 → END 裁决 + 独立 FNV-1a
// 黄金校验（TEST-API-02：主机独立重算，不信任设备 summary）。
// 构建：vcvars64 && cl /std:c++20 /EHsc ble_client.cpp s3p_reasm.c /I..\..\firmware\sticks3\main /link windowsapp.lib
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

extern "C" {
#include "s3p_proto.h"
#include "s3p_reasm.h"
}

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Storage::Streams;

static const wchar_t* SVC_STR   = L"{22587f80-334a-53d6-beb8-8a6ace31af07}";
static const wchar_t* AUDIO_STR = L"{54d5e361-d549-55cc-ab98-66ebc8afe4f3}";
static const wchar_t* CTRL_STR  = L"{14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0}";
static const wchar_t* EVENT_STR = L"{a1d7042f-43a0-53b5-ad2f-a1b3ae57c79c}";

static std::mutex g_mu;
static s3p_reasm_t* g_reasm;
static HANDLE g_end_evt;
static struct {
    uint32_t rec;
    uint32_t tf;
    uint32_t ts;
    uint32_t summary;
    int end_rc;
} g_end;
static unsigned g_audio_pkts = 0;
static bool g_session_started = false;
static GattDeviceService g_svc{nullptr};

static uint32_t fnv_update(uint32_t h, const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        h ^= d[i];
        h *= 0x01000193u;
    }
    return h;
}

/* M1 桥接：GET /v1/binding（M1 契约 BR-02，loopback-only）。
 * 返回 true 时 valid=1；canAppend=false → busy。 */
static bool read_bridge_binding(bool* busy) {
    const char* url = std::getenv("STICKS3_BRIDGE_URL");
    const char* token = std::getenv("STICKS3_BRIDGE_TOKEN");
    if (!url || !token) {
        return false;
    }
    const std::string prefix = "http://";
    if (prefix.compare(0, prefix.size(), url, prefix.size()) != 0) {
        return false;
    }
    const std::string rest = url + prefix.size();
    const auto colon = rest.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    const std::string host = rest.substr(0, colon);
    const int port = std::atoi(rest.substr(colon + 1).c_str());
    if ((host != "127.0.0.1" && host != "localhost") || port <= 0 ||
        port > 65535) {
        return false;
    }

    const std::wstring whost(host.begin(), host.end());
    HINTERNET ses = WinHttpOpen(L"sticks3-ble/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    HINTERNET conn = WinHttpConnect(ses, whost.c_str(), (INTERNET_PORT)port, 0);
    if (!conn) { WinHttpCloseHandle(ses); return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", L"/v1/binding", nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(ses); return false; }
    const std::wstring headers = L"Authorization: Bearer " +
                                 std::wstring(token, token + std::char_traits<char>::length(token));
    bool ok = false;
    if (WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        std::string body;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail) {
            std::string chunk(avail, '\0');
            DWORD rd = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &rd)) break;
            body.append(chunk, 0, rd);
            if (body.size() > 8192) break;
        }
        const bool can_append = body.find("\"canAppend\":true") != std::string::npos;
        const bool has_field = body.find("\"canAppend\"") != std::string::npos;
        if (has_field) {
            *busy = !can_append;
            ok = true;
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(ses);
    return ok;
}

static int fail(const char* what, const char* detail = "") {
    std::printf("[FAIL] %s%s%s\n", what, *detail ? " | " : "", detail);
    return 2;
}

/* DeviceInformation.Id: \\?\BTHLEDevice#{svc}_<MAC>#... — 提取 12 位 hex 地址（MAC 不入库，已掩码） */
static uint64_t parse_addr_from_id(const winrt::hstring& id) {
    const std::wstring s(id.c_str(), id.size());
    const auto pos = s.find(L"_");
    if (pos == std::wstring::npos) {
        return 0;
    }
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
    std::lock_guard<std::mutex> lk(g_mu);
    if (n < 2 || p[1] != S3P_PROTOCOL_VERSION) {
        return;
    }
    switch (p[0]) {
    case S3P_PKT_BEGIN:
        if (n >= S3P_BEGIN_LEN) {
            const uint32_t rec = s3p_get_u32(p + 2);
            const uint16_t fc = s3p_get_u16(p + 6);
            const uint16_t sr = s3p_get_u16(p + 8);
            if (s3p_reasm_begin(g_reasm, rec, fc, sr) == S3P_REASM_OK) {
                g_session_started = true;
                std::printf("  <BEGIN recID=%lu frames=%u rate=%u>\n",
                            (unsigned long)rec, fc, sr);
            }
        }
        break;
    case S3P_PKT_AUDIO:
        if (n >= S3P_AUDIO_HDR_LEN) {
            s3p_reasm_feed(g_reasm, s3p_get_u32(p + 2), s3p_get_u32(p + 6),
                           p[10], p[11], p + S3P_AUDIO_HDR_LEN,
                           (uint16_t)(n - S3P_AUDIO_HDR_LEN));
            g_audio_pkts += 1;
        }
        break;
    case S3P_PKT_END:
        if (n >= S3P_END_LEN) {
            g_end.rec = s3p_get_u32(p + 2);
            g_end.tf = s3p_get_u32(p + 6);
            g_end.ts = s3p_get_u32(p + 10);
            g_end.summary = s3p_get_u32(p + 14);
            g_end.end_rc = (int)s3p_reasm_end(g_reasm, g_end.rec, g_end.tf,
                                              g_end.ts, g_end.summary);
            std::printf("  <END recID=%lu totalFrames=%lu totalSamples=%lu summary=0x%08lx rc=%d>\n",
                        (unsigned long)g_end.rec, (unsigned long)g_end.tf,
                        (unsigned long)g_end.ts, (unsigned long)g_end.summary,
                        g_end.end_rc);
            SetEvent(g_end_evt);
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
    if (n >= 2 && p[0] == S3P_PKT_ERROR) {
        std::printf("[FAIL] device ERROR event code=%u\n",
                    n >= 8 ? (unsigned)s3p_get_u16(p + 6) : 0u);
    } else if (n >= 2 && p[0] == S3P_PKT_DEVICE_CANCEL) {
        std::printf("[FAIL] device DEVICE_CANCEL event\n");
    }
}

static int run() {
    const winrt::guid svcGuid(SVC_STR);
    const winrt::guid audioGuid(AUDIO_STR);
    const winrt::guid ctrlGuid(CTRL_STR);
    const winrt::guid eventGuid(EVENT_STR);

    auto devs = DeviceInformation::FindAllAsync(
                    GattDeviceService::GetDeviceSelectorFromUuid(svcGuid))
                    .get();
    if (devs.Size() == 0) {
        return fail("scan", "StickS3 not advertising");
    }
    auto di = devs.GetAt(0);
    std::printf("[PASS] scan found | %ws\n", di.Id().c_str());

    if (!di.Pairing().IsPaired()) {
        auto pr = di.Pairing()
                      .PairAsync(DevicePairingProtectionLevel::None)
                      .get();
        if (pr.Status() != DevicePairingResultStatus::Paired &&
            pr.Status() != DevicePairingResultStatus::AlreadyPaired) {
            return fail("pair", "status != Paired");
        }
    }
    std::printf("[PASS] paired | Just Works bond active\n");

    const uint64_t addr = parse_addr_from_id(di.Id());
    if (addr == 0) {
        return fail("connect", "cannot parse BLE address from Id");
    }
    auto ble = BluetoothLEDevice::FromBluetoothAddressAsync(
                   addr, BluetoothAddressType::Public)
                   .get();
    if (!ble) {
        return fail("connect");
    }
    std::printf("[PASS] connect | addr=%012llx\n", (unsigned long long)addr);

    std::printf("  step: connect (GattSession)\n");
    auto session = GattSession::FromDeviceIdAsync(
                       BluetoothDeviceId::FromId(ble.DeviceId()))
                       .get();
    if (session) {
        /* bleak 同款流程：先建链再发现；无 GattSession 直接发现会报
         * 0x80070016 (ERROR_BAD_COMMAND)。 */
        session.MaintainConnection(true);
        for (int i = 0; i < 50 &&
                        ble.ConnectionStatus() != BluetoothConnectionStatus::Connected;
             ++i) {
            Sleep(100);
        }
        std::printf("  link: %ws\n",
                    ble.ConnectionStatus() == BluetoothConnectionStatus::Connected
                        ? L"connected"
                        : L"NOT connected");
    }

    std::printf("  step: service discover\n");
    auto try_discover = [&](BluetoothCacheMode mode) -> bool {
        try {
            auto res = ble.GetGattServicesForUuidAsync(svcGuid, mode).get();
            if (res.Status() == GattCommunicationStatus::Success &&
                res.Services().Size() > 0) {
                g_svc = res.Services().GetAt(0);
                return true;
            }
        } catch (winrt::hresult_error const& e) {
            std::printf("  discovery mode=%d failed 0x%08lX\n", (int)mode,
                        (unsigned long)e.code());
        }
        return false;
    };
    /* Uncached 在配对设备上常报 0x80070016；Cached 依赖重刷固件句柄未变 */
    if (!try_discover(BluetoothCacheMode::Uncached) &&
        !try_discover(BluetoothCacheMode::Cached)) {
        return fail("service discover");
    }
    auto svc = g_svc;
    std::printf("[PASS] service discover\n");

    auto audioRes = svc.GetCharacteristicsForUuidAsync(audioGuid, BluetoothCacheMode::Uncached).get();
    auto ctrlRes = svc.GetCharacteristicsForUuidAsync(ctrlGuid, BluetoothCacheMode::Uncached).get();
    auto eventRes = svc.GetCharacteristicsForUuidAsync(eventGuid, BluetoothCacheMode::Uncached).get();
    if (audioRes.Status() != GattCommunicationStatus::Success ||
        ctrlRes.Status() != GattCommunicationStatus::Success ||
        eventRes.Status() != GattCommunicationStatus::Success ||
        audioRes.Characteristics().Size() == 0 ||
        ctrlRes.Characteristics().Size() == 0 ||
        eventRes.Characteristics().Size() == 0) {
        return fail("characteristics");
    }
    auto audioChar = audioRes.Characteristics().GetAt(0);
    auto ctrlChar = ctrlRes.Characteristics().GetAt(0);
    auto eventChar = eventRes.Characteristics().GetAt(0);
    std::printf("[PASS] characteristics | audio/ctrl/event\n");

    static uint8_t reasm_buf[256 * 1024];
    g_reasm = s3p_reasm_create(reasm_buf, sizeof(reasm_buf));
    if (!g_reasm) {
        return fail("reasm create");
    }
    g_end_evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    audioChar.ValueChanged({on_audio});
    eventChar.ValueChanged({on_event});

    auto s1 = audioChar.WriteClientCharacteristicConfigurationDescriptorAsync(
        GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
    auto s2 = eventChar.WriteClientCharacteristicConfigurationDescriptorAsync(
        GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
    if (s1 != GattCommunicationStatus::Success || s2 != GattCommunicationStatus::Success) {
        return fail("subscribe", "CCCD write failed");
    }
    std::printf("[PASS] subscribed | CCCD written (encrypted link)\n");

    Buffer wb(4);
    wb.Length(4);
    uint8_t* wd = wb.data();
    wd[0] = 0x10; /* READY */
    wd[1] = S3P_PROTOCOL_VERSION;
    wd[2] = 0xF7; /* mtu 247 LE */
    wd[3] = 0x00;
    auto wres = ctrlChar.WriteValueAsync(wb, GattWriteOption::WriteWithResponse).get();
    if (wres != GattCommunicationStatus::Success) {
        return fail("write READY", "GATT status != Success");
    }
    std::printf("[PASS] write READY | 1001f700\n");

    /* M2 纵切：读 M1 桥接真实状态 → STATE(0x12) 下发；桥接未运行 → valid=0
     * （设备如实显示 UNKNOWN，不用假状态冒充集成） */
    {
        bool busy = false;
        const bool valid = read_bridge_binding(&busy);
        const uint8_t bitmap = (uint8_t)(valid ? (0x02 | (busy ? 0x01 : 0x00)) : 0x00);
        Buffer sb(4);
        sb.Length(4);
        uint8_t* sd = sb.data();
        sd[0] = 0x12;
        sd[1] = S3P_PROTOCOL_VERSION;
        sd[2] = 1; /* snapshot version */
        sd[3] = bitmap;
        const auto sres = ctrlChar.WriteValueAsync(sb, GattWriteOption::WriteWithResponse).get();
        if (sres != GattCommunicationStatus::Success) {
            return fail("write STATE", "GATT status != Success");
        }
        std::printf("[PASS] write STATE | bitmap=0x%02x (%s)\n", bitmap,
                    valid ? (busy ? "BUSY from bridge" : "IDLE from bridge") : "bridge absent -> UNKNOWN");
    }

    /* 满载会话（TX-02）：500 帧 × 60ms ≈ 31s，45s 超时留余量 */
    const DWORD w = WaitForSingleObject(g_end_evt, 45000);
    std::lock_guard<std::mutex> lk(g_mu);
    if (w != WAIT_OBJECT_0) {
        char d[48];
        std::snprintf(d, sizeof(d), "timeout (audio_pkts=%u)", g_audio_pkts);
        return fail("session END", d);
    }
    if (g_end.end_rc != (int)S3P_REASM_OK || !s3p_reasm_is_complete(g_reasm)) {
        char d[64];
        std::snprintf(d, sizeof(d), "rc=%d state=%d", g_end.end_rc,
                      (int)s3p_reasm_state(g_reasm));
        return fail("reasm adjudication", d);
    }
    std::printf("[PASS] reasm adjudication | rc=%d state=COMPLETE frames=%lu bytes=%zu\n",
                g_end.end_rc, (unsigned long)s3p_reasm_received_frames(g_reasm),
                s3p_reasm_total_bytes(g_reasm));

    /* 黄金校验：按 seq 独立重读帧并重算 FNV-1a，与 END.summary 比对 */
    uint32_t h = 0x811c9dc5u;
    static uint8_t fb[512];
    bool all_read = true;
    for (uint32_t s = 0; s < g_end.tf && all_read; ++s) {
        const size_t l = s3p_reasm_read_frame(g_reasm, s, fb, sizeof(fb));
        if (l == 0) {
            all_read = false;
            break;
        }
        h = fnv_update(h, fb, l);
    }
    if (!all_read || h != g_end.summary) {
        char d[64];
        std::snprintf(d, sizeof(d), "indep=%08lx dev=%08lx",
                      (unsigned long)h, (unsigned long)g_end.summary);
        return fail("independent FNV-1a golden check", d);
    }
    std::printf("[PASS] independent FNV-1a golden check | %08lx == %08lx\n",
                (unsigned long)h, (unsigned long)g_end.summary);
    std::printf("[PASS] END totalSamples | %lu\n", (unsigned long)g_end.ts);

    /* A4 工具链：完整会话导出（.s3pcap = S3PO magic + recID + frameCount +
     * [seq u32][len u16][payload]...），由 tools/decode_capture.py 解码为 WAV */
    const char* capdir = std::getenv("STICKS3_CAPTURE_DIR");
    if (capdir) {
        char path[300];
        std::snprintf(path, sizeof(path), "%s\\rec_%lu.s3pcap", capdir,
                      (unsigned long)g_end.rec);
        FILE* f = std::fopen(path, "wb");
        if (f) {
            const uint32_t magic = 0x4F503353; /* "S3PO" */
            std::fwrite(&magic, 4, 1, f);
            std::fwrite(&g_end.rec, 4, 1, f);
            std::fwrite(&g_end.tf, 4, 1, f);
            for (uint32_t s = 0; s < g_end.tf; ++s) {
                const size_t l = s3p_reasm_read_frame(g_reasm, s, fb, sizeof(fb));
                std::fwrite(&s, 4, 1, f);
                const uint16_t l16 = (uint16_t)l;
                std::fwrite(&l16, 2, 1, f);
                if (l) std::fwrite(fb, 1, l, f);
            }
            std::fclose(f);
            std::printf("[INFO] frames captured -> %s\n", path);
        }
    }
    return 0;
}

int wmain() {
    winrt::init_apartment();
    try {
        const int rc = run();
        std::printf("--- ble_client %s ---\n", rc == 0 ? "PASS" : "FAIL");
        return rc;
    } catch (winrt::hresult_error const& e) {
        std::printf("[FAIL] hresult_error 0x%08lX %ws\n",
                    (unsigned long)e.code(), e.message().c_str());
        return 1;
    }
}
