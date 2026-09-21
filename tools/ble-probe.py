# -*- coding: utf-8 -*-
"""M2 GATT 空口验证探针（bleak，Windows 主机侧）。

按 contracts/M2-BLE线协议冻结.md：
- 发现服务 22587f80-334a-53d6-beb8-8a6ace31af07 与三特征
- 订阅 Audio Notify / Device Event（触发加密配对 Just Works）
- 写 READY{0x10, ver=1, mtu=247} 与两拍 HEARTBEAT{0x11, seq}
输出全程证据行；退出码 0=全部通过。
"""
import asyncio
import sys

sys.stdout.reconfigure(errors="replace")

from bleak import BleakScanner, BleakClient

SVC = "22587f80-334a-53d6-beb8-8a6ace31af07"
CHR_AUDIO = "54d5e361-d549-55cc-ab98-66ebc8afe4f3"
CHR_CTRL = "14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0"
CHR_EVENT = "a1d7042f-43a0-53b5-ad2f-a1b3ae57c79c"

events = []


def note(ok, label, detail=""):
    line = "[{}] {}{}".format("PASS" if ok else "FAIL", label, (" | " + detail) if detail else "")
    events.append(line)
    print(line, flush=True)


async def main():
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

        services = client.services
        svc = services.get_service(SVC)
        note(svc is not None, "service discover", SVC)

        audio = client.services.get_characteristic(CHR_AUDIO)
        ctrl = client.services.get_characteristic(CHR_CTRL)
        event = client.services.get_characteristic(CHR_EVENT)
        note(audio is not None, "char Audio Notify", CHR_AUDIO if audio else "缺失")
        note(ctrl is not None, "char Control Write", CHR_CTRL if ctrl else "缺失")
        note(event is not None, "char Device Event", CHR_EVENT if event else "缺失")

        got_audio = asyncio.Event()
        got_event = asyncio.Event()

        def on_audio(_h, data):
            note(True, "audio notify", data.hex())
            got_audio.set()

        def on_event(_h, data):
            note(True, "event notify", data.hex())
            got_event.set()

        await client.start_notify(audio, on_audio)
        await client.start_notify(event, on_event)
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
        # Write With Response: WNR 会在未加密链路被静默丢弃，且不会触发配对；
        # response=True 让服务端安全错误浮出水面，Windows 据此自动配对后重试成功。
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

        await client.stop_notify(audio)
        await client.stop_notify(event)

    print("--- SUMMARY ---", flush=True)
    fails = [e for e in events if e.startswith("[FAIL]")]
    print("total={} fails={}".format(len(events), len(fails)), flush=True)
    sys.exit(0 if not fails else 2)


asyncio.run(main())
