#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q05「重连不重放」确定性验证（修正版）。

背景：原 ble_reconnect_test.py 用 BleakClient.close() 制造断链，但 Windows 会
缓存 ACL 连接、不真正拆链，导致"重放"为假象（详见
evidence/M2/2026-09-22-M2演示执行记录.md §6）。

本版改为由**设备主动断开**（控制消息 0x7F，测试钩子），断链对设备必然可观测：
1. 连接 + 订阅 + READY 触发会话，收 ~15 帧
2. 写 0x7F → 设备主动 terminate 链路（真实断链）
3. 重连（不重新配对）+ 订阅
4. 静默 3s：**不得收到任何会话包**（0x01/0x02/0x03）→ Q05
5. 新 READY → BEGIN 正常到达 → 会话健康

退出码 0 = 全部通过。前置：ES8311 正常；固件含 0x7F 测试钩子。
"""
import asyncio
import sys
import time

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
READY = bytes([0x10, 0x01, 0xF7, 0x00])
FORCE_DROP = bytes([0x7F])
QUIET_S = 3.0


async def find_device():
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for addr, (dev, adv) in devices.items():
        if (dev.name or adv.local_name or "") == "StickS3":
            return dev
    return None


async def main():
    target = await find_device()
    if target is None:
        print("[FAIL] scan: StickS3 not found")
        sys.exit(1)
    print("[PASS] scan found", target.address, flush=True)

    frame_count = 0
    begin_evt = asyncio.Event()

    async with BleakClient(target, timeout=15.0) as c1:
        try:
            await c1.pair()
        except Exception:
            pass
        audio = c1.services.get_characteristic(CHR_AUDIO)
        ctrl = c1.services.get_characteristic(CHR_CTRL)

        def on_audio(_h, data):
            nonlocal frame_count
            if len(data) < 2 or data[1] != 1:
                return
            if data[0] == 0x01:
                begin_evt.set()
            elif data[0] == 0x02:
                frame_count += 1

        await c1.start_notify(audio, on_audio)
        await c1.write_gatt_char(ctrl, READY, response=True)
        try:
            await asyncio.wait_for(begin_evt.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            print("[FAIL] BEGIN not received")
            sys.exit(2)
        await asyncio.sleep(1.0)
        print("[PASS] session started, frames_received={}".format(frame_count), flush=True)
        if frame_count < 5:
            print("[FAIL] too few frames before drop")
            sys.exit(2)
        # 设备主动断链
        try:
            await c1.write_gatt_char(ctrl, FORCE_DROP, response=True)
        except Exception as e:
            print("[INFO] FORCE_DROP write returned", repr(e)[:80], flush=True)
        await asyncio.sleep(1.0)
        still = c1.is_connected
        print("[PASS] device-initiated drop; c1.is_connected={}".format(still), flush=True)

    # 等设备回到广播
    await asyncio.sleep(1.5)

    c2 = None
    t0 = time.monotonic()
    while time.monotonic() - t0 < 15.0:
        try:
            cand = BleakClient(target, timeout=5.0)
            await cand.connect()
            c2 = cand
            break
        except Exception:
            await asyncio.sleep(0.5)
    if c2 is None:
        print("[FAIL] reconnect timeout")
        sys.exit(2)
    print("[PASS] reconnect in {:.2f}s".format(time.monotonic() - t0), flush=True)

    stale = {"n": 0}
    stale_evt = asyncio.Event()

    def on_audio2(_h, data):
        if len(data) >= 2 and data[0] in (0x01, 0x02, 0x03):
            stale["n"] += 1
            stale_evt.set()

    audio2 = c2.services.get_characteristic(CHR_AUDIO)
    await c2.start_notify(audio2, on_audio2)
    try:
        await asyncio.wait_for(stale_evt.wait(), timeout=QUIET_S)
        print("[FAIL] replay after REAL disconnect: {} stale packets (Q05)".format(stale["n"]))
        sys.exit(2)
    except asyncio.TimeoutError:
        print("[PASS] no replay after real reconnect ({} stale packets in {:.1f}s)".format(
            stale["n"], QUIET_S), flush=True)

    begin2 = asyncio.Event()

    def on_audio3(_h, data):
        if len(data) >= 2 and data[0] == 0x01:
            begin2.set()

    await c2.stop_notify(audio2)
    await c2.start_notify(audio2, on_audio3)
    ctrl2 = c2.services.get_characteristic(CHR_CTRL)
    await c2.write_gatt_char(ctrl2, READY, response=True)
    try:
        await asyncio.wait_for(begin2.wait(), timeout=5.0)
        print("[PASS] new session healthy after reconnect", flush=True)
    except asyncio.TimeoutError:
        print("[FAIL] new session BEGIN not received")
        sys.exit(2)

    await c2.disconnect()
    print("--- Q05 reconnect demo PASS ---")


asyncio.run(main())
