"""1.2.0 (owner): each Windows builder download opens its window on its own edition and lists both Android editions (one
bundle) with iOS and Duo greyed. The Single-Screen and Thor Edition
downloads carry the same Thor-line profile (one bundle for both apps), so the edition cannot come from the profile's name;
stage_gui.py --edition writes it into the package (edition.txt, outside the builder's inventory) and the window reads it
(builder_core.discover_editions). Synthetic packages only.

    python3 -m unittest tools.own_disc.gui.tests.test_package_edition
"""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

OWN_DISC = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(OWN_DISC))
sys.path.insert(0, str(OWN_DISC / 'gui'))
import builder as B  # noqa: E402
import builder_core as core  # noqa: E402
import stage_gui  # noqa: E402


def package(folder: Path) -> Path:
    """a builder package as the release lane makes them: release.json + one Thor-line profile (no other files needed here)"""
    folder.mkdir(parents=True)
    (folder / 'release.json').write_text(json.dumps({'schema': 'builder-release/1', 'host': B.host_id(),
                                                     'profile': 'profile-thor-fivepocket-thor-v1922-3n-ui.json'}))
    (folder / 'profile-thor-fivepocket-thor-v1922-3n-ui.json').write_text(json.dumps(
        {'id': 'gta5-disc-thor-fivepocket-thor-v1922-3-portable', 'stages': [{'id': 'extract-xex'}], 'games': []}))
    return folder


def opens_as(folder: Path) -> str:
    """the edition the window selects first for this package alone (AppState.chosen: the available editions, in order)"""
    return next(e.key for e in core.discover_editions([folder]) if e.available)


class PackageEdition(unittest.TestCase):
    def test_each_download_opens_on_its_own_edition(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = package(Path(tmp) / 'base')
            thor = Path(tmp) / 'FivePocket-Builder-Windows-Thor-Edition'
            single = Path(tmp) / 'FivePocket-Builder-Windows-Single-Screen'
            stage_gui.stage(base, thor, edition='thor')
            stage_gui.stage(base, single, edition='fold')
            self.assertEqual(opens_as(thor), 'thor')
            self.assertEqual(opens_as(single), 'fold')           # the window's ANDROID word and android logo (launcher.js)
            self.assertEqual(opens_as(base), 'thor', 'a package without the file keeps the profile-derived edition')
            for out, word in ((thor, 'thor'), (single, 'fold')):
                added = json.loads((out / 'gui-manifest.json').read_text())['added']
                self.assertIn('edition.txt', [a['path'] for a in added])
                self.assertEqual((out / 'edition.txt').read_bytes(), word.encode() + b'\n')

    def test_each_package_lists_both_android_editions_and_the_apple_ones_greyed(self):
        # owner 1.2.0: every Windows download offers Thor Edition AND single screen (one bundle), iOS and Duo greyed
        with tempfile.TemporaryDirectory() as tmp:
            base = package(Path(tmp) / 'base')
            for name, word in (('Android', 'fold'), ('Thor', 'thor')):
                out = Path(tmp) / name
                stage_gui.stage(base, out, edition=word)
                listed = {e.key: e for e in core.discover_editions([out])}
                self.assertEqual(sorted(listed), ['duo', 'fold', 'ios', 'thor'], name)
                self.assertTrue(listed['thor'].available and listed['fold'].available, name)
                for apple in ('ios', 'duo'):
                    self.assertFalse(listed[apple].available)
                    self.assertIn('macOS builder package', listed[apple].reason)
                self.assertEqual(opens_as(out), word, name)
                self.assertNotEqual(listed['thor'].ident, listed['fold'].ident)

    def test_both_android_editions_build_once_with_a_copy_each(self):
        with tempfile.TemporaryDirectory() as tmp:
            out_pkg = Path(tmp) / 'Android'
            stage_gui.stage(package(Path(tmp) / 'base'), out_pkg, edition='fold')
            discs = {'play': Path(tmp) / 'play.iso', 'install': Path(tmp) / 'install.iso'}
            for d in discs.values():
                d.write_bytes(b'x')
            both = [e for e in core.discover_editions([out_pkg]) if e.available]
            steps = core.make_plan(both, discs, Path(tmp) / 'out', Path(tmp) / 'cache', True,
                                   fetched={str(both[0].release_path): True})
            self.assertEqual([s.kind for s in steps], ['check', 'build', 'verify', 'build', 'verify'])
            outs = [s.words[s.words.index('--out') + 1] for s in steps if s.kind == 'build']
            self.assertEqual(outs, [str(Path(tmp) / 'out' / 'Single screen'), str(Path(tmp) / 'out' / 'Thor Edition')])
            one = core.make_plan(both[:1], discs, Path(tmp) / 'out', Path(tmp) / 'cache', True, fetched={str(both[0].release_path): True})
            self.assertEqual([s.words[s.words.index('--out') + 1] for s in one if s.kind == 'build'], [str(Path(tmp) / 'out')])

    def test_an_unknown_edition_word_is_refused_by_the_stager_and_ignored_by_the_window(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = package(Path(tmp) / 'base')
            with self.assertRaises(stage_gui.StageError):
                stage_gui.stage(base, Path(tmp) / 'out', edition='ios')
            (base / 'edition.txt').write_text('ios\n')
            self.assertEqual(opens_as(base), 'thor')


if __name__ == '__main__':
    unittest.main()
