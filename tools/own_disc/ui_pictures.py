#!/usr/bin/env python3
"""The launcher's pictures, from the player's own disc (builder ui stage, 1.2.0): the controller glyphs the prompt chip draws,
the backdrop art and the three landing tiles.

Input: Xbox 360 texture dictionaries (.xtd resources) as the disc's archives store them. Their payload carries a 16-byte prefix
before its XMem frames; the decoded image splits into the system and graphics segments (sizes from the resource's flag words).
Each texture object is (name pointer, 0x00013000, ..., fetch-header pointer, w << 16 | h), so names pair with the Xenos fetch
headers by pointer. Tiled DXT1/3/5 and 8888 are decoded.

  glyphs:   gfxfontlib_360.xtd button_30..39 -> glyph-<prompt>.png (RGBA, the texture's own 64 x 64)
  art:      loadingscreen1.xtd 'background' -> menu-art.png;  0 / 6 / 5 -> tile-story / tile-sandbox / tile-online.png
            The 360's loading screens draw a 1024 x 512 background (a tilted panel on black) stretched to the 16:9 screen; the
            largest axis-aligned 2:1 box wholly inside the panel (2 px in) is cut and written at 16:9 (the game's own vertical
            stretch), sharp: no blur, no shade.

Determinism: integer arithmetic, IEEE + - * / on doubles (no libm) and Python's round (half to even). PNGs are written with
deflate *stored* blocks of a fixed size, so no zlib version can change a byte: the same disc gives the same files on every
host. Nothing of the game is in this file. Made from the menu revamp's ui_glyphs.py and ui_art.py and thor-builder's
xtd_decode.py (2026-11-06/07, our own code); the art crop is ui_art.py's numpy arithmetic written out operation by operation.
"""
import struct
import zlib

# prompt -> the disc texture (identified on the decoded pictures: menu-revamp M3-NOTES.md)
GLYPHS = {'a': 'button_30', 'b': 'button_31', 'x': 'button_32', 'y': 'button_33', 'lb': 'button_34', 'rb': 'button_36',
          'lt': 'button_35', 'rt': 'button_37', 'start': 'button_38', 'back': 'button_39'}
# output -> the loading screen whose 'background' it is cut from (menu-revamp M5/M6-NOTES.md)
ART = {'menu-art.png': 'loadingscreen1', 'tile-story.png': 'loadingscreen0', 'tile-sandbox.png': 'loadingscreen6',
       'tile-online.png': 'loadingscreen5'}
ART_TEXTURE = 'background'
FMT = {0x12: ('DXT1', 8), 0x13: ('DXT3', 16), 0x14: ('DXT5', 16), 0x06: ('8888', 4)}


# ---- texture dictionaries ----
def tiled_offset(x, y, width, bpp):          # XGAddress2DTiledOffset, in elements
    aw = (width + 31) & ~31
    log = (bpp >> 2) + ((bpp >> 1) >> (bpp >> 2))
    macro = ((x >> 5) + (y >> 5) * (aw >> 5)) << (log + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log)) + ((y & 1) << 4)
    return (((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log


def swap16(b):
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


def swap32(b):
    a = bytearray(b)
    a[0::4], a[1::4], a[2::4], a[3::4] = b[3::4], b[2::4], b[1::4], b[0::4]
    return bytes(a)


def c565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def dxt_color(blk, opaque):
    c0, c1 = struct.unpack('<HH', blk[:4])
    bits = struct.unpack('<I', blk[4:8])[0]
    p0, p1 = c565(c0), c565(c1)
    if c0 > c1 or opaque:
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1)), (0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def dxt5_alpha(blk):
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], 'little')
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def textures(sysm):
    """The Xenos fetch headers in a system segment (D3DBaseTexture pattern)."""
    out = []
    for o in range(0, len(sysm) - 0x30, 4):
        if sysm[o:o + 4] == b'\x00\x20\x00\x03' and sysm[o + 0x14:o + 0x18] == b'\xff\xff\x00\x00':
            w = struct.unpack('>6I', sysm[o + 0x1C:o + 0x34])
            out.append(dict(at=o, tiled=w[0] >> 31, pitch=((w[0] >> 22) & 0x1FF) * 32, fmt=w[1] & 0x3F, endian=(w[1] >> 6) & 3,
                            base=w[1] & 0xFFFFF000, width=(w[2] & 0x1FFF) + 1, height=((w[2] >> 13) & 0x1FFF) + 1))
    return out


