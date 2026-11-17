#!/usr/bin/env python3
"""The game's menu fonts, from the player's own disc, as TrueType files the launcher loads (builder ui stage, 1.2.0).

Input: font_lib_efigs_360.gfx as the disc's archive stores it, decoded (a Scaleform movie; tag 1005, DefineCompactedFont).
Output: chalet-london.ttf, chalet-comprime.ttf and pricedown.ttf (the faces FACES names). The outlines are quadratic in the
movie and are written as they are (no approximation); font units are the movie's own integers (EM 256), y flipped
(gfx y-down, TrueType y-up). Scaleform fills even-odd and TrueType renderers non-zero, so each contour is written clockwise
at an even nesting depth and counter-clockwise at an odd one. The font's own kerning pairs become a 'kern' table.

Integer arithmetic and struct packing, plus IEEE + - * / on doubles for the contour direction (no libm), so the same disc gives
the same bytes on every host. Nothing of the game is in this file. Made from the menu revamp's swf.py, gfxfont.py and gfx2ttf.py (2026-11-07, our own code), logic unchanged.

Movie layout read here (little endian): tag 1005 = UI16 font id, zero-terminated name, UI16 flags, UI16 nominal size (EM),
SI16 ascent, SI16 descent, SI16 leading, UI32 glyph count, UI32 glyph byte length, glyph bytes, then per glyph UI16 code,
SI16 advance, UI32 offset (from the byte after the font id), then a UI30 kerning pair count and pairs (UI16, UI16, SI16).
Glyph: 4 x SI15 bounds, UI15 contour count; contour: SI15 x, SI15 y, UI30 edge word (bit 0: a contour stored elsewhere at
word >> 1, else word >> 1 edges), then packed edges whose low nibble is the edge type (H12 H20 V12 V20 L6 L10 L14 L18 C5..C19).
"""
import struct
import zlib


class Bits:
    def __init__(self, data, pos=0):
        self.d, self.p, self.b = data, pos * 8, 0

    def align(self):
        self.p = (self.p + 7) & ~7

    @property
    def byte(self):
        self.align(); return self.p >> 3

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1); self.p += 1
        return v

    def s(self, n):
        if n == 0: return 0
        v = self.u(n)
        return v - (1 << n) if v & (1 << (n - 1)) else v


def rect(data, pos):
    b = Bits(data, pos); n = b.u(5)
    r = [b.s(n) for _ in range(4)]
    return r, b.byte


def header(data):
    sig = data[:3]
    if sig in (b'CWS', b'CFX'): data = data[:8] + zlib.decompress(data[8:])
    r, p = rect(data, 8)
    rate, count = struct.unpack_from('<HH', data, p)
    return data, r, rate / 256, p + 4


def tags(data, pos, end=None):
    end = len(data) if end is None else end
    while pos < end:
        code_len, = struct.unpack_from('<H', data, pos); pos += 2
        code, ln = code_len >> 6, code_len & 63
        if ln == 63: ln, = struct.unpack_from('<I', data, pos); pos += 4
        yield code, pos, ln
        pos += ln
        if code == 0: break


def cstr(data, pos):
    e = data.index(b'\0', pos); return data[pos:e].decode('latin-1'), e + 1



def walk(data, pos, end=None, depth=0, out=None):
    out = [] if out is None else out
    for code, p, ln in tags(data, pos, end):
        out.append((depth, code, p, ln))
        if code == 39:
            walk(data, p + 4, p + ln, depth + 1, out)
    return out



EDGE = {0: ('H', 12), 1: ('H', 20), 2: ('V', 12), 3: ('V', 20), 4: ('L', 6), 5: ('L', 10), 6: ('L', 14), 7: ('L', 18)}
for i, n in enumerate(range(5, 21, 2)): EDGE[8 + i] = ('C', n)


def sx(v, n): return v - (1 << n) if v & (1 << (n - 1)) else v


class R:
    def __init__(self, d, p): self.d, self.p = d, p

    def u8(self): v = self.d[self.p]; self.p += 1; return v

    def ui15(self):
        b = self.u8()
        return (b >> 1) | (self.u8() << 7) if b & 1 else b >> 1

    def si15(self):
        b = self.u8()
        if b & 1: return sx((b >> 1) | (self.u8() << 7), 15)
        return sx(b >> 1, 7)

    def ui30(self):
        b = self.u8(); n = b & 3; v = b >> 2
        for i in range(n): v |= self.u8() << (6 + 8 * i)
        return v


