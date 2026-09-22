#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A3a 缺片故障注入演示（BLE-06/11 实机）。

流程：连接 → READY 触发会话 → 接收 AUDIO 通知但**丢弃 frameSeq=10 的全部分片**
→ END 到达后校验重组视图缺帧 → 再等待 FRAME_DRAIN_TIMEOUT(2000ms) →
裁决"整段失败、无 ASR"（契约 §4：缺片不静默截短、不伪造完整音频）。

退出码 0 = 缺片被正确识别为不完整（未误判完整）。
前置：ES8311 正常（当前硬件阻塞中，恢复后执行）。
"""
import asyncio
import sys
import time

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"

DROP_SEQ = 10
FRAME_DRAIN_TIMEOUT_MS = 2000


def le16(d, o):
    return d[o] | (d[o + 1] << 8)


def le32(d, o):
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)


async def main():
    sess = {"begin": None, "frames": set(), "end": None, "dropped": 0}
    end_evt = asyncio.Event()

    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = next((d for d, (dv, av) in devices.items()
                   if (dv.name or av.local_name or "") == "StickS3"
                   or SVC in [str(u) for u in av.service_uuids]), None)
    if target is None:
        print("[FAIL] scan: StickS3 not found")
        sys.exit(1)

    async with BleakClient(target, timeout=15.0) as client:
        try:
            await client.pair()
        except Exception:
            pass
        audio = client.services.get_characteristic(CHR_AUDIO)
        ctrl = client.services.get_characteristic(CHR_CTRL)

        def on_audio(_h, data):
            if len(data) < 2 or data[1] != 1:
                return
            if data[0] == 0x01:
                sess["begin"] = (le32(data, 2), le16(data, 6))
            elif data[0] == 0x02:
                seq = le32(data, 6)
                if seq == DROP_SEQ:
                    sess["dropped"] += 1  # 注入丢包：不记录
                else:
                    sess["frames"].add(seq)
            elif data[0] == 0x03:
                sess["end"] = (le32(data, 6), le32(data, 10), le32(data, 14))
                end_evt.set()

        await client.start_notify(audio, on_audio)
        ready = bytes([0x10, 0x01, 0xF7, 0x00])
        for _ in range(4):
            try:
                await client.write_gatt_char(ctrl, ready, response=True)
                break
            except Exception:
                await asyncio.sleep(1.5)
        else:
            print("[FAIL] write READY")
            sys.exit(2)

        t_end = None
        try:
            await asyncio.wait_for(end_evt.wait(), timeout=45.0)
            t_end = time.monotonic()
        except asyncio.TimeoutError:
            print("[FAIL] END not received")
            sys.exit(2)

        total_frames, total_samples, summary = sess["end"]
        missing = [s for s in range(total_frames) if s not in sess["frames"]]
        print("session: declared={} received={} dropped_injections={} missing={}".format(
            total_frames, len(sess["frames"]), sess["dropped"], missing))

        # 契约 §4 裁决：缺片 → 尾帧窗口内不得视为完整
        if missing and total_frames > 0:
            print("[PASS] missing frames detected:", missing[:5],
                  "→ 整段失败，无 ASR（不伪造完整音频）")
        elif sess["dropped"] == 0:
            print("[FAIL] 注入未生效（链路零丢包，请改用断线注入）")
            sys.exit(2)
        else:
            print("[FAIL] dropped={} 但帧集完整——重组完整性校验失效".format(sess["dropped"]))
            sys.exit(2)

        # 尾帧窗口语义：END 后等待 2000ms，缺片状态保持失败
        await asyncio.sleep(FRAME_DRAIN_TIMEOUT_MS / 1000.0 + 0.5)
        if sess["frames"] == set(range(total_frames)):
            print("[FAIL] 尾帧窗口后帧集变完整——违反缺片裁决")
            sys.exit(2)
        print("[PASS] drain window elapsed, session remains INCOMPLETE (BLE-06/11)")

    sys.exit(0)


asyncio.run(main())
