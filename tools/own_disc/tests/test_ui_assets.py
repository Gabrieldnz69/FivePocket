#!/usr/bin/env python3
"""Host tests of the builder's ui stage (build_ui_assets.py, ui_pictures.py, ui_sounds.py, xma2_decode.py, ui_stage.py) on
synthetic inputs:
no disc data. The disc-derived outputs themselves are pinned by the stage's expect hashes in the private profile and proven by
an end-to-end run from the discs (docs: tools/own_disc/BUNDLE-FORMAT.md, format 2 point 4).

    python3 -m unittest tools.own_disc.tests.test_ui_assets
"""
import hashlib
import io
import json
import math
import os
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build_ui_assets  # noqa: E402
import ui_pictures  # noqa: E402
import ui_sounds as u  # noqa: E402
import ui_stage  # noqa: E402
import xma2_decode  # noqa: E402


def read_png(data):
    """A plain PNG reader (filter 0 rows only), independent of the writer."""
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    p, idat = 8, b''
    while p < len(data):
        n, = struct.unpack('>I', data[p:p + 4])
        kind, body = data[p + 4:p + 8], data[p + 8:p + 8 + n]
        assert struct.unpack('>I', data[p + 8 + n:p + 12 + n])[0] == zlib.crc32(kind + body) & 0xFFFFFFFF
        if kind == b'IHDR':
            w, h, _depth, ct = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat += body
        p += 12 + n
    ch = {2: 3, 6: 4}[ct]
    raw = zlib.decompress(idat)
    stride = w * ch
    assert all(raw[y * (stride + 1)] == 0 for y in range(h))
    return w, h, ch, b''.join(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(h)), idat