def read_glyph(gdata, off, edge_mode='control'):
    r = R(gdata, off)
    bounds = [r.si15() for _ in range(4)]
    nc = r.ui15()
    contours = []
    for _ in range(nc):
        x, y = r.si15(), r.si15()
        w = r.ui30()
        if w & 1:   # shared contour stored elsewhere
            sub = R(gdata, w >> 1); ne = sub.ui30() >> 1; rr = sub
        else:
            ne = w >> 1; rr = r
        pts = [(x, y)]
        for _ in range(ne):
            b0 = rr.d[rr.p]; kind, n = EDGE[b0 & 15]
            nvals = {'H': 1, 'V': 1, 'L': 2, 'C': 4}[kind]
            nbytes = (4 + n * nvals + 7) // 8
            raw = int.from_bytes(rr.d[rr.p:rr.p + nbytes], 'little'); rr.p += nbytes
            vals = [sx((raw >> (4 + n * i)) & ((1 << n) - 1), n) for i in range(nvals)]
            if kind == 'H': x += vals[0]; pts.append(('L', x, y))
            elif kind == 'V': y += vals[0]; pts.append(('L', x, y))
            elif kind == 'L': x += vals[0]; y += vals[1]; pts.append(('L', x, y))
            else:
                if edge_mode == 'control':   # anchor delta relative to the control point (SWF style)
                    cx, cy = x + vals[0], y + vals[1]; x, y = cx + vals[2], cy + vals[3]
                else:                        # both deltas relative to the current point
                    cx, cy = x + vals[0], y + vals[1]; x, y = x + vals[2], y + vals[3]
                pts.append(('Q', cx, cy, x, y))
        contours.append(pts)
    return bounds, contours


def read_fonts(movie, edge_mode='control'):
    data, _, _, p = header(movie)
    exports = {}
    fonts = {}
    for d, c, q, ln in walk(data, p):
        if c == 56:
            n, = struct.unpack_from('<H', data, q); pp = q + 2
            for _ in range(n):
                cid, = struct.unpack_from('<H', data, pp); name, pp = cstr(data, pp + 2); exports[cid] = name
        if c != 1005: continue
        fid, = struct.unpack_from('<H', data, q)
        name, h = cstr(data, q + 2)
        flags, nom, asc, des, lead, ng, gb = struct.unpack_from('<HHhhhII', data, h)
        g0 = h + 18; gi = g0 + gb
        base = q + 2; gdata = data[base:gi]      # glyph offsets count from the byte after the font id
        glyphs = {}
        for i in range(ng):
            code, adv, off = struct.unpack_from('<HhI', data, gi + 8 * i)
            glyphs[code] = dict(advance=adv, offset=off)
        r = R(data, gi + 8 * ng); nk = r.ui30(); kern = {}
        for _ in range(nk):
            a, b, k = struct.unpack_from('<HHh', data, r.p); r.p += 6; kern[(a, b)] = k
        for code, g in glyphs.items():
            g['bounds'], g['contours'] = read_glyph(gdata, g['offset'], edge_mode)
        fonts[fid] = dict(id=fid, name=name, flags=flags, em=nom, ascent=asc, descent=des, leading=lead, glyphs=glyphs, kern=kern)
    for fid, f in fonts.items(): f['export'] = exports.get(fid)
    return fonts

# export name in the movie -> output file (the faces the menus use; others are not written)
FACES = {'$Font2': 'chalet-london.ttf', '$Font2_cond': 'chalet-comprime.ttf', '$gtaCash': 'pricedown.ttf'}


def points_of(contour):
    """gfx contour -> list of (x, y, on_curve) in font units, y-down, without a repeated closing point."""
    x0, y0 = contour[0]; pts = [(x0, y0, True)]
    for seg in contour[1:]:
        if seg[0] == 'L': pts.append((seg[1], seg[2], True))
        else: pts.append((seg[1], seg[2], False)); pts.append((seg[3], seg[4], True))
    if len(pts) > 1 and pts[-1][2] and pts[-1][:2] == pts[0][:2]: pts.pop()
    return pts


def flat(pts):
    """Polygon through on-curve points and the curve midpoints (enough for area and containment)."""
    out = []
    n = len(pts)
    for i, p in enumerate(pts):
        if p[2]: out.append(p[:2])
        else:
            a = pts[i - 1]; b = pts[(i + 1) % n]
            out.append((0.25 * a[0] + 0.5 * p[0] + 0.25 * b[0], 0.25 * a[1] + 0.5 * p[1] + 0.25 * b[1]))
    return out


def area(poly):
    return sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly))) / 2.0


def inside(pt, poly):
    x, y = pt; c = False; n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1): c = not c
    return c


