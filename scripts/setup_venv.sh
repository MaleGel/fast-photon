#!/usr/bin/env bash
# Creates a local Python virtual environment under ./venv and installs
# dependencies required by every tool under tools/.
#
# Run once after cloning. Re-run any time a tool's requirements.txt changes.
# The venv is shared across all Python tools in this repo.

set -euo pipefail

# Resolve project root (parent of this script's directory).
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
VENV_DIR="$PROJECT_ROOT/venv"

# ── 1. Create venv if missing ────────────────────────────────────────────────
if [ ! -d "$VENV_DIR" ]; then
    echo "[setup] Creating virtual environment at $VENV_DIR"
    python -m venv "$VENV_DIR" || python3 -m venv "$VENV_DIR"
else
    echo "[setup] venv already exists — reusing $VENV_DIR"
fi

# ── 2. Pick platform-specific interpreter path ───────────────────────────────
if [ -x "$VENV_DIR/Scripts/python.exe" ]; then
    PY="$VENV_DIR/Scripts/python.exe"        # Windows (Git Bash / MSYS)
else
    PY="$VENV_DIR/bin/python"                # Linux / macOS / WSL
fi

# ── 3. Upgrade pip, install every requirements.txt under tools/ ─────────────
echo "[setup] Upgrading pip"
"$PY" -m pip install --upgrade pip >/dev/null

find "$PROJECT_ROOT/tools" -name 'requirements.txt' -print0 \
    | while IFS= read -r -d '' REQ; do
        echo "[setup] Installing from $REQ"
        "$PY" -m pip install -r "$REQ"
    done

echo "[setup] Done."
