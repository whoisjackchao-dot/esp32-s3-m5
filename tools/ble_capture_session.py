#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A4 实机捕获：完整 30s 会话 → .s3pcap + 独立摘要核对。

流程：连接 → 订阅 → READY → 接收 BEGIN/AUDIO/END →
按 (seq, fragIndex) 重组 Opus 帧 → 独立计算 FNV-1a 与 END.summary 对比
（黄金向量纪律：期望值独立于被测设备固件推导）→ 落盘 .s3pcap。

格式：[u32 'S3PO'][u32 recID][u32 frameCount] + N × [u32 seq][u16 len][payload]
退出码 0 = 捕获完整且摘要一致。
"""
import asyncio
import hashlib
import struct
import sys
import time

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
READY = bytes([0x10, 0x01, 0xF7, 0x00])
END_TIMEOUT_S = 45.0
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
    out_path = sys.argv[1] if len(sys.argv) > 1 else "session.s3pcap"
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for d, (dv, av) in devices.items():
        if (dv.name or av.local_name or "") == "StickS3" or SVC in [str(u) for u in av.service_uuids]:
            target = d
            break
    if target is None:
        print("[FAIL] scan: StickS3 not found")
        sys.exit(1)

    sess = {"begin": None, "end": None,
            "frames": {},  # seq -> {"count": n, "frags": {idx: bytes}}
            "end_evt": asyncio.Event()}

    def on_audio(_h, data):
        if len(data) < 2 or data[1] != 1:
            return
        t = data[0]
        if t == 0x01:
            sess["begin"] = {"recID": le32(data, 2), "frameCount": le16(data, 6),
                             "sampleRate": le16(data, 8), "frameMs": data[10]}
        elif t == 0x02 and len(data) >= 14:
            rec_id = le32(data, 2)
            seq = le32(data, 6)
            fidx, fcnt = data[10], data[11]
            flen = le16(data, 12)
            payload = bytes(data[14:14 + flen])
            fr = sess["frames"].setdefault(seq, {"count": fcnt, "frags": {}})
            fr["frags"][fidx] = payload
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
        ctrl = client.services.get_characteristic(CHR_CTRL)
        await client.start_notify(audio, on_audio)
        for _ in range(4):
            try:
                await client.write_gatt_char(ctrl, READY, response=True)
                break
            except Exception:
                await asyncio.sleep(1.5)
        else:
            print("[FAIL] write READY")
            sys.exit(2)
        t0 = time.monotonic()
        try:
            await asyncio.wait_for(sess["end_evt"].wait(), timeout=END_TIMEOUT_S)
        except asyncio.TimeoutError:
            print("[FAIL] END not received within {}s (frames={})".format(
                END_TIMEOUT_S, len(sess["frames"])))
            sys.exit(2)
        elapsed = time.monotonic() - t0

    begin, end = sess["begin"], sess["end"]
    frames = sess["frames"]
    total_declared = begin["frameCount"] if begin else end["totalFrames"]

    # 完整性：帧数连续、分片齐全
    missing = [s for s in range(total_declared) if s not in frames]
    incomplete = [s for s, f in frames.items()
                  if sorted(f["frags"]) != list(range(f["count"]))]
    if missing:
        print("[FAIL] missing frames (INCOMPLETE, 不伪造完整音频): {}...".format(missing[:5]))
        sys.exit(2)
    if incomplete:
        print("[FAIL] incomplete fragments at seq: {}...".format(incomplete[:5]))
        sys.exit(2)

    # 按序重组 + 独立摘要
    payload_all = bytearray()
    for s in range(total_declared):
        f = frames[s]
        for i in range(f["count"]):
            payload_all += f["frags"][i]
    local_summary = fnv1a(payload_all)

    ok_summary = local_summary == end["summary"]
    ok_counts = (end["totalFrames"] == total_declared == len(frames))
    print("session: recID={} declared={} received={} frags_complete=True".format(
        end["recID"], total_declared, len(frames)))
    print("samples: declared={} elapsed={:.1f}s opus_bytes={}".format(
        end["totalSamples"], elapsed, len(payload_all)))
    print("summary: device=0x{:08x} independent(FNV-1a)=0x{:08x} → {}".format(
        end["summary"], local_summary, "MATCH" if ok_summary else "MISMATCH"))

    if not ok_summary:
        print("[FAIL] summary mismatch（独立计算与设备不一致）")
        sys.exit(2)
    if not ok_counts:
        print("[FAIL] frame count mismatch: declared={} end={} received={}".format(
            total_declared, end["totalFrames"], len(frames)))
        sys.exit(2)

    # 落盘 .s3pcap
    blob = bytearray(struct.pack("<III", 0x4F503353, end["recID"], len(frames)))
    for s in range(total_declared):
        f = frames[s]
        for i in range(f["count"]):
            p = f["frags"][i]
            blob += struct.pack("<IH", s, len(p)) + p
    with open(out_path, "wb") as f:
        f.write(blob)
    sha = hashlib.sha256(blob).hexdigest()
    print("[PASS] capture saved: {} ({} bytes, sha256={}...)".format(
        out_path, len(blob), sha[:16]))
    print("[PASS] A4 independent summary verification OK")


asyncio.run(main())
