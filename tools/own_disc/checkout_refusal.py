"""The builder's refusal of a folder for game-derived files that lies inside a git repository (outside its local-analysis/).

Its own module, not host_support.py: host_support.py is a helper of recipe passes (tools/fp_codegen/direct_calls.py,
vmx_all.py), and the recipe record pins every helper's SHA-256, so any edit there makes every fresh build refuse at the
recipe stage ("tool helpers changed"; found 2026-11-06 when 9d8438b had put this code into host_support.py). Nothing a
recipe pass imports may import this module.
"""
from __future__ import annotations

import os
from pathlib import Path


class CheckoutRefusal(ValueError):
    """A folder for game-derived files that lies inside a git repository (outside its local-analysis/). The message is
    complete advice for the player, so a caller prints it without the generic "use an intact release" tail."""


def checkout_refusal_text(what: str, path: Path, repository: Path) -> str:
    """The one wording of the git-repository refusal (builder.py, disc_builder.py and the stage tools all use it): which
    folder, where the repository was found, why, what to do, and the developer's way."""
    example = os.path.join('Desktop', 'FivePocket-Builder')
    return (f'the {what} {path} is inside a git repository (found at {repository}). The builder only writes your game '
            f'files outside git repositories, so they can\'t be committed by accident. What to do: unzip the builder '
            f'download from the Releases page into a normal folder outside any git repository, for example {example}, '
            f'and run it there; you don\'t need to clone the repository to build. (Developers: inside a checkout, put '
            f'the work folder under its local-analysis{os.sep} folder: {repository / "local-analysis"})')


def private_output(path: Path, what: str = 'output folder') -> None:
    path = path.resolve()
    for parent in (path, *path.parents):
        if (parent / '.git').exists():
            if not path.is_relative_to(parent / 'local-analysis'):
                raise CheckoutRefusal(checkout_refusal_text(what, path, parent))
            break
