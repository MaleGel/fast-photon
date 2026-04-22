#!/usr/bin/env bash
# Runs bake.py against assets/textures/ and writes the result into
# assets/atlases/. Uses the shared venv at <project_root>/venv
# (run scripts/setup_venv.sh once to create it).
#
# Extra arguments are forwarded to bake.py, e.g.
#     tools/atlas_baker/bake.sh --page-size 512

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd)"
VENV_DIR="$PROJECT_ROOT/venv"

if [ -x "$VENV_DIR/Scripts/python.exe" ]; then
    PY="$VENV_DIR/Scripts/python.exe"
elif [ -x "$VENV_DIR/bin/python" ]; then
    PY="$VENV_DIR/bin/python"
else
    echo "venv not found at $VENV_DIR. Run scripts/setup_venv.sh first." >&2
    exit 1
fi

BAKE_SCRIPT="$SCRIPT_DIR/bake.py"
INPUT_DIR="$PROJECT_ROOT/assets/textures"
OUTPUT_DIR="$PROJECT_ROOT/assets/atlases"

exec "$PY" "$BAKE_SCRIPT" "$INPUT_DIR" "$OUTPUT_DIR" "$@"