def oriented(contours):
    """TrueType contours (y-up ints): clockwise at even nesting depth, counter-clockwise at odd depth."""
    cs = [points_of(c) for c in contours]
    cs = [[(x, -y, on) for x, y, on in c] for c in cs if len(c) >= 2]
    polys = [flat(c) for c in cs]
    out = []
    for i, c in enumerate(cs):
        probe = polys[i][0]
        depth = sum(1 for j, q in enumerate(polys) if j != i and abs(area(q)) > abs(area(polys[i])) and inside(probe, q))
        clockwise = area(polys[i]) < 0      # y-up: negative shoelace area = clockwise
        if clockwise != (depth % 2 == 0): c = [c[0]] + c[1:][::-1]
        out.append(c)
    return out


def glyf_entry(contours):
    if not contours: return b'', (0, 0, 0, 0), 0, 0
    xs = [p[0] for c in contours for p in c]; ys = [p[1] for c in contours for p in c]
    bbox = (min(xs), min(ys), max(xs), max(ys))
    ends, flags, dx, dy = [], bytearray(), [], []
    n = 0; px = py = 0
    for c in contours:
        n += len(c); ends.append(n - 1)
        for x, y, on in c:
            flags.append(1 if on else 0); dx.append(x - px); dy.append(y - py); px, py = x, y
    b = struct.pack('>hhhhh', len(contours), *bbox) + b''.join(struct.pack('>H', e) for e in ends) + struct.pack('>H', 0)
    b += bytes(flags) + b''.join(struct.pack('>h', v) for v in dx) + b''.join(struct.pack('>h', v) for v in dy)
    if len(b) % 2: b += b'\0'
    return b, bbox, n, len(contours)


def cmap4(code_to_gid):
    codes = sorted(c for c in code_to_gid if c < 0xFFFF)
    segs = []
    for c in codes:
        if segs and c == segs[-1][1] + 1 and code_to_gid[c] - c == code_to_gid[segs[-1][0]] - segs[-1][0]: segs[-1][1] = c
        else: segs.append([c, c])
    segs.append([0xFFFF, 0xFFFF])
    n = len(segs); ends = b''.join(struct.pack('>H', e) for _, e in segs); starts = b''.join(struct.pack('>H', s) for s, _ in segs)
    deltas = b''.join(struct.pack('>H', ((code_to_gid[s] - s) if s != 0xFFFF else 1) & 0xFFFF) for s, _ in segs)
    ranges = b'\0\0' * n
    es = 1; lg = 0
    while es * 2 <= n: es *= 2; lg += 1
    body = struct.pack('>HHHH', n * 2, es * 2, lg, n * 2 - es * 2) + ends + b'\0\0' + starts + deltas + ranges
    sub = struct.pack('>HHH', 4, 6 + len(body), 0) + body
    return struct.pack('>HHHHI', 0, 1, 3, 1, 12) + sub


def name_table(family, style='Regular'):
    recs = [(1, family), (2, style), (3, family + '-' + style + ' (from the player\'s disc)'), (4, family + ' ' + style),
            (5, 'Version 1.0'), (6, family.replace(' ', '') + '-' + style)]
    strings = b''; entries = b''
    for nid, s in recs:
        e = s.encode('utf-16-be'); entries += struct.pack('>HHHHHH', 3, 1, 0x409, nid, len(e), len(strings)); strings += e
    return struct.pack('>HHH', 0, len(recs), 6 + 12 * len(recs)) + entries + strings