def names(sysm, headers):
    """{texture name: index into headers}, paired through the texture objects' fetch-header pointers."""
    by_at = {t['at']: i for i, t in enumerate(headers)}
    out = {}
    for k in range(0, len(sysm) - 32, 4):
        n, tag = struct.unpack_from('>II', sysm, k)
        if n >> 24 == 0x50 and tag == 0x00013000:
            hp, _wh = struct.unpack_from('>II', sysm, k + 20)
            off = n & 0xFFFFFF
            if off >= len(sysm) or (hp & 0xFFFFFF) not in by_at:
                continue
            out[sysm[off:sysm.index(b'\0', off)].decode('latin1')] = by_at[hp & 0xFFFFFF]
    return out


def decode(t, gfx):
    """(width, height, RGBA bytes) of one texture."""
    name, bpb = FMT[t['fmt']]
    blockdim = 1 if name == '8888' else 4
    bw, bh = (max(t['pitch'], t['width']) + blockdim - 1) // blockdim, (t['height'] + blockdim - 1) // blockdim
    bh_al = (bh + 31) & ~31
    base = t['base'] - 0x60000000 if t['base'] >= 0x60000000 else t['base']
    data = gfx[base: base + ((bw + 31) & ~31) * bh_al * bpb]
    data = swap16(data) if t['endian'] == 1 else swap32(data) if t['endian'] == 2 else data
    W, H = t['width'], t['height']
    px = bytearray(W * H * 4)
    for by in range(bh):
        for bx in range((W + blockdim - 1) // blockdim):
            off = (tiled_offset(bx, by, bw, bpb) if t['tiled'] else by * bw + bx) * bpb
            blk = data[off: off + bpb]
            if len(blk) < bpb:
                continue
            if name == '8888':
                cols, alph = [(blk[2], blk[1], blk[0])], [blk[3]]
            elif name == 'DXT1':
                cols, alph = dxt_color(blk, False), [255] * 16
            elif name == 'DXT3':
                cols = dxt_color(blk[8:], True)
                alph = [((int.from_bytes(blk[:8], 'little') >> (4 * i)) & 15) * 17 for i in range(16)]
            else:
                cols, alph = dxt_color(blk[8:], True), dxt5_alpha(blk[:8])
            for i, (c, a) in enumerate(zip(cols, alph)):
                x, y = bx * blockdim + (i % blockdim), by * blockdim + (i // blockdim)
                if x < W and y < H:
                    px[(y * W + x) * 4:(y * W + x) * 4 + 4] = bytes((c[0], c[1], c[2], a))
    return W, H, bytes(px)


def texture(sysm, gfx, name, where):
    heads = textures(sysm)
    table = names(sysm, heads)
    if name not in table:
        raise ValueError('%s: no texture named %s' % (where, name))
    t = heads[table[name]]
    if t['fmt'] not in FMT:
        raise ValueError('%s: %s has format %#x, not decoded here' % (where, name, t['fmt']))
    return decode(t, gfx)


# ---- PNG with stored deflate blocks ----
STORED = 65535


def png(width, height, channels, pixels):
    """A PNG (8-bit RGB or RGBA, filter 0) whose zlib stream is stored blocks of STORED bytes: identical on every host."""
    stride = width * channels
    if len(pixels) != stride * height or channels not in (3, 4):
        raise ValueError('picture size mismatch')
    raw = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
    z = bytearray(b'\x78\x01')
    for i in range(0, len(raw), STORED):
        part = raw[i:i + STORED]
        z += bytes([1 if i + STORED >= len(raw) else 0]) + struct.pack('<HH', len(part), len(part) ^ 0xFFFF) + part
    z += struct.pack('>I', zlib.adler32(raw) & 0xFFFFFFFF)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', bytes(z)) + chunk(b'IEND', b'')


# ---- the backdrop / tile cut (ui_art.py, written out without numpy) ----
def panel_mask(W, H, rgba):
    """Panel cells: a pixel is lit when its RGB mean is above 14 (sum > 42); per row and per column everything between the first
    and last lit pixel counts (rows / columns with more than 8 lit pixels), and a cell is panel when both its row span and its
    column span hold it."""
    lit = [[rgba[(y * W + x) * 4] + rgba[(y * W + x) * 4 + 1] + rgba[(y * W + x) * 4 + 2] > 42 for x in range(W)] for y in range(H)]
    rows = [[False] * W for _ in range(H)]
    for y in range(H):
        xs = [x for x in range(W) if lit[y][x]]
        if len(xs) > 8:
            for x in range(xs[0], xs[-1] + 1):
                rows[y][x] = True
    mask = [[False] * W for _ in range(H)]
    for x in range(W):
        ys = [y for y in range(H) if lit[y][x]]
        if len(ys) > 8:
            for y in range(ys[0], ys[-1] + 1):
                mask[y][x] = rows[y][x]
    return mask


def largest_2to1(mask, inset=2):
    """(y, x, h, w), w = 2h: the largest box of panel cells, searched from the biggest down, rows then even columns (as
    ui_art.py). The inset shrinks the mask by its four neighbours, wrapping at the edges as numpy.roll does."""
    H, W = len(mask), len(mask[0])
    m = [row[:] for row in mask]
    for _ in range(inset):
        m = [[m[y][x] and m[y - 1][x] and m[(y + 1) % H][x] and m[y][x - 1] and m[y][(x + 1) % W] for x in range(W)]
             for y in range(H)]
    cs = [[0] * (W + 1) for _ in range(H + 1)]          # cs[y + 1][x + 1] = cells in [0..y] x [0..x]
    for y in range(H):
        run = 0
        for x in range(W):
            run += 1 if m[y][x] else 0
            cs[y + 1][x + 1] = cs[y][x + 1] + run
    for h in range(H, 0, -1):
        w = 2 * h
        if w > W:
            continue
        full = h * w
        for y in range(0, H - h + 1):
            top, bottom = cs[y], cs[y + h]
            for x in range(0, W - w + 1, 2):
                if bottom[x + w] - top[x + w] - bottom[x] + top[x] == full:
                    return y, x, h, w
    raise ValueError('no 2:1 box inside the panel')


def art(W, H, rgba):
    """(width, height, RGB bytes) of the 16:9 picture cut from a loading-screen background."""
    y, x, h, w = largest_2to1(panel_mask(W, H, rgba))
    outw, outh = w, int(round(w * 9 / 16))
    out = bytearray(outw * outh * 3)
    for i in range(outh):
        ys = (i + 0.5) * h / outh - 0.5
        fl = ys // 1.0                                   # floor, as a float (numpy.floor)
        f = ys - fl
        y0 = min(max(int(fl), 0), h - 1)
        y1 = min(max(y0 + 1, 0), h - 1)
        r0 = ((y + y0) * W + x) * 4
        r1 = ((y + y1) * W + x) * 4
        g = 1 - f
        o = i * outw * 3
        for c in range(outw):
            for k in range(3):
                v = round(rgba[r0 + 4 * c + k] * g + rgba[r1 + 4 * c + k] * f)
                out[o + 3 * c + k] = 0 if v < 0 else 255 if v > 255 else v
    return outw, outh, bytes(out)
