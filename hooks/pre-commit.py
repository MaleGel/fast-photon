"""
Pre-commit logic for fast-photon. Invoked by hooks/pre-commit (bash shim)
which ensures this runs inside the project venv.

Fast path: only verifies staged asset changes, not the whole tree.
For a full refresh of assets.json, run: tools/asset_indexer/index.sh
"""

import subprocess
import sys
from pathlib import Path


def main() -> int:
    repo_root = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True).strip())

    sys.path.insert(0, str(repo_root / "tools" / "asset_indexer"))
    import check_staged  # noqa: E402

    return check_staged.check_staged(repo_root)


if __name__ == "__main__":
    sys.exit(main())
