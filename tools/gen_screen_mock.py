#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""屏幕模拟器（M4/S4.3 评审用）：按 135x240 复现固件 display.c 的三页渲染，放大输出 PNG。
用法：python gen_screen_mock.py <font8x8_basic.h> <out.png>
例：python gen_screen_mock.py firmware/sticks3/main/font8x8_basic.h screen_mock.png
"""
import re
import sys

from PIL import Image

W, H, SCALE = 135, 240, 3
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
PURPLE = (205, 156, 255)  # C_PURPLE 0xCA9F -> #CD9CFF
YELLOW = (255, 255, 0)
BLUE = (51, 153, 255)  # #3399FF
RED = (255, 0, 0)


def load_font(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    glyphs = []
    for m in re.finditer(r"\{\s*((?:0x[0-9A-Fa-f]{1,2}\s*,\s*){7}0x[0-9A-Fa-f]{1,2})\s*\}", txt):
        glyphs.append([int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{1,2}", m.group(1))])
    return glyphs


def draw_char(px, g, x, y, ch, color, scale):
    o = ord(ch)
    if o >= 128 or o >= len(g):
        return
    rows = g[o]
    for row in range(8):
        bits = rows[row]
        for col in range(8):
            if bits & (1 << col):
                for sy in range(scale):
                    py = y + row * scale + sy
                    for sx in range(scale):
                        pxx = x + col * scale + sx
                        if 0 <= pxx < W and 0 <= py < H:
                            px[pxx, py] = color


def draw_str(px, g, x, y, s, color, scale):
    for ch in s:
        draw_char(px, g, x, y, ch, color, scale)
        x += 8 * scale


def draw_str_px(px, g, x, y, s, color, size):
    """按目标像素高 size 渲染（最近邻放缩 8x8 字形，支持非 8 倍数字号）。"""
    for ch in s:
        o = ord(ch)
        if o < 128 and o < len(g):
            for row in range(size):
                bits = g[o][row * 8 // size]
                for col in range(size):
                    if bits & (1 << (col * 8 // size)):
                        xx, yy = x + col, y + row
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = color
        x += size


def draw_title(px, g, t):
    n = len(t)
    glyph = 16
    span = W - 4 - n * glyph
    gap = span // (n - 1) if n > 1 and span > 0 else 0
    x = 2
    for ch in t:
        draw_char(px, g, x, 2, ch, PURPLE, 2)
        x += glyph + gap


def page(px, g, idx):
    if idx == 0:
        draw_title(px, g, "STATUS")
        draw_str(px, g, 4, 44, "BLE:", WHITE, 2)
        draw_str(px, g, 4 + 4 * 16, 48, "CONN", YELLOW, 1)
        draw_str(px, g, 4, 74, "HOST:", WHITE, 2)
        draw_str(px, g, 4 + 5 * 16, 78, "OK", YELLOW, 1)
        draw_str_px(px, g, (W - 4 * 23) // 2, 124, "BUSY", BLUE, 23)
    elif idx == 1:
        draw_title(px, g, "TASK")
        draw_str(px, g, 4, 44, "esp32-s3", WHITE, 2)
        draw_str(px, g, 4, 74, "sticks3", WHITE, 2)
        draw_str(px, g, 4, 108, "T2 D6/6", WHITE, 2)
    else:
        draw_title(px, g, "USAGE")
        draw_str(px, g, 4, 40, "IN:", WHITE, 2)
        draw_str(px, g, 16, 62, "123456", YELLOW, 1)
        draw_str(px, g, 4, 82, "OUT:", WHITE, 2)
        draw_str(px, g, 16, 104, "2345", YELLOW, 1)
        draw_str(px, g, 4, 124, "COST:", WHITE, 2)
        draw_str(px, g, 16, 146, "1234", YELLOW, 1)
    draw_str(px, g, W - 24, H - 12, "%d/3" % (idx + 1), WHITE, 1)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    g = load_font(sys.argv[1])
    pad = 12
    out = Image.new("RGB", (3 * W * SCALE + 4 * pad, H * SCALE + 2 * pad), (40, 40, 40))
    for i in range(3):
        p = Image.new("RGB", (W, H), BLACK)
        page(p.load(), g, i)
        big = p.resize((W * SCALE, H * SCALE), Image.NEAREST)
        out.paste(big, (pad + i * (W * SCALE + pad), pad))
    out.save(sys.argv[2])
    print("saved %s | glyphs=%d | %dx%d" % (sys.argv[2], len(g), out.width, out.height))
    return 0


if __name__ == "__main__":
    sys.exit(main())
