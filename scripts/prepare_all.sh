#!/usr/bin/env bash
# One-shot project bootstrap: runs every setup script in scripts/ that is
# not this file and not prefixed with '_'. Idempotent — safe to rerun.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
SELF="$(basename "${BASH_SOURCE[0]}")"

# Fixed order ensures dependencies (venv before hooks that might use it).
STEPS=(
    "setup_venv.sh"
    "install_hooks.sh"
)

for STEP in "${STEPS[@]}"; do
    SCRIPT="$SCRIPT_DIR/$STEP"
    if [ ! -x "$SCRIPT" ]; then
        echo "[prepare] skipping $STEP (not found or not executable)"
        continue
    fi
    echo "[prepare] running $STEP"
    "$SCRIPT"
done

echo "[prepare] All done."
