# -*- coding: utf-8 -*-
"""M2 GATT 空口验证探针（bleak，Windows 主机侧）。

按 contracts/M2-BLE线协议冻结.md：
- 发现服务 22587f80-334a-53d6-beb8-8a6ace31af07 与三特征
- Just Works 配对（显式 pair()，WinRT 不因 error 5 自动配对）
- 订阅 Audio Notify / Device Event
- 写 READY{0x10, ver=1, mtu=247} 触发设备测试录音会话（BEGIN/AUDIO.../END）
- 独立重组验证：帧完整性 + FNV-1a(END.summary) 独立计算比对（TEST-API-02）
退出码 0=全部通过。
"""
import asyncio
import sys

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
CHR_EVENT = "a1d7042f-43a0-53b5-ad2f-a1b3ae57c79c"

events = []
sess = {
    "begin": None,        # (recID, frameCount, sampleRate, frameMs)
    "frames": {},         # frameSeq -> {fragIndex: payload bytes}
    "frag_counts": {},    # frameSeq -> fragCount
    "hash": 0x811C9DC5,   # FNV-1a 连续状态（全部 Opus 载荷字节）
    "end": None,          # (recID, totalFrames, totalSamples, summary)
    "audio_pkts": 0,
}


def note(ok, label, detail=""):
    line = "[{}] {}{}".format("PASS" if ok else "FAIL", label, (" | " + detail) if detail else "")
    events.append(line)
    print(line, flush=True)
    return ok


def fnv_update(h, data):
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def le16(d, off):
    return d[off] | (d[off + 1] << 8)


def le32(d, off):
    return d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (d[off + 3] << 24)


def on_audio(_h, data):
    if len(data) < 2 or data[1] != 1:
        note(False, "audio packet version", data[:2].hex())
        return
    t = data[0]
    if t == 0x01 and len(data) >= 12:
        sess["begin"] = (le32(data, 2), le16(data, 6), le16(data, 8), data[10])
        print("  <BEGIN recID={} frames={} rate={} frameMs={}>".format(*sess["begin"]), flush=True)
    elif t == 0x02 and len(data) >= 14:
        flen = le16(data, 12)
        if len(data) != 14 + flen:
            note(False, "AUDIO len mismatch", "declared={} actual={}".format(flen, len(data) - 14))
            return
        seq = le32(data, 6)
        fi, fc = data[10], data[11]
        payload = data[14:]
        sess["frag_counts"][seq] = fc
        sess["frames"].setdefault(seq, {})[fi] = payload
        sess["hash"] = fnv_update(sess["hash"], payload)
        sess["audio_pkts"] += 1
    elif t == 0x03 and len(data) >= 18:
        sess["end"] = (le32(data, 2), le32(data, 6), le32(data, 10), le32(data, 14))
        print("  <END recID={} totalFrames={} totalSamples={} summary={:#010x}>".format(*sess["end"]), flush=True)
        end_evt.set()
    else:
        note(False, "unknown packet type", "0x{:02x}".format(t))


def on_event(_h, data):
    if len(data) >= 2 and data[0] == 0x05:
        note(False, "device ERROR event", "code={}".format(le16(data, 6) if len(data) >= 8 else "?"))
    elif len(data) >= 2 and data[0] == 0x04:
        note(False, "device DEVICE_CANCEL event", "recID={}".format(le32(data, 2) if len(data) >= 6 else "?"))
    else:
        note(True, "event notify", data.hex())


def verify_session():
    ok = True
    if sess["begin"] is None:
        return note(False, "session", "BEGIN not received")
    rec_id, fc, sr, fms = sess["begin"]
    ok &= note(sr == 16000, "BEGIN sampleRate", str(sr))
    ok &= note(fms == 60, "BEGIN frameMs", str(fms))
    ok &= note(fc == 500, "BEGIN frameCount", str(fc))
    if sess["end"] is None:
        return note(False, "session", "END not received")
    rec_id_e, tf, ts, summary = sess["end"]
    ok &= note(rec_id_e == rec_id, "recordingID consistent", "begin={} end={}".format(rec_id, rec_id_e))
    ok &= note(tf == fc and tf == len(sess["frames"]), "END totalFrames",
               "declared={} received={}".format(tf, len(sess["frames"])))
    ok &= note(all(i in sess["frames"] for i in range(tf)), "frames complete 0..{}".format(tf - 1), "")
    frag_ok = all(
        sess["frag_counts"].get(seq) == len(frags)
        for seq, frags in sess["frames"].items()
    )
    ok &= note(frag_ok, "fragments complete per frame", "")
    ok &= note(ts == 480000, "END totalSamples", str(ts))
    ok &= note(sess["hash"] == summary, "END summary FNV-1a (independent golden check)",
               "indep={:#010x} dev={:#010x}".format(sess["hash"], summary))
    return ok


