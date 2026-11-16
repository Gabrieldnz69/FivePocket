"""Host tests of stage_gui.py: the window goes into a new copy of a builder package, the original and the builder's
own inventory stay as they were, and every added file is recorded with its hash."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_builder_core as t           # noqa: E402  the synthetic package fixtures
import stage_gui                        # noqa: E402


class Stage(t.Synthetic):
    def test_the_window_goes_into_a_new_copy_and_the_package_still_loads(self):
        fx, release = self.package()
        before = {p.relative_to(fx.side): p.read_bytes() for p in fx.side.rglob('*') if p.is_file()}
        pictures = self.root / 'pictures'
        pictures.mkdir()
        (pictures / 'la-dusk.jpg').write_bytes(b'jpeg bytes')
        (pictures / 'notes.txt').write_bytes(b'not a picture')
        exe = self.root / 'gui_launcher.exe'
        exe.write_bytes(b'MZ synthetic launcher')
        out = self.root / 'staged package'
        logo = self.root / 'my logo.svg'
        logo.write_text('<svg xmlns="http://www.w3.org/2000/svg"/>')
        sets = self.root / 'colourways' / 'pink-blue-dusk'
        sets.mkdir(parents=True)
        (sets / 'five-pocket-thor-pink-blue-dusk.png').write_bytes(b'png')
        buttons = self.root / 'buttons'
        buttons.mkdir()
        (buttons / 'button-build.png').write_bytes(b'png')
        (buttons / 'notes.txt').write_bytes(b'x')
        manifest = stage_gui.stage(fx.side, out, pictures, exe, logo, sets.parent, buttons)
        after = {p.relative_to(fx.side): p.read_bytes() for p in fx.side.rglob('*') if p.is_file()}
        self.assertEqual(after, before)                                            # the source is only read
        added = {a['path']: a for a in manifest['added']}
        for rel in ('tools/own_disc/gui/web_app.py', 'tools/own_disc/gui/builder_core.py', 'tools/own_disc/gui/web/index.html',
                    'tools/own_disc/gui/web/launcher.js', 'tools/own_disc/gui/web/backdrops/la-dusk.jpg', 'FivePocket Builder.exe',
                    'tools/own_disc/gui/web/logo.svg', 'tools/own_disc/gui/web/logos/five-pocket-thor-pink-blue-dusk.png',
                    'tools/own_disc/gui/web/button-build.png'):
            self.assertIn(rel, added)
            self.assertEqual(hashlib.sha256((out / rel).read_bytes()).hexdigest(), added[rel]['sha256'])
        self.assertNotIn('tools/own_disc/gui/web/backdrops/notes.txt', added)
        self.assertFalse(any('tests/' in p or p.endswith('.c') or p.endswith('STATUS.md') for p in added))
        self.assertEqual(json.loads((out / 'gui-manifest.json').read_text())['release_json_sha256'],
                         hashlib.sha256(release.read_bytes()).hexdigest())
        loaded, root = t.B.load_release(out / 'release.json')                     # the builder's inventory check passes
        self.assertEqual(root, out.resolve())

    def test_it_never_writes_over_an_existing_folder_or_inside_the_package(self):
        fx, release = self.package()
        taken = self.root / 'taken'
        taken.mkdir()
        with contextlib.redirect_stdout(io.StringIO()) as said:
            self.assertEqual(stage_gui.main(['--package', str(fx.side), '--out', str(taken)]), 2)
        self.assertIn('exists', said.getvalue())
        with self.assertRaises(stage_gui.StageError):
            stage_gui.stage(fx.side, fx.side / 'inside')
        with self.assertRaises(stage_gui.StageError):
            stage_gui.stage(self.root, self.root / 'x')                            # no release.json: not a package


if __name__ == '__main__':
    unittest.main()
