"""Builds the XMB icon (ICON0.PNG, 320x176) from the user's own oot.o2r.

The packager ships no game artwork: the title-screen logo is rebuilt from the
archive's object_mag textures the same way En_Mag draws it (z_en_mag.c): the
160x160 RGBA32 logo, then the two I8 lines of title text (72x8 and 96x8)
twice - a dark shadow 1 px down/right, then the gold text on top. The
textures are found by format and size. Pure Python (zlib only) so the .exe
stays small.
"""

import struct
import zlib

ICON_W, ICON_H = 320, 176
OTR_HEADER = 64
TEX_RGBA32, TEX_I8 = 1, 6  # LUS Fast::TextureType
RESOURCE_TEXTURE = 0x4F544558  # "OTEX"
TITLE_OBJECT = "objects/object_mag/"

# The title-screen pieces, by (texture type, width, height): each is unique in object_mag.
LOGO = (TEX_RGBA32, 160, 160)
TEXT_LINE_1 = (TEX_I8, 72, 8)
TEXT_LINE_2 = (TEX_I8, 96, 8)


def find_texture(archive, shape):
    """Name of the title-screen texture with this (type, width, height)."""
    for name in sorted(archive.namelist()):
        if not name.startswith(TITLE_OBJECT):
            continue
        with archive.open(name) as handle:
            head = handle.read(OTR_HEADER + 16)
        if len(head) < OTR_HEADER + 16 or struct.unpack_from("<I", head, 4)[0] != RESOURCE_TEXTURE:
            continue
        if struct.unpack_from("<III", head, OTR_HEADER) == shape:
            return name
    raise KeyError("title-screen texture %dx%d not found" % (shape[1], shape[2]))


def read_texture(archive, shape):
    """Returns (width, height, pixels) with pixels as a flat list of RGBA tuples."""
    want_type = shape[0]
    name = find_texture(archive, shape)
    data = archive.read(name)
    tex_type, width, height, size = struct.unpack_from("<IIII", data, OTR_HEADER)
    body = data[OTR_HEADER + 16:OTR_HEADER + 16 + size]
    if tex_type != want_type or width * height * (4 if tex_type == TEX_RGBA32 else 1) != len(body):
        raise ValueError("%s: unexpected texture type %d %dx%d" % (name, tex_type, width, height))
    if tex_type == TEX_RGBA32:
        pixels = [tuple(body[i:i + 4]) for i in range(0, len(body), 4)]
    else:
        pixels = [(v, v, v, v) for v in body]
    return width, height, pixels


def draw_i8(canvas, cw, tex, left, top, prim, env):
    """En_Mag's I8 text: colour = lerp(env, prim, texel), alpha = texel (XLU blend)."""
    width, height, pixels = tex
    for y in range(height):
        for x in range(width):
            t = pixels[y * width + x][0]
            if t == 0:
                continue
            f = t / 255.0
            src = [env[c] + (prim[c] - env[c]) * f for c in range(3)]
            blend(canvas, cw, left + x, top + y, src, f)


def blend(canvas, cw, x, y, rgb, a):
    if x < 0 or y < 0 or x >= cw or y * cw + x >= len(canvas):
        return
    dr, dg, db, da = canvas[y * cw + x]
    out_a = a + da * (1.0 - a)
    if out_a <= 0:
        return
    mix = [(rgb[c] * a + d * da * (1.0 - a)) / out_a for c, d in enumerate((dr, dg, db))]
    canvas[y * cw + x] = (mix[0], mix[1], mix[2], out_a)


def compose_logo(archive):
    """The title logo at its native 160x160, as (r, g, b, a) floats (a in 0..1)."""
    w, h, logo = read_texture(archive, LOGO)
    canvas = [(p[0], p[1], p[2], p[3] / 255.0) for p in logo]
    line1 = read_texture(archive, TEXT_LINE_1)
    line2 = read_texture(archive, TEXT_LINE_2)
    # Screen positions from z_en_mag.c minus the logo's top-left (80, 20).
    shadow_prim, shadow_env = (0, 0, 0), (100, 0, 100)
    draw_i8(canvas, w, line1, 154 - 80, 73 - 20, shadow_prim, shadow_env)
    draw_i8(canvas, w, line2, 152 - 80, 127 - 20, shadow_prim, shadow_env)
    text_prim, text_env = (200, 200, 150), (100, 100, 50)
    draw_i8(canvas, w, line1, 153 - 80, 72 - 20, text_prim, text_env)
    draw_i8(canvas, w, line2, 151 - 80, 126 - 20, text_prim, text_env)
    return w, h, canvas


def resample(src, sw, sh, dw, dh):
    """Bilinear scale with premultiplied alpha (no dark fringes at the edges)."""
    pre = [(r * a, g * a, b * a, a) for r, g, b, a in src]
    out = []
    for y in range(dh):
        fy = min(max((y + 0.5) * sh / dh - 0.5, 0.0), sh - 1.0)
        y0 = int(fy)
        y1 = min(y0 + 1, sh - 1)
        ty = fy - y0
        for x in range(dw):
            fx = min(max((x + 0.5) * sw / dw - 0.5, 0.0), sw - 1.0)
            x0 = int(fx)
            x1 = min(x0 + 1, sw - 1)
            tx = fx - x0
            p00, p01 = pre[y0 * sw + x0], pre[y0 * sw + x1]
            p10, p11 = pre[y1 * sw + x0], pre[y1 * sw + x1]
            v = [(p00[c] * (1 - tx) + p01[c] * tx) * (1 - ty) + (p10[c] * (1 - tx) + p11[c] * tx) * ty
                 for c in range(4)]
            a = v[3]
            out.append((v[0] / a, v[1] / a, v[2] / a, a) if a > 0 else (0, 0, 0, 0))
    return out


def encode_png(width, height, pixels):
    """RGBA8 PNG from (r, g, b, a) floats."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for r, g, b, a in pixels[y * width:(y + 1) * width]:
            raw += bytes((min(255, max(0, int(r + 0.5))), min(255, max(0, int(g + 0.5))),
                          min(255, max(0, int(b + 0.5))), min(255, max(0, int(a * 255 + 0.5)))))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def build_icon0(archive):
    """ICON0.PNG bytes: the logo scaled to the icon height, centred on a transparent 320x176."""
    w, h, logo = compose_logo(archive)
    size = ICON_H
    scaled = resample(logo, w, h, size, size)
    left = (ICON_W - size) // 2
    canvas = [(0, 0, 0, 0)] * (ICON_W * ICON_H)
    for y in range(size):
        canvas[y * ICON_W + left:y * ICON_W + left + size] = scaled[y * size:(y + 1) * size]
    return encode_png(ICON_W, ICON_H, canvas)


if __name__ == "__main__":
    import sys
    import zipfile

    if len(sys.argv) != 3:
        sys.exit("usage: xmb_icon.py <oot.o2r> <out ICON0.PNG>")
    with zipfile.ZipFile(sys.argv[1]) as archive, open(sys.argv[2], "wb") as out:
        out.write(build_icon0(archive))
