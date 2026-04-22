#!/usr/bin/env bash
# Installs git hooks from hooks/ into .git/hooks/.
# Run once after cloning. Rerun if you add/modify a hook in hooks/.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HOOKS_SRC="$PROJECT_ROOT/hooks"
HOOKS_DST="$PROJECT_ROOT/.git/hooks"

if [ ! -d "$HOOKS_DST" ]; then
    echo "[hooks] ERROR: .git/hooks not found — is this a git repo?" >&2
    exit 1
fi

# Only extension-less files are git hooks. Companion files like *.py stay
# in hooks/ and are invoked by the shim at commit time.
for SRC in "$HOOKS_SRC"/*; do
    [ -f "$SRC" ] || continue
    NAME="$(basename "$SRC")"
    case "$NAME" in
        *.*) continue ;;  # skip anything with an extension
    esac
    DST="$HOOKS_DST/$NAME"
    cp "$SRC" "$DST"
    chmod +x "$DST"
    echo "[hooks] installed $NAME"
done

echo "[hooks] Done."
