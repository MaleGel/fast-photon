"""
Entry point for the animation editor. Resolves the project root and the
target manifest, then opens the main window.

Run from the repo root:
    python tools/animation_editor/main.py
or:
    python tools/animation_editor/main.py --manifest path/to/assets.json
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

# Local modules — the script may be launched directly (no package install),
# so make sure our directory is on sys.path before importing from it.
HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from manifest import Manifest                      # noqa: E402
from ui.main_window import MainWindow              # noqa: E402


def project_root() -> Path:
    # main.py lives at <repo>/tools/animation_editor/main.py
    return HERE.parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description="fast-photon animation editor")
    parser.add_argument(
        "--manifest", type=Path,
        default=project_root() / "assets" / "assets.json",
        help="Path to assets.json (default: <repo>/assets/assets.json)",
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)
    manifest = Manifest(args.manifest)
    window = MainWindow(manifest, project_root=project_root())
    window.resize(1400, 900)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