class Pictures(unittest.TestCase):
    def test_png_reads_back_and_its_zlib_stream_is_stored_blocks(self):
        # the pins hold on every host only if no zlib version can change a byte: stored blocks of the fixed size
        w, h = 300, 233                                  # 300 * 3 + 1 bytes a row: blocks split inside rows
        pixels = bytes((x * 7 + y * 13) & 0xFF for y in range(h) for x in range(w * 3))
        rw, rh, ch, back, z = read_png(ui_pictures.png(w, h, 3, pixels))
        self.assertEqual((rw, rh, ch, back), (w, h, 3, pixels))
        p, blocks = 2, []
        while True:
            final, n, nn = z[p], *struct.unpack('<HH', z[p + 1:p + 5])
            self.assertEqual(final & 6, 0, 'a stored block (BTYPE 00)')
            self.assertEqual(n ^ nn, 0xFFFF)
            blocks.append(n)
            p += 5 + n
            if final & 1:
                break
        self.assertEqual(z[:2], b'\x78\x01')
        self.assertEqual(blocks[:-1], [ui_pictures.STORED] * (len(blocks) - 1))
        self.assertEqual(sum(blocks), h * (w * 3 + 1))
        with self.assertRaises(ValueError):
            ui_pictures.png(w, h, 3, pixels[:-1])

    def test_art_cuts_a_2_to_1_box_inside_the_tilted_panel_and_writes_it_at_16_to_9(self):
        # a 1024 x 512 background: a tilted lit panel on black, as the 360's loading screens draw it
        W, H = 1024, 512
        inside = lambda x, y: 0.12 * x + 60 <= y <= 0.12 * x + 400 and 40 <= x <= 1000
        rgba = bytearray(W * H * 4)
        for y in range(H):
            for x in range(W):
                if inside(x, y):
                    rgba[(y * W + x) * 4:(y * W + x) * 4 + 4] = bytes((200, (x * 3) & 0xFF, (y * 5) & 0xFF, 255))
        rgba = bytes(rgba)
        y, x, h, w = ui_pictures.largest_2to1(ui_pictures.panel_mask(W, H, rgba))
        self.assertEqual(w, 2 * h)
        for cx, cy in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
            for dx, dy in ((0, 0), (2, 0), (-2, 0), (0, 2), (0, -2)):      # the inset: two 4-neighbour erosions
                self.assertTrue(inside(cx + dx, cy + dy), 'the box keeps 2 px inside the panel at corner %r' % ((cx, cy),))
        ow, oh, rgb = ui_pictures.art(W, H, rgba)
        self.assertEqual((ow, oh, len(rgb)), (w, round(w * 9 / 16), ow * oh * 3))
        for c in range(0, ow, 37):                       # the vertical stretch only blends neighbouring rows (no blur across)
            for k in range(3):
                top, below = rgba[(y * W + x + c) * 4 + k], rgba[((y + 1) * W + x + c) * 4 + k]
                self.assertTrue(min(top, below) <= rgb[3 * c + k] <= max(top, below))

    def test_textures_pair_names_with_their_fetch_headers(self):
        # a system segment with two fetch headers and two texture objects naming them; an 8888 linear texture decodes
        sysm = bytearray(0x400)
        def header(at, w, h, base):
            sysm[at:at + 4] = b'\x00\x20\x00\x03'
            sysm[at + 0x14:at + 0x18] = b'\xff\xff\x00\x00'
            words = [(w // 32) << 22, base | 0x06, (w - 1) | ((h - 1) << 13), 0, 0, 0]
            sysm[at + 0x1C:at + 0x34] = struct.pack('>6I', *words)
        header(0x100, 32, 2, 0x0000)
        header(0x180, 32, 1, 0x1000)
        for k, (name_at, head_at, name) in enumerate(((0x300, 0x100, b'button_30'), (0x340, 0x180, b'background'))):
            sysm[name_at:name_at + len(name) + 1] = name + b'\0'
            obj = 0x200 + 0x20 * k
            sysm[obj:obj + 8] = struct.pack('>II', 0x50000000 | name_at, 0x00013000)
            sysm[obj + 20:obj + 28] = struct.pack('>II', 0x50000000 | head_at, 0)
        gfx = bytes(range(256)) * 32
        w, h, rgba = ui_pictures.texture(bytes(sysm), gfx, 'button_30', 'test')
        self.assertEqual((w, h, rgba[:4]), (32, 2, bytes((2, 1, 0, 3))))   # BGRA in memory -> RGBA
        with self.assertRaises(ValueError):
            ui_pictures.texture(bytes(sysm), gfx, 'button_99', 'test')


class Stage(unittest.TestCase):
    def test_manifest_lists_the_files_directly_in_ui_with_their_hash_and_size(self):
        files = {'ui/a.ttf': b'font', 'ui/glyph-a.png': b'png', 'ui/sounds/select.wav': b'RIFFwav'}
        lines = build_ui_assets.manifest(files).decode().splitlines()
        self.assertEqual(lines, ['%s %d %s' % (hashlib.sha256(d).hexdigest(), len(d), n[3:]) for n, d in sorted(files.items())
                                 if n.count('/') == 1])

    def test_profile_helper_adds_the_stage_last_with_a_pin_for_every_file_the_app_accepts(self):
        import disc_builder
        profile = {'stages': [{'id': 'codegen'}, {'id': 'compile-link'}], 'bundle': {'format': 2, 'components': [], 'portable_tools': []}}
        pins = {'schema': ui_stage.PINS_SCHEMA, 'files': {n: '0' * 64 for n in ui_stage.files()}}
        out = ui_stage.add(json.loads(json.dumps(profile)), pins)
        stage = out['stages'][-1]
        self.assertEqual(stage['id'], ui_stage.STAGE_ID)
        self.assertEqual(set(stage['expect']), set(stage['outputs']) - {'tree', 'report'})
        self.assertIn('ui/manifest.txt', ui_stage.files())
        for c in out['bundle']['components']:
            self.assertIsNone(disc_builder.role_problem(c['name'], c['role'], True), c['name'])
            self.assertEqual(stage['outputs'][c['output']], ui_stage.STAGE_ID + '/' + c['name'])
        some = ui_stage.files()[0]
        for label, broken_pins, broken_profile in (
                ('a missing pin', {k: v for k, v in pins['files'].items() if k != some}, profile),
                ('an extra pin', dict(pins['files'], **{'ui/other.png': '0' * 64}), profile),
                ('a pin that is no hash', dict(pins['files'], **{some: 'x' * 64}), profile),
                ('a format-1 bundle', pins['files'], dict(profile, bundle=dict(profile['bundle'], format=1)))):
            with self.subTest(label), self.assertRaises(ValueError):
                ui_stage.add(json.loads(json.dumps(broken_profile)), {'schema': ui_stage.PINS_SCHEMA, 'files': broken_pins})

    def test_refuses_an_existing_output_and_unknown_parts_before_reading_a_disc(self):
        with tempfile.TemporaryDirectory(dir=HERE.parents[2] / 'local-analysis') as tmp:
            common = ['--play', str(Path(tmp) / 'no-disc.iso'), '--generated', tmp, '--xmem', tmp]
            self.assertEqual(build_ui_assets.main(common + ['--output', tmp]), 1)
            self.assertEqual(build_ui_assets.main(common + ['--output', str(Path(tmp) / 'new'), '--parts', 'fonts,movies']), 1)
            self.assertFalse((Path(tmp) / 'new').exists())


class Sounds(unittest.TestCase):
    """The sound tree walk and the mix (from the start-menu lane's test_ui_sounds.py) on synthetic tables."""

    @staticmethod
    def rel(objects):
        data, index = b'', b''
        for h, body in objects:
            index += struct.pack('>III', h, len(data), len(body))
            data += body
        return struct.pack('>II', 54, len(data)) + data + struct.pack('>I', len(objects)) + index

    @staticmethod
    def obj(t, flags, fields, body):
        return bytes([t]) + struct.pack('>I', flags) + fields + body

    def setUp(self):
        A, B, M, W, Y, S = 0x10000001, 0x10000002, 0x40000001, 0x30000001, 0x50000001, 0x20000001
        o = self.obj
        self.simple_a = o(12, 0x00800004, struct.pack('>hB', 500, 3), struct.pack('>IIB', u.CONTAINER, 0x11111111, 0))
        simple_b = o(12, 0x00800010, struct.pack('>hB', -600, 1), struct.pack('>IIB', u.CONTAINER, 0x22222222, 0))
        multi = o(13, 0x00000001, struct.pack('>I', 0xAAA0AAAA), bytes([2]) + struct.pack('>II', A, B))
        wrap = o(6, 0x01008054, struct.pack('>hhHIB', 400, 100, 0, 0xAAEC069B, 2), struct.pack('>I', M))
        synth = o(18, 0x00000001, struct.pack('>I', 0xAAA8AAAA), b'\0' * 8)
        self.wrap_synth = o(6, 0x00000004 | 0xAAAAAA00, struct.pack('>h', -700), struct.pack('>I', Y))
        foreign = o(12, 0x00800000, bytes([3]), struct.pack('>IIB', 0x12345678, 0x33333333, 0))
        self.W, self.S = W, S
        self.sounds = u.Sounds(self.rel([(A, self.simple_a), (B, simple_b), (M, multi), (W, wrap), (Y, synth), (S, self.wrap_synth),
                                         (0x60000001, foreign)]))

    def test_header_fields_are_gated_by_their_bits(self):
        self.assertEqual(u.Sounds.header(self.simple_a)[0], {u.VOLUME: 500, u.SPEAKERS: 3})
        self.assertEqual(u.Sounds.header(self.wrap_synth)[0], {u.VOLUME: -700}, 'a 0xAA flags byte skips its group')

    def test_tree_walk(self):
        self.assertEqual(u.plan(self.sounds, self.W), [(0x11111111, 900, 100, 3), (0x02222222, 400, -500, 1)])
        self.assertEqual(u.plan(self.sounds, self.S), 'synth')
        self.assertTrue(str(u.plan(self.sounds, 0x60000001)).startswith('unsupported:container'))
        self.assertEqual(u.plan(self.sounds, 0x7FFFFFFF), 'missing')

    def test_mix(self):
        loud = {1: ([0.9] * 480, u.RATE, 0), 2: ([0.9] * 480, u.RATE, 0)}
        frames = u.render([(1, 0, 0, 3), (2, 0, 0, 3)], loud)
        peak = max(abs(v) for v in struct.unpack('<%dh' % (len(frames) // 2), frames)) / 32767
        self.assertAlmostEqual(peak, 10 ** (-1 / 20), places=3, msg='two full layers scaled to -1 dBFS, never clipped')
        quiet = {1: ([0.1] * 480, u.RATE, 0)}
        l, r = struct.unpack('<hh', u.render([(1, 0, 0, 1)], quiet)[:4])
        self.assertTrue(abs(l / 32767 - 0.1) < 0.001 and r == 0, 'a quiet left-only layer keeps its level and side')
        self.assertEqual(len(u.render([(1, 0, 1200, 3)], quiet)) // 4, 240, '+1200 cents halves the length')
        data = u.wav(b'\0' * 8)
        self.assertEqual((data[:4], data[8:12]), (b'RIFF', b'WAVE'), 'the launcher refuses anything but RIFF WAVE')


class Decoder(unittest.TestCase):
    def test_constants_are_the_doubles_nearest_the_exact_values(self):
        # independent oracle: the platform's libm (within one unit in the last place)
        for m, k in ((128, 1), (256, 77), (512, 1023), (512, 2048), (64, 129)):
            self.assertLessEqual(abs(xma2_decode.cos_q(m, k) - math.cos(math.pi * k / (4 * m))), math.ulp(1.0))
        for e in (-300, -7, 0, 1, 90, 255):
            want = 10 ** (e / 20)
            self.assertLessEqual(abs(xma2_decode.exp10_20(e) - want), 2 * math.ulp(want))

    def test_tables_come_only_from_the_pinned_ffmpeg_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            tar_path = Path(tmp) / 'ffmpeg.tar'
            with tarfile.open(tar_path, 'w') as tar:
                text = b'static const uint16_t critical_freq[] = { 100, 200 };\n'
                info = tarfile.TarInfo(xma2_decode.FFMPEG_TABLES)
                info.size = len(text)
                tar.addfile(info, io.BytesIO(text))
            with self.assertRaises(xma2_decode.XmaError) as cm:
                xma2_decode.load_tables(tar_path)
            self.assertIn('not the pinned FFmpeg file', str(cm.exception))
        with self.assertRaises(xma2_decode.XmaError):
            xma2_decode.decode(b'\0' * 2047, 512, 32000, {})

    def test_frames_with_a_wrong_length_are_refused(self):
        tables = {n: [1] for n in xma2_decode.TABLES}
        tables.update(critical_freq=[100 * i for i in range(1, 29)], scale_huffcodes=[0], scale_huffbits=[1],
                      scale_rl_huffcodes=[0], scale_rl_huffbits=[1], coef0_huffcodes=[0], coef0_huffbits=[1],
                      coef1_huffcodes=[0], coef1_huffbits=[1], vec4_huffcodes=[0], vec4_huffbits=[1],
                      vec2_huffcodes=[0], vec2_huffbits=[1], vec1_huffcodes=[0], vec1_huffbits=[1])
        dec = xma2_decode.Decoder(tables, 27937)
        # a frame of one 512-sample sub-frame that transmits nothing, whose length field says 3 bits too many
        bits = '{:015b}'.format(0) + '1' + '0' + '0' * 8 + '0' + '0' + '0' + '0' + '00'
        length = len(bits) + 3
        bits = '{:015b}'.format(length) + bits[15:]
        br = xma2_decode.Bits(int(bits, 2), len(bits))
        with self.assertRaises(xma2_decode.XmaError) as cm:
            dec.frame(br)
        self.assertIn('frame length', str(cm.exception))
        ok = '{:015b}'.format(len(bits)) + bits[15:]
        samples, more = dec.frame(xma2_decode.Bits(int(ok, 2), len(ok)))
        self.assertEqual((len(samples), more, max(map(abs, samples))), (512, 0, 0.0), 'a silent frame decodes to 512 zeros')


# ---- cross-host determinism: a synthetic stream (made-up code tables, no disc data, no FFmpeg tables) ----
def synthetic_tables():
    def fixed(n):                                       # a fixed-length prefix code of n symbols
        return list(range(n)), [max(1, (n - 1).bit_length())] * n
    t = {'critical_freq': [150 * i + 10 * i * i for i in range(1, 29)]}
    for name, n in (('scale', 121), ('scale_rl', 120), ('coef0', 272), ('coef1', 244), ('vec4', 127), ('vec2', 137), ('vec1', 101)):
        t[name + '_huffcodes'], t[name + '_huffbits'] = fixed(n)
    t.update(scale_rl_run=[i % 3 for i in range(120)], scale_rl_level=[i % 5 for i in range(120)],
             coef0_run=[i % 4 for i in range(272)], coef0_level=[1 + i % 7 for i in range(272)],
             coef1_run=[i % 4 for i in range(244)], coef1_level=[1 + i % 7 for i in range(244)],
             symbol_to_vec4=[((i % 3) << 12) | ((i % 5) << 8) | ((i % 2) << 4) | (i % 4) for i in range(127)],
             symbol_to_vec2=[((i % 4) << 4) | (i % 3) for i in range(137)])
    return t


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value, n):
        self.bits += [(value >> (n - 1 - i)) & 1 for i in range(n)]


def synthetic_frame(t, dec, subframes, more, salt):
    """One frame whose every field the decoder reads in order: tile header, sub-frames that transmit vector and run-level
    coefficients, DPCM and run-level scale factors, the length prefix and the trailer."""
    w = BitWriter()
    w.put(0, 15)                                        # the length, patched below
    w.put(1, 1)                                         # fixed channel layout
    total = 0
    for sl in subframes:
        if total != 512 - 128:
            shift = {512: 0, 256: 1, 128: 2}[sl]
            w.put(0, 1) if not shift else (w.put(1, 1), w.put(shift - 1, 1))
        total += sl
    w.put(0x5A, 8)                                      # dynamic range gain
    w.put(1, 1), w.put(1, 1), w.put(3, 10), w.put(0, 1)  # a start skip of 3
    for n, sl in enumerate(subframes):
        bands = dec.num_sfb[(512 // sl).bit_length() - 1]
        w.put(0, 1), w.put(0, 1), w.put(1, 1), w.put(1, 1)   # no extended header, reserved 0, transmit, vector count follows
        groups = 3
        w.put(groups, ((sl + 3) // 4).bit_length())
        w.put((-4 - salt) & 63, 6)                      # quantisation step
        if n == 0:
            w.put(1, 2)                                 # scale factor step 2, DPCM factors
            for b in range(bands):
                w.put(59 + (b + salt) % 3, 7)
        else:
            w.put(1, 1)
            w.put(7, 7), w.put(1, 1)                    # run-level: one change (skip 1, level 2, positive)
            w.put(1, 7)                                 # end
        w.put(0, 1)                                     # coefficient table 0
        for g in range(groups):
            idx = (g * 7 + n + salt) % 126
            w.put(idx, 7)
            s = t['symbol_to_vec4'][idx]
            for v in (s >> 12, (s >> 8) & 15, (s >> 4) & 15, s & 15):
                if v:
                    w.put((g + v) & 1, 1)
        w.put(2 + (n + salt) % 200, 9), w.put(1, 1)     # one run-level coefficient
        w.put(1, 9)                                     # end of block
    w.put(0, 1), w.put(more, 1)                         # the bit before the trailer, the trailer
    n = len(w.bits)
    w.bits[:15] = [(n >> (14 - i)) & 1 for i in range(15)]
    return w.bits


def synthetic_stream():
    t = synthetic_tables()
    dec = xma2_decode.Decoder(t, 27937)
    data = b''
    for p, frames in enumerate((([256, 128, 128], [512]), ([128, 128, 256], [512]))):
        w = BitWriter()
        w.put(len(frames), 6), w.put(0, 15), w.put(0, 3), w.put(0, 8)
        for k, sub in enumerate(frames):
            w.bits += synthetic_frame(t, dec, sub, 1 if k + 1 < len(frames) else 0, 2 * p + k)
        w.bits += [0] * (2048 * 8 - len(w.bits))
        data += int(''.join(map(str, w.bits)), 2).to_bytes(2048, 'big')
    return t, data


def probe():
    """SHA-256 over the decoded samples (as doubles) and a mix of them (pitch, volume, headroom, two speakers) as a WAVE."""
    t, data = synthetic_stream()
    pcm = xma2_decode.decode(data, 4 * 512, 27937, t)
    wav = u.wav(u.render([(1, 500, -300, 3), (2, 0, 300, 2)], {1: (pcm, 27937, 787), 2: (pcm[::-1], 27917, -176)}))
    return hashlib.sha256(struct.pack('<%dd' % len(pcm), *pcm) + wav).hexdigest()


# The digest on the first host (macOS arm64, Python 3.14 and Xcode's 3.9). Any host, any Python must give it: the stage pins its
# menu sounds, so a host that decoded or mixed differently would fail the player's build.
PROBE_SHA256 = '9f0aaad584717fd93320305039a7952b19b2f241091b336e8f815e208f9419f1'


class Determinism(unittest.TestCase):
    def test_the_synthetic_stream_decodes_to_sound(self):
        # the probe is not trivially silent: every sub-frame size, the window transitions and the mix are exercised
        t, data = synthetic_stream()
        pcm = xma2_decode.decode(data, 4 * 512, 27937, t)
        self.assertEqual(len(pcm), 2048)
        for frame in range(4):
            self.assertGreater(max(abs(v) for v in pcm[frame * 512:(frame + 1) * 512]), 1e-6, 'frame %d' % frame)

    def test_this_host_gives_the_pinned_digest(self):
        self.assertEqual(probe(), PROBE_SHA256)

    def test_every_other_python_here_gives_the_same_digest(self):
        here = Path(__file__).resolve().parent
        candidates = ['/usr/bin/python3', '/opt/homebrew/bin/python3', '/usr/local/bin/python3']
        code = ('import sys; sys.path[:0] = [%r, %r]; import test_ui_assets as t; print(t.probe())' % (str(here), str(here.parent)))
        env = dict(os.environ, PYTHONPATH=str(here.parents[0] / 'macos' / 'python-compat'))
        ran = 0
        for exe in candidates:
            if not Path(exe).exists() or Path(exe).resolve() == Path(sys.executable).resolve():
                continue
            r = subprocess.run([exe, '-B', '-c', code], capture_output=True, text=True, env=env, timeout=300)
            if r.returncode != 0 and 'No module named' in r.stderr:
                continue                                # a Python without what the tests import: not a builder Python
            self.assertEqual((r.returncode, r.stdout.strip()), (0, probe()), '%s: %s' % (exe, r.stderr[-500:]))
            ran += 1
        if not ran:
            self.skipTest('no other Python on this host')


if __name__ == '__main__':
    unittest.main()
