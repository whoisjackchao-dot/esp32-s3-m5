#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A3b 断线重连演示（BLE-16/Q05 实机）。

流程：
1. 连接 + 订阅 + READY 触发会话，接收 ~15 帧后**主动断开**（模拟意外断线）
2. 重连计时：从断开到重新连接成功 ≤ 15s（Q05 门槛），不重新配对
3. 无重放校验：重连后不写 READY，~3s 内不得收到任何会话数据（Q05：不重放语音事务）
4. 会话健康：新 READY → BEGIN 正常到达

退出码 0 = 全部通过。前置：ES8311 正常。
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

Q05_RECONNECT_LIMIT_S = 15.0


def le32(d, o):
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)


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

    # 阶段 1：建连 + 会话启动 + 接收若干帧
    frame_count = 0
    got_begin = False
    async with BleakClient(target, timeout=15.0) as client:
        try:
            await client.pair()
        except Exception:
            pass
        audio = client.services.get_characteristic(CHR_AUDIO)
        ctrl = client.services.get_characteristic(CHR_CTRL)

        got_begin_evt = asyncio.Event()

        def on_audio(_h, data):
            nonlocal frame_count, got_begin
            if len(data) < 2 or data[1] != 1:
                return
            if data[0] == 0x01:
                got_begin = True
                got_begin_evt.set()
            elif data[0] == 0x02:
                frame_count += 1

        await client.start_notify(audio, on_audio)
        try:
            await client.write_gatt_char(ctrl, READY, response=True)
        except Exception as e:
            print("[FAIL] READY:", repr(e)[:100])
            sys.exit(2)
        try:
            await asyncio.wait_for(got_begin_evt.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            print("[FAIL] BEGIN not received（检查 ES8311 状态）")
            sys.exit(2)
        await asyncio.sleep(1.0)  # 收 ~15 帧
        print("[PASS] session started, frames_received={}".format(frame_count))
        drop_at = frame_count
    # with 块退出 = 主动断开（会话中断点 ≈ drop_at 帧）
    print("disconnected mid-session at frame ≈{}".format(drop_at))

    # 阶段 2：重连计时（Q05：15s 内，不重新配对）
    t0 = time.monotonic()
    client2 = None
    while time.monotonic() - t0 < Q05_RECONNECT_LIMIT_S:
        try:
            candidate = BleakClient(target, timeout=5.0)
            await candidate.connect()
            client2 = candidate
            break
        except Exception:
            await asyncio.sleep(0.5)
    if client2 is None:
        print("[FAIL] reconnect exceeded {}s (Q05)".format(Q05_RECONNECT_LIMIT_S))
        sys.exit(2)
    reconnect_s = time.monotonic() - t0
    print("[PASS] reconnect in {:.2f}s (limit {}s, no re-pairing)".format(
        reconnect_s, Q05_RECONNECT_LIMIT_S))

    # 阶段 3：无重放校验（重连后不发 READY，不得收到会话数据）
    stale = {"n": 0}
    stale_evt = asyncio.Event()

    def on_audio2(_h, data):
        if len(data) >= 2 and data[0] in (0x01, 0x02, 0x03):
            stale["n"] += 1
            stale_evt.set()

    audio2 = client2.services.get_characteristic(CHR_AUDIO)
    await client2.start_notify(audio2, on_audio2)
    try:
        await asyncio.wait_for(stale_evt.wait(), timeout=3.0)
        print("[FAIL] replay detected: {} stale session packets (Q05 禁止重放)".format(
            stale["n"]))
        sys.exit(2)
    except asyncio.TimeoutError:
        print("[PASS] no replay after reconnect ({} stale packets)".format(stale["n"]))

    # 阶段 4：新会话健康
    begin2 = asyncio.Event()

    def on_audio3(_h, data):
        if len(data) >= 2 and data[0] == 0x01:
            begin2.set()

    await client2.stop_notify(audio2)
    await client2.start_notify(audio2, on_audio3)
    ctrl2 = client2.services.get_characteristic(CHR_CTRL)
    try:
        await client2.write_gatt_char(ctrl2, READY, response=True)
        await asyncio.wait_for(begin2.wait(), timeout=5.0)
        print("[PASS] new session healthy after reconnect")
    except Exception as e:
        print("[FAIL] new session after reconnect:", repr(e)[:100])
        sys.exit(2)

    print("--- reconnect demo PASS ---")


asyncio.run(main())
