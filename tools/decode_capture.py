#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A4：.s3pcap（S3PO）→ WAV 解码器（Python/PyAV 参考实现）。

格式：[u32 magic 'S3PO'][u32 recID][u32 frameCount] + N × [u32 seq][u16 len][payload]
Opus 转码（PyAV/libopus）→ 16bit mono WAV。用法：
  python decode_capture.py <in.s3pcap> <out.wav>
"""
import struct
import sys
import wave

import av

MAGIC = 0x4F503353  # 'S3PO'


def main():
    if len(sys.argv) != 3:
        print("usage: decode_capture.py <in.s3pcap> <out.wav>")
        return 2
    data = open(sys.argv[1], "rb").read()
    magic, rec_id, frame_count = struct.unpack_from("<III", data, 0)
    if magic != MAGIC:
        print("bad magic: {:08x}".format(magic))
        return 2
    off = 12
    frames = []
    while off + 6 <= len(data):
        seq, = struct.unpack_from("<I", data, off)
        length, = struct.unpack_from("<H", data, off + 4)
        off += 6
        frames.append((seq, data[off:off + length]))
        off += length
    print("recID={} frameCount={} frames_read={}".format(rec_id, frame_count, len(frames)))

    ctx = av.CodecContext.create("libopus", "r")
    pcm = bytearray()
    total_samples = 0
    for seq, pkt in frames:
        try:
            for frame in ctx.decode(av.Packet(pkt)):
                if frame.samples:
                    arr = frame.to_ndarray()
                    if frame.layout.name == "stereo":
                        arr = arr.reshape(-1, 2)[:, 0:1]  # 交错双声道 → 取左声道
                    pcm.extend(arr.tobytes())
                    total_samples += arr.size
        except Exception as e:  # noqa: BLE001
            print("[WARN] decode frame {} failed: {!r}".format(seq, e))
    with wave.open(sys.argv[2], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(48000)  # libopus 解码固定 48kHz 输出
        w.writeframes(bytes(pcm))
    print("decoded: {} frames -> {} samples (48kHz) = {:.2f}s -> {}".format(
        len(frames), total_samples, total_samples / 48000.0, sys.argv[2]))
    return 0


sys.exit(main())
