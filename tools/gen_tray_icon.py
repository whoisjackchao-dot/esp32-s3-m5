#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""托盘图标生成器：低像素 ESP32-S3 开发板。
产出：① host/src/tray_icon.h（32x32 BGRA uint32 数组，供 tray.cpp CreateIconIndirect）
      ② tray_icon_preview.png（放大预览，仅用于目视核对，gitignored）
用法（Windows python 有 PIL）：python tools/gen_tray_icon.py
"""
import os
import sys

from PIL import Image

# 16x16 像素稿（×2 放大为 32x32，兼顾 100%/200% DPI 清晰度）
ART = [
    "................",
    "...A.A.A.A.A....",
    "...AAAAAAAAAA...",
    "..GGKKKKKKKKGG..",
    ".GGKKKKKKKKKKGG.",
    "AGGGKKKKKKKKGGGA",
    "AGGGKKKKKKKKGGGA",
    "AGGGKKKKKKKKGGGA",
    "AGGGKKKKKKKKGGGA",
    "AGGGKKKKKKKKGGGA",
    "AGGGKKKKKKKKGGGA",
    ".GGKKKKKKKKKKGG.",
    "..GGGGGGGGGGGG..",
    "...GGGGGGGGGG...",
    "....SSSSSSSS....",
    ".....SSSSSS.....",
]

COLORS = {
    ".": (0, 0, 0, 0),          # 透明
    "G": (30, 122, 60, 255),   # PCB 绿
    "K": (26, 26, 30, 255),    # 模块黑
    "A": (212, 175, 55, 255),  # 金色（天线/引脚）
    "S": (180, 184, 190, 255),  # 银色 USB
    "W": (235, 235, 235, 255),  # 白丝印
}

N = 16
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_rgba():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(ART):
        for x, ch in enumerate(row):
            px[x, y] = COLORS[ch]
    return img


def emit_header(img32):
    px = img32.load()
    lines = []
    lines.append("#pragma once")
    lines.append("// 自动生成 by tools/gen_tray_icon.py —— 勿手改。32x32 BGRA(0xAARRGGBB)。")
    lines.append("// 低像素 ESP32-S3 开发板：绿 PCB + 黑模块 + 金色天线/引脚 + 银色 USB。")
    lines.append("#include <cstdint>")
    lines.append("static const uint32_t TRAY_ICON_32[32 * 32] = {")
    for y in range(32):
        vals = []
        for x in range(32):
            r, g, b, a = px[x, y]
            vals.append("0x%02X%02X%02X%02Xu" % (a, r, g, b))  # 0xAARRGGBB
        lines.append("    " + ", ".join(vals) + ",")
    lines.append("};")
    lines.append("")
    out = os.path.join(ROOT, "host", "src", "tray_icon.h")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return out


def main():
    img = build_rgba()
    img32 = img.resize((32, 32), Image.NEAREST)
    out = emit_header(img32)
    preview = os.path.join(ROOT, "tray_icon_preview.png")
    img32.resize((320, 320), Image.NEAREST).save(preview)
    print("wrote %s" % out)
    print("preview %s" % preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