def checksum(b):
    b = b + b'\0' * (-len(b) % 4)
    return sum(struct.unpack('>%dI' % (len(b) // 4), b)) & 0xFFFFFFFF


def build(font, family):
    em = font['em']
    codes = sorted(font['glyphs'])
    gids = {c: i + 1 for i, c in enumerate(codes)}
    glyf = bytearray(); loca = [0]; hmtx = b''
    xmin = ymin = 32767; xmax = ymax = -32768; maxp_pts = maxp_cts = 0; adv_max = 0; min_lsb = min_rsb = 32767; ext_max = -32768
    entries = [(0, b'', (0, 0, 0, 0), 0, 0)]   # .notdef: empty, advance = half EM
    for c in codes:
        g = font['glyphs'][c]
        b, bbox, npts, nct = glyf_entry(oriented(g['contours']))
        entries.append((max(g['advance'], 0), b, bbox, npts, nct))
    entries[0] = (em // 2, b'', (0, 0, 0, 0), 0, 0)
    for adv, b, bbox, npts, nct in entries:
        glyf += b; loca.append(len(glyf))
        lsb = bbox[0] if b else 0
        hmtx += struct.pack('>Hh', adv, lsb); adv_max = max(adv_max, adv)
        if b:
            xmin, ymin, xmax, ymax = min(xmin, bbox[0]), min(ymin, bbox[1]), max(xmax, bbox[2]), max(ymax, bbox[3])
            min_lsb = min(min_lsb, bbox[0]); min_rsb = min(min_rsb, adv - bbox[2]); ext_max = max(ext_max, bbox[2])
            maxp_pts = max(maxp_pts, npts); maxp_cts = max(maxp_cts, nct)
    long_loca = len(glyf) > 0x1FFFE
    loca_b = b''.join(struct.pack('>I' if long_loca else '>H', o if long_loca else o // 2) for o in loca)
    ng = len(entries)
    asc, des, lead = font['ascent'], font['descent'], font['leading']
    head = struct.pack('>IIIIHHqqhhhhHHhhh', 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, em, 0, 0, xmin, ymin, xmax, ymax, 0, 8, 2,
                       1 if long_loca else 0, 0)
    hhea = struct.pack('>IhhhHhhhhhhhhhhhH', 0x00010000, asc, -des, lead, adv_max, min_lsb, min_rsb, ext_max, 1, 0, 0, 0, 0, 0, 0, 0, ng)
    maxp = struct.pack('>IHHHHHHHHHHHHHH', 0x00010000, ng, maxp_pts, maxp_cts, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    avg = sum(e[0] for e in entries) // max(1, ng)
    os2 = struct.pack('>HhHHHhhhhhhhhhhh', 4, avg, 400, 5, 0, em // 5, em // 5, 0, em // 10, em // 5, em // 5, 0, em // 3, em // 20, em // 4, 0)
    os2 += bytes(10) + struct.pack('>IIII', 1, 0, 0, 0) + b'NONE' + struct.pack('>HHH', 0x40, min(c for c in codes if c > 0) if codes else 32,
                                                                                    min(max(codes), 0xFFFF) if codes else 32)
    os2 += struct.pack('>hhhHH', asc, -des, lead, asc, des) + struct.pack('>II', 1, 0) + struct.pack('>hhHHH', em // 2, round(asc * 0.7), 0, 32, 0)
    post = struct.pack('>IIhhIIIII', 0x00030000, 0, -em // 10, em // 20, 0, 0, 0, 0, 0)
    tables = {b'head': head, b'hhea': hhea, b'maxp': maxp, b'OS/2': os2, b'hmtx': hmtx, b'cmap': cmap4(gids), b'loca': loca_b,
              b'glyf': bytes(glyf), b'name': name_table(family), b'post': post}
    pairs = sorted((gids[a], gids[b], k) for (a, b), k in font['kern'].items() if a in gids and b in gids and k)
    if pairs:
        n = len(pairs); es = 1; lg = 0
        while es * 2 <= n: es *= 2; lg += 1
        sub = struct.pack('>HHHHHHH', 0, 14 + 6 * n, 1, n, es * 6, lg, (n - es) * 6) + b''.join(struct.pack('>HHh', *p) for p in pairs)
        tables[b'kern'] = struct.pack('>HH', 0, 1) + sub
    tags = sorted(tables)
    nt = len(tags); es = 1; lg = 0
    while es * 2 <= nt: es *= 2; lg += 1
    out = struct.pack('>IHHHH', 0x00010000, nt, es * 16, lg, nt * 16 - es * 16)
    off = 12 + 16 * nt; dir_ = b''; data = b''
    for t in tags:
        b = tables[t]; dir_ += struct.pack('>4sIII', t, checksum(b), off + len(data), len(b)); data += b + b'\0' * (-len(b) % 4)
    font_bytes = bytearray(out + dir_ + data)
    adj = (0xB1B0AFBA - checksum(bytes(font_bytes))) & 0xFFFFFFFF
    hp = font_bytes.find(b'head', 12, off); ho, = struct.unpack_from('>I', font_bytes, hp + 8)
    struct.pack_into('>I', font_bytes, ho + 8, adj)
    return bytes(font_bytes)




def ttf_files(gfx_bytes):
    """{file name: TrueType bytes} for the faces in FACES, from the decoded movie. Refuses a movie that lacks one."""
    fonts = read_fonts(gfx_bytes)
    out = {}
    for f in sorted(fonts.values(), key=lambda f: f['id']):
        name = FACES.get(f['export'])
        if name:
            out[name] = build(f, f['name'].split('-')[0])
    missing = sorted(set(FACES.values()) - set(out))
    if missing:
        raise ValueError('the font movie lacks ' + ', '.join(missing))
    return out
