#!/usr/bin/env bash
# Regenerates the textures/sprites sections of assets/assets.json.
# Extra arguments forwarded to index.py (e.g. --check for pre-commit hook).

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

exec "$PY" "$SCRIPT_DIR/index.py" "$PROJECT_ROOT" "$@"
