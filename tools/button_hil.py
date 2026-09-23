#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""M01 按键 HIL：连接+订阅后**不主动发 READY**，等待物理按键触发的会话。

验证契约 §3.2 v1.1（2026-09-23 裁决）：
  - 按下 BtnA → BEGIN（frameCount = 预告上限 500）
  - 松开 → END，totalFrames = 实际帧数（≤ 预告）；收齐 [0, actual) 且摘要一致 = 合法提前结束
  - 按住满 30s → 30s 硬停（END.totalFrames == 500）

用法：
  python button_hil.py [BEGIN等待秒] [END等待秒] [可选输出 .s3pcap]
  默认 60s / 45s。启动后看到 "等待按下 BtnA" 即可按住按键讲话，说完松开。
退出码：0 = 合法会话；2 = 失败/超时。
"""
import asyncio
import struct
import sys
import time

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
FNV_SEED = 0x811C9DC5


def le16(d, o):
    return d[o] | (d[o + 1] << 8)


def le32(d, o):
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)


def fnv1a(data):
    h = FNV_SEED
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


async def main():
    begin_wait = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    end_wait = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0
    out_path = sys.argv[3] if len(sys.argv) > 3 else None

    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for d, (dv, av) in devices.items():
        if (dv.name or av.local_name or "") == "StickS3" or SVC in [str(u) for u in av.service_uuids]:
            target = d
            break
    if target is None:
        print("[FAIL] scan: StickS3 not found")
        sys.exit(2)

    sess = {"begin": None, "end": None, "frames": {}, "begin_evt": asyncio.Event(),
            "end_evt": asyncio.Event()}

    def on_audio(_h, data):
        if len(data) < 2 or data[1] != 1:
            return
        t = data[0]
        if t == 0x01:
            sess["begin"] = {"recID": le32(data, 2), "frameCount": le16(data, 6),
                             "sampleRate": le16(data, 8), "frameMs": data[10]}
            sess["begin_evt"].set()
        elif t == 0x02 and len(data) >= 14:
            seq = le32(data, 6)
            fidx, fcnt = data[10], data[11]
            flen = le16(data, 12)
            fr = sess["frames"].setdefault(seq, {"count": fcnt, "frags": {}})
            fr["frags"][fidx] = bytes(data[14:14 + flen])
            fr["count"] = fcnt
        elif t == 0x03 and len(data) >= 18:
            sess["end"] = {"recID": le32(data, 2), "totalFrames": le32(data, 6),
                           "totalSamples": le32(data, 10), "summary": le32(data, 14)}
            sess["end_evt"].set()

    async with BleakClient(target, timeout=15.0) as client:
        try:
            await client.pair()
        except Exception:
            pass
        audio = client.services.get_characteristic(CHR_AUDIO)
        await client.start_notify(audio, on_audio)
        print("[OK] 已连接并订阅（未发 READY）。请按住 BtnA 讲话，说完松开 …", flush=True)

        t_press = time.monotonic()
        try:
            await asyncio.wait_for(sess["begin_evt"].wait(), timeout=begin_wait)
        except asyncio.TimeoutError:
            print("[FAIL] {}s 内未收到 BEGIN（按键未触发或链路未就绪）".format(begin_wait))
            sys.exit(2)
        press_lat = time.monotonic() - t_press
        print("[OK] BEGIN recID={} frameCount={}（按下后 {:.1f}s 收到）".format(
            sess["begin"]["recID"], sess["begin"]["frameCount"], press_lat), flush=True)

        try:
            await asyncio.wait_for(sess["end_evt"].wait(), timeout=end_wait)
        except asyncio.TimeoutError:
            print("[FAIL] {}s 内未收到 END".format(end_wait))
            sys.exit(2)

    begin, end, frames = sess["begin"], sess["end"], sess["frames"]
    declared = begin["frameCount"]
    actual = end["totalFrames"]
    missing = [s for s in range(actual) if s not in frames]
    incomplete = [s for s, f in frames.items() if sorted(f["frags"]) != list(range(f["count"]))]
    beyond = [s for s in frames if s >= actual]

    payload_all = bytearray()
    for s in range(actual):
        f = frames.get(s)
        if f:
            for i in range(f["count"]):
                payload_all += f["frags"][i]
    local_summary = fnv1a(payload_all)

    print("declared(预告)={} actual(END实际)={} received={}".format(declared, actual, len(frames)))
    print("samples={} opus_bytes={} 时长≈{:.2f}s".format(
        end["totalSamples"], len(payload_all), end["totalSamples"] / 16000.0))
    print("summary: device=0x{:08x} independent=0x{:08x} → {}".format(
        end["summary"], local_summary, "MATCH" if local_summary == end["summary"] else "MISMATCH"))

    ok = True
    if actual > declared:
        print("[FAIL] END.totalFrames > BEGIN 预告（不得声多）"); ok = False
    if beyond:
        print("[FAIL] 收到 frameSeq >= END 声明: {}".format(beyond[:5])); ok = False
    if missing:
        print("[FAIL] 缺帧（不伪造完整音频）: {}...".format(missing[:5])); ok = False
    if incomplete:
        print("[FAIL] 分片不全: {}...".format(incomplete[:5])); ok = False
    if local_summary != end["summary"]:
        print("[FAIL] 摘要不一致"); ok = False
    if len(frames) != actual:
        print("[FAIL] 接收帧数 != END 声明: {} != {}".format(len(frames), actual)); ok = False

    if out_path and ok:
        blob = bytearray(struct.pack("<III", 0x4F503353, end["recID"], actual))
        for s in range(actual):
            f = frames[s]
            for i in range(f["count"]):
                p = f["frags"][i]
                blob += struct.pack("<IH", s, len(p)) + p
        with open(out_path, "wb") as fh:
            fh.write(blob)
        print("[OK] 已保存 {}".format(out_path))

    print("[PASS] 按键会话合法" if ok else "[FAIL] 按键会话不达标")
    sys.exit(0 if ok else 2)


asyncio.run(main())
