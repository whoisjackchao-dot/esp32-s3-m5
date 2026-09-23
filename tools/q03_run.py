#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q03 实时编排（方案B；契约 contracts/M3-Q03时延测量契约.md）。

单进程、单一 time.monotonic() 时钟：串口锚点线程 + BLE 采集(不发 READY) + 解码 + ASR/BR-04 计时。
每次运行产出一条 run record（JSONL）；失败也写记录（保留在分母）。

用法（Windows）：
  set STICKS3_BRIDGE_* 已由 bridge_env.ps1 装载；ASR Key 用 STICKS3_ASR_KEY 或交互输入
  python q03_run.py --group 5s --index 1 --port COM5 --out C:\\Temp\\ESP32-S3M5\\captures\\q03.jsonl
"""
import argparse
import asyncio
import getpass
import json
import os
import struct
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import q03_latency as q  # noqa: E402

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
FNV_SEED = 0x811C9DC5
CAP_DIR = os.path.join(os.path.dirname(HERE), "captures", "q03")


def now_ms():
    return time.monotonic() * 1000.0


def append_record(path, record):
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(record, ensure_ascii=False) + "\n")


def save_serial_log(group, index, lines):
    """持久化串口锚点日志（含到达时刻），供审计与 Δ/Δ₂ 复核。"""
    path = os.path.join(CAP_DIR, "serial_{}_{}.log".format(group, index))
    with open(path, "w", encoding="utf-8") as fh:
        for ts, text in lines:
            fh.write("{:.3f}\t{}\n".format(ts, text))
    return path


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


class SerialAnchor:
    """后台线程读取串口，按到达时刻(单调 ms)记录整行。"""

    def __init__(self, port, baud=115200):
        import serial as pyserial
        self.lines = []
        self._stop = threading.Event()
        self._ser = pyserial.Serial(port, baud, timeout=0.2)
        self._ser.dtr = True
        self._ser.rts = True
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

    def _run(self):
        buf = b""
        while not self._stop.is_set():
            try:
                data = self._ser.read(4096)
            except Exception:
                break
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self.lines.append((now_ms(), raw.decode("utf-8", "replace").rstrip()))

    def close(self):
        self._stop.set()
        try:
            self._t.join(timeout=1.0)
            self._ser.close()
        except Exception:
            pass


async def ble_capture(timeout_begin_s, timeout_end_s):
    from bleak import BleakScanner, BleakClient
    devs = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for d, (dv, av) in devs.items():
        if (dv.name or av.local_name or "") == "StickS3" or SVC in [str(u) for u in av.service_uuids]:
            target = d
            break
    if target is None:
        raise RuntimeError("scan: StickS3 not found")

    st = {"begin": None, "end": None, "ble_begin_ms": None, "ble_end_ms": None,
          "frames": {}, "begin_evt": asyncio.Event(), "end_evt": asyncio.Event()}

    def on_audio(_h, data):
        if len(data) < 2 or data[1] != 1:
            return
        t = data[0]
        if t == 0x01:
            st["begin"] = {"recID": le32(data, 2), "frameCount": le16(data, 6)}
            st["ble_begin_ms"] = now_ms()
            st["begin_evt"].set()
        elif t == 0x02 and len(data) >= 14:
            seq, fidx, fcnt, flen = le32(data, 6), data[10], data[11], le16(data, 12)
            fr = st["frames"].setdefault(seq, {"count": fcnt, "frags": {}})
            fr["frags"][fidx] = bytes(data[14:14 + flen])
            fr["count"] = fcnt
        elif t == 0x03 and len(data) >= 18:
            st["end"] = {"recID": le32(data, 2), "totalFrames": le32(data, 6),
                         "totalSamples": le32(data, 10), "summary": le32(data, 14)}
            st["ble_end_ms"] = now_ms()
            st["end_evt"].set()

    async with BleakClient(target, timeout=15.0) as client:
        try:
            await client.pair()
        except Exception:
            pass
        audio = client.services.get_characteristic(CHR_AUDIO)
        await client.start_notify(audio, on_audio)
        print("[OK] 已连接订阅（未发 READY）。请按住 BtnA 讲话后松开 …", flush=True)
        await asyncio.wait_for(st["begin_evt"].wait(), timeout=timeout_begin_s)
        await asyncio.wait_for(st["end_evt"].wait(), timeout=timeout_end_s)
    return st


def verify_frames(st):
    end = st["end"]
    actual = end["totalFrames"]
    frames = st["frames"]
    if actual > st["begin"]["frameCount"] or len(frames) != actual:
        return False
    payload = bytearray()
    for s in range(actual):
        f = frames.get(s)
        if not f or sorted(f["frags"]) != list(range(f["count"])):
            return False
        for i in range(f["count"]):
            payload += f["frags"][i]
    return fnv1a(payload) == end["summary"]


def save_capture(st, path):
    end, frames = st["end"], st["frames"]
    blob = bytearray(struct.pack("<III", 0x4F503353, end["recID"], end["totalFrames"]))
    for s in range(end["totalFrames"]):
        f = frames[s]
        for i in range(f["count"]):
            p = f["frags"][i]
            blob += struct.pack("<IH", s, len(p)) + p
    with open(path, "wb") as fh:
        fh.write(blob)
    return path


def classify_append_line(line):
    """把 voice_append.exe 的一行输出分类为 (是否 BR-04 响应行, status)。
    BR-04 行判定只用 ASCII 前缀 `[3]`：该行含中文（`[3] 桥接 HTTP …`），历史上
    因子进程 stdout 以 cp1252 解码成乱码而永不命中，导致 46 次运行 latency 全空
    （2026-09-23 实测缺陷）——故此处不得依赖中文。"""
    br04 = line.startswith("[3]")
    if '"applied"' in line:
        status = "applied"
    elif '"uncertain"' in line:
        status = "uncertain"
    elif '"processing"' in line:
        status = "processing"
    else:
        status = None
    return br04, status


def run_append(wav_path):
    """运行 voice_append.exe，返回 (t1_ms, br04_status)。t1 = 出现 BR-04 响应行的时刻。"""
    exe = os.path.join(os.path.dirname(HERE), "host", "src", "voice_append.exe")
    proc = subprocess.Popen([exe, wav_path], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                            errors="replace", bufsize=1)
    t1, status = None, "no_response"
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        br04, line_status = classify_append_line(line)
        if t1 is None and br04:
            t1 = now_ms()
        if line_status:
            status = line_status
    proc.wait()
    return t1, status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", required=True, choices=list(q.GROUPS))
    ap.add_argument("--index", type=int, default=1)
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--out", default=os.path.join(CAP_DIR, "records.jsonl"))
    ap.add_argument("--end-timeout", type=float, default=45.0)
    ap.add_argument("--begin-timeout", type=float, default=120.0)
    args = ap.parse_args()

    missing = [v for v in ("STICKS3_BRIDGE_URL", "STICKS3_BRIDGE_TOKEN",
                           "STICKS3_TUI_EPOCH", "STICKS3_SESSION_ID")
               if not os.environ.get(v)]
    if missing:
        print("[FAIL] 桥接变量缺失（{}）：Q03 终点需要 BR-04 applied".format(", ".join(missing)))
        return 2

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    serial_anchor = SerialAnchor(args.port)
    time.sleep(1.5)  # 串口打开后留出稳定时间，再开始 BLE 扫描
    try:
        st = asyncio.run(ble_capture(args.begin_timeout, args.end_timeout))
    except Exception as exc:
        serial_anchor.close()
        print("[FAIL] BLE 采集: {!r}".format(exc))
        save_serial_log(args.group, args.index, serial_anchor.lines)
        record = q.build_record(args.group, args.index, None, None, "ble_capture_fail")
        append_record(args.out, record)
        print("[FAIL] {} #{} latency=-（失败记录已写入，保留分母）".format(args.group, args.index))
        return 1
    time.sleep(1.0)
    serial_anchor.close()
    save_serial_log(args.group, args.index, serial_anchor.lines)

    ok_summary = verify_frames(st)
    anchor = q.pick_anchor_ts(serial_anchor.lines, args.group, st["ble_begin_ms"])
    delta = q.serial_latency_delta_ms(serial_anchor.lines, st["ble_end_ms"])

    t1, status = None, "not_run"
    if anchor is None:
        print("[FAIL] 未找到起点锚点行（group={}）".format(args.group))
        record = q.build_record(args.group, args.index, None, None, "no_anchor",
                                delta, st["end"]["totalFrames"], st["end"]["totalSamples"],
                                ok_summary)
    else:
        t0 = anchor[0]
        cap = save_capture(st, os.path.join(CAP_DIR, "rec_{}_{}.s3pcap".format(args.group, args.index)))
        wav = cap[:-7] + ".wav"
        rc = subprocess.call([sys.executable, os.path.join(HERE, "decode_capture.py"), cap, wav])
        if rc != 0:
            print("[FAIL] 解码失败")
            record = q.build_record(args.group, args.index, t0, None, "decode_fail",
                                    delta, st["end"]["totalFrames"], st["end"]["totalSamples"],
                                    ok_summary)
        else:
            if not os.environ.get("STICKS3_ASR_KEY"):
                os.environ["STICKS3_ASR_KEY"] = getpass.getpass("智谱API Key（隐藏输入）: ")
            t1, status = run_append(wav)
            record = q.build_record(args.group, args.index, t0, t1, status, delta,
                                    st["end"]["totalFrames"], st["end"]["totalSamples"],
                                    ok_summary)

    append_record(args.out, record)
    lat = record["latency_s"]
    print("[{}] {} #{} latency={} delta={}ms frames={} duration={}s duration_ok={} summary_match={}".format(
        "OK" if record["ok"] else "FAIL", args.group, args.index,
        "-" if lat is None else "{:.3f}s".format(lat), delta,
        record["end_total_frames"], record["duration_s"], record["duration_ok"],
        record["summary_match"]))
    if record["duration_s"] is not None and not record["duration_ok"]:
        print("[REDO] 录音 {}s 不在 {} 合格窗 {}s 内——本次无效，请重做".format(
            record["duration_s"], args.group, q.DURATION_WINDOWS_S.get(args.group)))
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
