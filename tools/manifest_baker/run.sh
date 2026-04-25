#!/usr/bin/env bash
# Run the manifest baker. No external deps — uses only the Python stdlib —
# so we just need any python on PATH. The shared venv isn't required, but
# we go through it for consistency with sibling tools.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VENV_DIR="$REPO_ROOT/venv"

if [[ -f "$VENV_DIR/Scripts/python.exe" ]]; then
    PY="$VENV_DIR/Scripts/python.exe"
elif [[ -f "$VENV_DIR/bin/python" ]]; then
    PY="$VENV_DIR/bin/python"
elif command -v python >/dev/null 2>&1; then
    PY=python
elif command -v py >/dev/null 2>&1; then
    PY=py
else
    echo "Error: no python on PATH." >&2
    exit 1
fi

exec "$PY" "$SCRIPT_DIR/bake.py" "$REPO_ROOT" "$@"
