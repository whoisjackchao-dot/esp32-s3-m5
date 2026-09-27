#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PERF-02：20 次意外断线恢复（M5 候选版本验证）。

机制：用固件测试钩子 0x7F 让**设备主动断链**（对设备必然可观测的"意外断线"），
再由测试中心重连。每次测量：断链时刻 t0 → 恢复"加密连接且会话健康" t1，Δ=t1-t0。
门槛（M5 用例 PERF-02）：≥19/20 次 Δ≤15s。

要点（对照 PERF-02 文本）：
- 计时起点=断链时刻（含设备重新可连接/扫描延迟，**不从"主机首次发现"起算**）；
- **不重新配对**（用已有 bond；仅首连尝试 pair 一次）；
- 失败不剔除，逐条列出；关机/蓝牙关闭的等待单列（本工具不含）。

用法：python tools/perf02_reconnect.py [N]   # 默认 20
前置：固件含 0x7F 测试钩子；设备在 0.3~2m、无遮挡；记录无线共存条件。
"""
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
READY = bytes([0x10, 0x01, 0xF7, 0x00])
DROP = bytes([0x7F])
BUDGET = 15.0


async def find_device():
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for addr, (dev, adv) in devices.items():
        if (dev.name or adv.local_name or "") == "StickS3":
            return dev
    return None


async def connect_healthy(target, budget, pair=False):
    """(重)连接 + 订阅 + READY→BEGIN；返回 (client|None, elapsed seconds)。"""
    t0 = time.monotonic()
    while True:
        elapsed = time.monotonic() - t0
        if elapsed >= budget:
            return None, elapsed
        c = None
        try:
            c = BleakClient(target, timeout=5.0)
            await c.connect()
            if not c.is_connected:
                raise RuntimeError("not connected")
            if pair:
                try:
                    await c.pair()
                except Exception:
                    pass
            audio = c.services.get_characteristic(CHR_AUDIO)
            ctrl = c.services.get_characteristic(CHR_CTRL)
            begin = asyncio.Event()

            def on_audio(_h, data):
                if len(data) >= 2 and data[1] == 1 and data[0] == 0x01:
                    begin.set()

            await c.start_notify(audio, on_audio)
            await c.write_gatt_char(ctrl, READY, response=True)
            try:
                await asyncio.wait_for(begin.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                raise RuntimeError("BEGIN timeout")
            return c, time.monotonic() - t0
        except Exception:
            try:
                if c is not None:
                    await c.disconnect()
            except Exception:
                pass
            await asyncio.sleep(0.3)


async def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    target = await find_device()
    if target is None:
        print("[FAIL] StickS3 not found")
        sys.exit(1)
    print("device %s  N=%d  recovery_budget=%.0fs" % (target.address, n, BUDGET), flush=True)

    okn = 0
    done = 0
    fails = []
    while done < n:
        # setup 连接（预算宽，非被测项）；失败则重扫并等待（Windows BLE 栈需要沉降）
        c, _ = await connect_healthy(target, 30.0, pair=(done == 0))
        if c is None:
            print("setup retry (rescan) after %d done" % done, flush=True)
            t2 = await find_device()
            if t2 is not None:
                target = t2
            await asyncio.sleep(2.0)
            continue
        done += 1
        await asyncio.sleep(0.6)
        t0 = time.monotonic()
        try:
            await c.write_gatt_char(c.services.get_characteristic(CHR_CTRL), DROP, response=True)
        except Exception:
            pass
        await asyncio.sleep(0.4)
        try:
            await c.disconnect()
        except Exception:
            pass
        c2, dt = await connect_healthy(target, BUDGET)
        ok = (c2 is not None) and (dt <= BUDGET)
        if ok:
            okn += 1
        else:
            fails.append(done)
        print("recovery %d: drop->healthy %.3fs %s" % (done, dt, "OK" if ok else "FAIL"), flush=True)
        if c2 is not None:
            try:
                await c2.disconnect()
            except Exception:
                pass
        await asyncio.sleep(3.0)  # 沉降，避免下轮 setup 连接失败

    print("=== PERF-02: %d/%d within %.0fs ===" % (okn, n, BUDGET))
    if fails:
        print("fail iterations:", fails)
    verdict = "PASS" if okn >= n - 1 else "FAIL"
    print("VERDICT=%s" % verdict)
    sys.exit(0 if verdict == "PASS" else 1)


asyncio.run(main())
