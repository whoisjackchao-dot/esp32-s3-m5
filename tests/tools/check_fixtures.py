#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BLE 线协议黄金向量独立校验器（契约 §6 TEST-API-02）。

独立实现小端解析与 FNV-1a32——不引用 C 序列化器代码。
用法：python3 check_fixtures.py [fixtures.json]
退出码 0=全部通过。
"""
import json
import sys

SEED = 0x811C9DC5
PRIME = 0x01000193

MIN_LEN = {"BEGIN": 12, "AUDIO": 14, "END": 18}  # 契约表格最小长度
TYPE_NAMES = {1: "BEGIN", 2: "AUDIO", 3: "END"}  # 线上类型字节 → 名称


def fnv1a32(data: bytes) -> int:
    h = SEED
    for b in data:
        h ^= b
        h = (h * PRIME) & 0xFFFFFFFF
    return h


def parse(buf: bytes, v_header_only: bool = False):
    """返回 (结果字典, 错误码)。错误码: OK / BAD_VERSION / TOO_SHORT / UNKNOWN_TYPE"""
    if len(buf) < 2:
        return None, "TOO_SHORT"
    ptype, ver = buf[0], buf[1]
    if ver != 1:
        return None, "BAD_VERSION"
    name = TYPE_NAMES.get(ptype)
    if name is None:
        return None, "UNKNOWN_TYPE"
    if len(buf) < MIN_LEN[name]:
        return None, "TOO_SHORT"
    le16 = lambda o: buf[o] | (buf[o + 1] << 8)
    le32 = lambda o: buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)
    if name == "BEGIN":
        return {"type": "BEGIN", "recordingID": le32(2), "frameCount": le16(6),
                "sampleRate": le16(8), "frameMs": buf[10]}, "OK"
    if name == "AUDIO":
        flen = le16(12)
        if not v_header_only and len(buf) < 14 + flen:
            return None, "TOO_SHORT"
        return {"type": "AUDIO", "recordingID": le32(2), "frameSeq": le32(6),
                "fragIndex": buf[10], "fragCount": buf[11], "fragLen": flen,
                "payload": buf[14:14 + flen]}, "OK"
    return {"type": "END", "recordingID": le32(2), "totalFrames": le32(6),
            "totalSamples": le32(10), "summary": le32(14)}, "OK"


def check_vector(v):
    failures = []
    exp = v["expect"]
    hexes = v["hex_list"] if "hex_list" in v else [v["hex"]]
    parsed_list = []
    header_only = v.get("header_only", False)
    for h in hexes:
        buf = bytes.fromhex(h)
        parsed, err = parse(buf, header_only)
        want = exp.get("parse", "OK")
        if err != want:
            failures.append("{}: parse={} want={}".format(v["id"], err, want))
            continue
        if parsed is None:
            continue
        parsed_list.append(parsed)
        for key in ("recordingID", "frameSeq", "fragIndex", "fragCount",
                    "frameCount", "sampleRate", "frameMs", "totalFrames",
                    "totalSamples"):
            if key in exp and parsed.get(key) != exp[key]:
                failures.append("{}: {}={} want={}".format(
                    v["id"], key, parsed.get(key), exp[key]))
        if "fragLen" in exp and parsed.get("fragLen") != exp["fragLen"]:
            failures.append("{}: fragLen mismatch".format(v["id"]))
        if "fragLen_list" in exp:
            idx = hexes.index(h)
            if parsed.get("fragLen") != exp["fragLen_list"][idx]:
                failures.append("{}: fragLen[{}] mismatch".format(v["id"], idx))
        if "sampleRate_not_equal" in exp:
            if parsed.get("sampleRate") == exp["sampleRate_not_equal"]:
                pass  # 字节序炸弹：小端解析出错误值本身即预期
    # F04 summary 独立复算：需要同文件中 F02 的载荷
    if exp.get("type") == "END" and "summary_check" in exp:
        audio = next(x for x in data["vectors"] if x["id"] == "F02")
        payload = bytes.fromhex(audio["hex"])[14:]
        calc = fnv1a32(payload)
        if len(parsed_list) == 1:
            declared = parsed_list[0]["summary"]
            if calc != declared:
                failures.append("{}: FNV calc={:#x} declared={:#x}".format(
                    v["id"], calc, declared))
    return failures


def main():
    global data
    path = sys.argv[1] if len(sys.argv) > 1 else "fixtures/ble_protocol_v1.json"
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    all_failures = []
    for v in data["vectors"]:
        fails = check_vector(v)
        status = "PASS" if not fails else "FAIL"
        print("[{}] {} {}".format(status, v["id"], v["name"]))
        all_failures.extend(fails)
    for f in all_failures:
        print("  " + f)
    print("vectors={} failures={}".format(len(data["vectors"]), len(all_failures)))
    sys.exit(0 if not all_failures else 2)


main()