async def main():
    global end_evt
    end_evt = asyncio.Event()

    print("scanning for StickS3 ...", flush=True)
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for addr, (dev, adv) in devices.items():
        name = dev.name or (adv.local_name or "")
        if name == "StickS3" or SVC in [str(u) for u in adv.service_uuids]:
            target = dev
            note(True, "scan found", "{} rssi={} uuids={}".format(addr, adv.rssi,
                 [str(u) for u in adv.service_uuids]))
            break
    if target is None:
        note(False, "scan", "StickS3 not in advertising")
        sys.exit(1)

    async with BleakClient(target, timeout=15.0) as client:
        note(True, "connected", client.address)

        # Just Works SC pairing: WinRT 不会因 error 5 自动配对，显式发起
        try:
            await client.pair()
            note(True, "pair", "Windows pairing OK")
        except Exception as e:
            note(False, "pair", repr(e)[:120])

        svc = client.services.get_service(SVC)
        note(svc is not None, "service discover", SVC)
        audio = client.services.get_characteristic(CHR_AUDIO)
        ctrl = client.services.get_characteristic(CHR_CTRL)
        event = client.services.get_characteristic(CHR_EVENT)
        note(audio is not None, "char Audio Notify", CHR_AUDIO if audio else "missing")
        note(ctrl is not None, "char Control Write", CHR_CTRL if ctrl else "missing")
        note(event is not None, "char Device Event", CHR_EVENT if event else "missing")

        got_event_asyncio = asyncio.Event()  # placeholder naming guard

        def on_evt(h, data):
            on_event(h, data)

        await client.start_notify(audio, on_audio)
        await client.start_notify(event, on_evt)
        note(True, "subscribed", "audio+event CCCD written (encrypted link active)")

        # MTU 交换由 Windows 懒触发：轮询等待，契约硬底线 65（冻结 §2）
        mtu = 23
        for _ in range(15):
            mtu = client.mtu_size
            if mtu >= 65:
                break
            await asyncio.sleep(0.2)
        note(mtu >= 65, "MTU", "negotiated={} (preferred 247)".format(mtu))

        ready = bytes([0x10, 0x01, 0xF7 & 0xFF, (247 >> 8) & 0xFF])  # ver=1, mtu=247 LE
        hb1 = bytes([0x11, 0x01])
        hb2 = bytes([0x11, 0x02])
        # Write With Response: WNR 会在未加密链路被静默丢弃；response=True 让
        # 服务端安全错误浮出水面，Windows 据此自动配对后重试成功。
        ok = True
        for label, payload in (("READY", ready), ("HEARTBEAT#1", hb1), ("HEARTBEAT#2", hb2)):
            done = False
            for attempt in range(4):
                try:
                    await client.write_gatt_char(ctrl, payload, response=True)
                    note(True, "write {}".format(label), payload.hex() + (" attempt={}".format(attempt + 1) if attempt else ""))
                    done = True
                    break
                except Exception as e:
                    note(False, "write {} attempt {}".format(label, attempt + 1), repr(e)[:120])
                    await asyncio.sleep(1.5)
            ok = ok and done
            await asyncio.sleep(0.3)
        if not ok:
            note(False, "control writes", "some messages not delivered")
            sys.exit(2)

        # 等待设备会话完成（BEGIN → 500 AUDIO → END ≈ 30s；M2 v1.1 预告上限 500）
        try:
            await asyncio.wait_for(end_evt.wait(), timeout=40.0)
        except asyncio.TimeoutError:
            note(False, "session END", "timeout (audio_pkts={})".format(sess["audio_pkts"]))
        verify_session()

        await client.stop_notify(audio)
        await client.stop_notify(event)

    print("--- SUMMARY ---", flush=True)
    fails = [e for e in events if e.startswith("[FAIL]")]
    print("total={} fails={} audio_pkts={}".format(len(events), len(fails), sess["audio_pkts"]), flush=True)
    sys.exit(0 if not fails else 2)


asyncio.run(main())
