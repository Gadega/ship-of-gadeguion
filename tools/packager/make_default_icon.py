"""Draws the built-in XMB icon (ICON0.PNG, 320x176) used before the player's oot.o2r is
installed: plain text in the public-domain 8x8 font, no game artwork. The game replaces it
with the title logo from the player's own archive after the first-boot setup.

usage: make_default_icon.py <font8x8_basic.h> <out ICON0.PNG>"""

import re
import struct
import sys
import zlib

W, H = 320, 176


def load_font(path):
    text = open(path).read()
    rows = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}", text)
    return [[int(v, 16) for v in row.split(",")] for row in rows[:128]]


def main():
    font = load_font(sys.argv[1])
    px = [[(0, 0, 0, 0)] * W for _ in range(H)]

    def rect(x0, y0, w, h, c):
        for y in range(max(0, y0), min(H, y0 + h)):
            for x in range(max(0, x0), min(W, x0 + w)):
                px[y][x] = c

    def text(s, y, scale, c, shadow=None):
        x0 = (W - len(s) * 8 * scale) // 2
        for pass_c, off in ((shadow, 2), (c, 0)) if shadow else ((c, 0),):
            for i, ch in enumerate(s):
                glyph = font[ord(ch) & 0x7F]
                for gy in range(8):
                    for gx in range(8):
                        if glyph[gy] & (1 << gx):
                            rect(x0 + (i * 8 + gx) * scale + off, y + gy * scale + off, scale, scale, pass_c)

    # rounded dark-blue card with a vertical gradient
    for y in range(8, H - 8):
        for x in range(8, W - 8):
            dx = max(0, 20 - x, x - (W - 21))
            dy = max(0, 20 - y, y - (H - 21))
            if dx * dx + dy * dy <= 144:
                t = (y - 8) / (H - 16)
                px[y][x] = (int(10 + 20 * t), int(20 + 40 * t), int(55 + 70 * t), 255)
    text("Ship of", 30, 4, (150, 215, 255, 255), (5, 8, 25, 255))
    text("Gadeguion", 74, 4, (150, 215, 255, 255), (5, 8, 25, 255))
    text("PS3 native port", 128, 2, (235, 235, 240, 255))

    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in px)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) +
                                  chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
