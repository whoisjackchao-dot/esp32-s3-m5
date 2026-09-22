#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A3c 陌生设备拒绝演示（BLE-01/02 实机）。

前置：已用 PowerShell 解除 StickS3 配对（模拟陌生主机，无 bond）。
流程：不配对直连 → 订阅 CCCD（NimBLE 对 CCCD 不强制加密，可成功）→
写 READY（加密特征）→ 期望 Insufficient Authentication 拒绝 →
随后显式配对 → 重写成功（证明配对后恢复正常）。
"""
import asyncio
import sys

from bleak import BleakScanner, BleakClient

sys.stdout.reconfigure(errors="replace")

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
READY = bytes([0x10, 0x01, 0xF7, 0x00])


async def main():
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for addr, (dev, adv) in devices.items():
        if (dev.name or adv.local_name or "") == "StickS3":
            target = dev
            break
    if target is None:
        print("[FAIL] scan: StickS3 not found")
        sys.exit(1)
    print("[PASS] scan found", target.address, flush=True)

    rejected = False
    async with BleakClient(target, timeout=15.0) as client:
        svc = client.services.get_service(SVC)
        ctrl = client.services.get_characteristic(CHR_CTRL)
        if ctrl is None:
            # 未配对时服务发现可能受限：再发现一次
            await client.get_services()
            ctrl = client.services.get_characteristic(CHR_CTRL)

        try:
            await client.write_gatt_char(ctrl, READY, response=True)
            print("[FAIL] stranger write ACCEPTED — 拒绝机制未生效", flush=True)
        except Exception as e:
            rejected = True
            print("[PASS] stranger write REJECTED:", repr(e)[:110], flush=True)

        if rejected:
            try:
                await client.pair()
                await asyncio.sleep(0.5)
                await client.write_gatt_char(ctrl, READY, response=True)
                print("[PASS] after pairing, write OK — bond 恢复闭环", flush=True)
            except Exception as e:
                print("[FAIL] post-pair write:", repr(e)[:110], flush=True)
                sys.exit(2)
    sys.exit(0 if rejected else 2)


asyncio.run(main())
