"""
Particle editor entry point.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

# When run via run.sh / run.bat, this file's directory is on sys.path
# already; calling python tools/particle_editor/main.py directly works
# the same way.
sys.path.insert(0, str(Path(__file__).parent))

from manifest import Manifest
from ui.main_window import MainWindow


def main() -> int:
    parser = argparse.ArgumentParser(description="fast-photon particle editor")
    parser.add_argument(
        "--manifest", type=Path, default=None,
        help="Path to assets.json (default: <repo>/assets/assets.json)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = args.manifest or (repo_root / "assets" / "assets.json")

    app = QApplication(sys.argv)
    manifest = Manifest(manifest_path)
    window = MainWindow(manifest, repo_root)
    window.resize(1200, 800)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
