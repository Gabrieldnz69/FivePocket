#!/usr/bin/env python3
"""Puts the builder window into a NEW copy of an unpacked builder package, for a test or a release candidate:

  stage_gui.py --package SRC --out NEW [--backdrops DIR] [--launcher EXE] [--logo FILE] [--logos DIR] [--button DIR]
               [--edition thor|fold]

- copies SRC (a folder holding release.json) to NEW, which must not exist yet; SRC is only read;
- adds this checkout's tools/own_disc/gui (builder_core.py, web_app.py, web/; no tests, notes or launcher sources),
  the backdrop pictures of DIR into tools/own_disc/gui/web/backdrops, EXE as "FivePocket Builder.exe" at the root, and
  FILE (.svg/.png/.webp) as web/logo.<ext>, which the window shows in its logo box instead of the drawn lockup, and
  the logo files of DIR (and its folders) into web/logos/, chosen by edition and backdrop (web_app.AppState.logos),
  and the button pictures of DIR (button-build|resume|cancel|blank.png|webp|svg) into web/;
- with --edition, edition.txt at the root: the edition the window opens as (builder_core._package_edition; the
  Single-Screen download carries the Thor-line profile, so without it the window would open as the Thor Edition);
- writes gui-manifest.json at the root: every added file with its SHA-256 and size, and the hash of release.json, which
  stays as it was (the window is not part of the builder's inventory: load_release checks the listed files only).
Nothing else is changed. Never point --out at a published package (they are kept exactly as published).
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
GUI_FILES = ('builder_core.py', 'web_app.py', 'README.md')
WEB_TYPES = ('.html', '.css', '.js', '.svg')
PICTURE_TYPES = ('.jpg', '.jpeg', '.png', '.webp')
LAUNCHER_NAME = 'FivePocket Builder.exe'
EDITION_FILE = 'edition.txt'


class StageError(Exception):
    pass


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def stage(package: Path, out: Path, backdrops: Path | None = None, launcher: Path | None = None, logo: Path | None = None,
          logos: Path | None = None, button: Path | None = None, edition: str | None = None) -> dict:
    package, out = Path(package), Path(out)
    if not (package / 'release.json').is_file():
        raise StageError(f'{package} holds no release.json: not an unpacked builder package')
    if out.exists():
        raise StageError(f'{out} exists: staging writes only into a new folder')
    if edition not in (None, 'thor', 'fold'):
        raise StageError(f'edition {edition!r}: a package opens as thor or fold')
    if out.resolve().is_relative_to(package.resolve()):
        raise StageError('the new folder may not lie inside the package it copies')
    release_before = sha256(package / 'release.json')
    shutil.copytree(package, out, symlinks=True)
    added = []

    def put(source: Path, rel: str):
        target = out / rel
        if target.exists():
            raise StageError(f'{rel} is already in the package; refusing to replace it')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        added.append({'path': rel, 'sha256': sha256(target), 'size': target.stat().st_size})

    for name in GUI_FILES:
        put(HERE / name, f'tools/own_disc/gui/{name}')
    for p in sorted((HERE / 'web').iterdir()):
        if p.is_file() and p.suffix in WEB_TYPES:
            put(p, f'tools/own_disc/gui/web/{p.name}')
    if backdrops is not None:
        pictures = sorted(p for p in Path(backdrops).iterdir() if p.is_file() and p.suffix.lower() in PICTURE_TYPES)
        if not pictures:
            raise StageError(f'{backdrops} holds no pictures')
        for p in pictures:
            put(p, f'tools/own_disc/gui/web/backdrops/{p.name}')
    if launcher is not None:
        put(Path(launcher), LAUNCHER_NAME)
    if logo is not None:
        ext = Path(logo).suffix.lower()
        if ext not in ('.svg', '.png', '.webp'):
            raise StageError(f'{logo}: a logo is an .svg, .png or .webp file')
        put(Path(logo), f'tools/own_disc/gui/web/logo{ext}')
    if logos is not None:
        files = sorted(p for p in Path(logos).rglob('*') if p.is_file() and p.suffix.lower() in ('.svg', '.png', '.webp'))
        if not files:
            raise StageError(f'{logos} holds no logo files')
        for p in files:
            put(p, f'tools/own_disc/gui/web/logos/{p.name}')
    if button is not None:
        pictures = sorted(p for p in Path(button).iterdir() if p.is_file() and p.stem in
                          ('button-build', 'button-resume', 'button-cancel', 'button-blank') and p.suffix.lower() in ('.png', '.webp', '.svg'))
        if not pictures:
            raise StageError(f'{button} holds no button pictures (button-build.png, button-resume.png, ...)')
        for p in pictures:
            put(p, f'tools/own_disc/gui/web/{p.name}')
    if edition is not None:
        if (out / EDITION_FILE).exists():
            raise StageError(f'{EDITION_FILE} is already in the package; refusing to replace it')
        (out / EDITION_FILE).write_text(edition + '\n', encoding='utf-8', newline='\n')
        added.append({'path': EDITION_FILE, 'sha256': sha256(out / EDITION_FILE), 'size': (out / EDITION_FILE).stat().st_size})
    if sha256(out / 'release.json') != release_before:
        raise StageError('release.json changed while staging')            # cannot happen; checked, not assumed
    manifest = {'schema': 'fivepocket-gui-stage/1', 'package': package.name, 'release_json_sha256': release_before,
                'added': added}
    (out / 'gui-manifest.json').write_text(json.dumps(manifest, indent=1) + '\n', encoding='utf-8', newline='\n')
    return manifest


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--package', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--backdrops', type=Path)
    ap.add_argument('--launcher', type=Path)
    ap.add_argument('--logo', type=Path)
    ap.add_argument('--logos', type=Path)
    ap.add_argument('--button', type=Path)
    ap.add_argument('--edition', choices=('thor', 'fold'))
    a = ap.parse_args(argv)
    try:
        manifest = stage(a.package, a.out, a.backdrops, a.launcher, a.logo, a.logos, a.button, a.edition)
    except (StageError, OSError) as error:
        print(f'REFUSED: {error}')
        return 2
    print(f'Staged {len(manifest["added"])} files into {a.out} (release.json {manifest["release_json_sha256"][:12]} unchanged)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
