#!/usr/bin/env bash
# Launches the particle editor. Mirrors the animation_editor wrapper —
# uses the shared <repo>/venv, idempotently installs this tool's
# requirements, then execs the editor.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"
REQS="$SCRIPT_DIR/requirements.txt"

if [[ -f "$VENV_DIR/Scripts/python.exe" ]]; then
    VENV_PY="$VENV_DIR/Scripts/python.exe"
elif [[ -f "$VENV_DIR/bin/python" ]]; then
    VENV_PY="$VENV_DIR/bin/python"
else
    VENV_PY=""
fi

if [[ -z "$VENV_PY" ]]; then
    echo "[particle_editor] Creating shared venv at $VENV_DIR…"
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

"$VENV_PY" -m pip install -q -r "$REQS"

exec "$VENV_PY" "$SCRIPT_DIR/main.py" "$@"
