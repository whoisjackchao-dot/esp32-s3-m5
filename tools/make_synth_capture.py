#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""合成 .s3pcap 生成器（A4 工具链往返验证用，不依赖设备）。

440Hz 正弦波 3 秒 → Opus 编码（PyAV/libopus, 16kHz mono 60ms 帧）
→ .s3pcap 格式。用法：python make_synth_capture.py <out.s3pcap>
"""
import math
import struct
import sys

import av
import numpy as np

MAGIC = 0x4F503353  # 'S3PO'
RATE = 16000
FRAME_MS = 60
SAMPLES = RATE * FRAME_MS // 1000  # 960


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "synth.s3pcap"
    seconds = 3.0
    total = int(RATE * seconds)

    enc = av.CodecContext.create("libopus", "w")
    enc.sample_rate = RATE
    enc.layout = "mono"
    enc.format = "s16"
    enc.bit_rate = 24000
    enc.open(strict=False)
    fs = enc.frame_size  # 编码器要求的每帧样本数（只读）
    print("encoder frame_size={} bits_rate={}".format(fs, enc.bit_rate))

    frames = []
    seq = 0
    pts = 0
    n = 0
    while n < total:
        count = min(fs, total - n)
        raw = np.zeros((1, fs), dtype=np.int16)  # 形状 (channels, samples)
        for i in range(fs):
            idx = n + i
            if idx < total:
                raw[0][i] = int(12000 * math.sin(2 * math.pi * 440 * idx / RATE))
        frame = av.AudioFrame.from_ndarray(raw, format="s16", layout="mono")
        frame.sample_rate = RATE
        frame.pts = pts
        pts += fs
        for packet in enc.encode(frame):
            frames.append((seq, bytes(packet)))
            seq += 1
        n += count
    for packet in enc.encode(None):  # flush
        frames.append((seq, bytes(packet)))
        seq += 1

    with open(out, "wb") as f:
        f.write(struct.pack("<III", MAGIC, 1, len(frames)))
        for seq, payload in frames:
            f.write(struct.pack("<IH", seq, len(payload)))
            f.write(payload)
    print("synth capture: {} frames -> {}".format(len(frames), out))
    return 0


sys.exit(main())
