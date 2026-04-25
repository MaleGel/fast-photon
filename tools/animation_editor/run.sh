#!/usr/bin/env bash
# Launches the animation editor. Uses the shared repo-level venv at
# <repo>/venv. On first run (or if any required package is missing),
# installs this tool's requirements into that venv.
#
# Works from any cwd — paths are resolved relative to this script's location.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
REQS="$SCRIPT_DIR/requirements.txt"

# Cross-platform venv layout: Windows uses Scripts/, POSIX uses bin/.
if [[ -f "$VENV_DIR/Scripts/python.exe" ]]; then
    VENV_PY="$VENV_DIR/Scripts/python.exe"
elif [[ -f "$VENV_DIR/bin/python" ]]; then
    VENV_PY="$VENV_DIR/bin/python"
else
    VENV_PY=""
fi

# First-run bootstrap: create the shared venv if it doesn't exist yet.
if [[ -z "$VENV_PY" ]]; then
    echo "[animation_editor] Creating shared venv at $VENV_DIR…"
    if command -v python >/dev/null 2>&1; then
        BOOT_PY=python
    elif command -v py >/dev/null 2>&1; then
        BOOT_PY=py
    else
        echo "Error: no python on PATH. Install Python 3.10+ first." >&2
        exit 1
    fi
    "$BOOT_PY" -m venv "$VENV_DIR"
    if [[ -f "$VENV_DIR/Scripts/python.exe" ]]; then
        VENV_PY="$VENV_DIR/Scripts/python.exe"
    else
        VENV_PY="$VENV_DIR/bin/python"
    fi
    "$VENV_PY" -m pip install --upgrade pip
fi

# Always sync this tool's requirements. pip is a no-op when everything's
# already at the right version, so the cost is one quick metadata read on
# warm runs. Catches the case where a sibling tool created the venv but
# this one's deps haven't been installed yet.
"$VENV_PY" -m pip install -q -r "$REQS"

exec "$VENV_PY" "$SCRIPT_DIR/main.py" "$@"
