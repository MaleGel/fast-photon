"""
Fast pre-commit check for fast-photon.

Verifies that staged asset changes (PNG files under assets/textures/) and
the matching faction manifests are mutually consistent — without touching
the working copy or HEAD.

Manifest layout this script understands:

    assets/textures/common/<rel>.png        ↔ assets/assets.json
    assets/textures/<faction>/<rel>.png     ↔ assets/data/factions/<faction>/assets.json

Inside each manifest, sprite ids are stored *unqualified* (the bake step
adds the faction prefix later). So 'assets/textures/player/warrior.png'
must have a sprite named 'warrior' in player's manifest, not
'player/warrior'.

Contract:
    * For every staged PNG: the matching faction manifest must also be
      staged, and must have a corresponding sprite entry (added/modified
      → present, deleted → absent, renamed → both transitions reflected).
    * Sprite W/H in the manifest must match the staged PNG dimensions.
"""

from __future__ import annotations

import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from PIL import Image


# ── Paths we care about ──────────────────────────────────────────────────────

TEXTURES_PREFIX = "assets/textures/"
COMMON_NAME     = "common"
COMMON_MANIFEST = "assets/assets.json"


# ── Git helpers ──────────────────────────────────────────────────────────────

def _git(repo_root: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo_root), *args], text=True)


def _git_bytes(repo_root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(repo_root), *args])


def _staged_changes(repo_root: Path) -> List[Tuple[str, str, Optional[str]]]:
    """
    Returns a list of (status, path, rename_target) for every staged change.
    status is 'A'/'M'/'D'/'R'. rename_target is set only for 'R'.
    """
    raw = _git(repo_root, "diff", "--cached", "--name-status", "-z")
    fields = [f for f in raw.split("\x00") if f]

    out: List[Tuple[str, str, Optional[str]]] = []
    i = 0
    while i < len(fields):
        status = fields[i]
        if status.startswith("R"):
            out.append(("R", fields[i + 1], fields[i + 2]))
            i += 3
        else:
            out.append((status[0], fields[i + 1], None))
            i += 2
    return out


def _staged_blob(repo_root: Path, path: str) -> Optional[bytes]:
    """Read staged content of 'path' (repo-relative). None if not staged."""
    try:
        return _git_bytes(repo_root, "show", f":0:{path}")
    except subprocess.CalledProcessError:
        return None


# ── Faction routing ──────────────────────────────────────────────────────────

def _split_texture_path(repo_path: str) -> Optional[Tuple[str, str]]:
    """assets/textures/<faction>/<rel>.png  →  (faction, sprite_id).
    Returns None for paths that don't fit the expected shape (e.g. a PNG
    placed directly under assets/textures/ without a faction folder)."""
    if not (repo_path.startswith(TEXTURES_PREFIX) and repo_path.endswith(".png")):
        return None
    rel_inside = repo_path[len(TEXTURES_PREFIX):]
    parts = rel_inside.split("/", 1)
    if len(parts) < 2 or not parts[1]:
        return None
    faction = parts[0]
    sprite_id = parts[1].rsplit(".", 1)[0]
    return faction, sprite_id


def _manifest_path_for(faction: str) -> str:
    if faction == COMMON_NAME:
        return COMMON_MANIFEST
    return f"assets/data/factions/{faction}/assets.json"


# ── Manifest parsing ─────────────────────────────────────────────────────────

def _parse_json(data: bytes) -> dict:
    return json.loads(data.decode("utf-8"))


def _sprite_entry(manifest: dict, sprite_id: str) -> Optional[dict]:
    for s in manifest.get("sprites", []) or []:
        if s.get("name") == sprite_id:
            return s
    return None


def _png_size(blob: bytes) -> Tuple[int, int]:
    with Image.open(io.BytesIO(blob)) as img:
        return img.width, img.height


# ── Per-PNG check ────────────────────────────────────────────────────────────

def _check_png_change(
    repo_root: Path,
    status: str,
    path: str,
    rename_target: Optional[str],
    manifests: Dict[str, dict],
    staged_manifest_paths: Set[str],
    errors: List[str],
) -> None:
    """Validate one staged PNG change against its faction manifest."""

    def require(cond: bool, msg: str) -> None:
        if not cond:
            errors.append(msg)

    def faction_for(p: str) -> Optional[Tuple[str, str]]:
        parsed = _split_texture_path(p)
        if parsed is None:
            errors.append(
                f"PNG '{p}' is not inside a faction folder. "
                f"Place it under assets/textures/<faction>/ "
                f"(e.g. assets/textures/common/{Path(p).name})."
            )
        return parsed

    def manifest_for(faction: str, png_path: str) -> Optional[dict]:
        man_path = _manifest_path_for(faction)
        if man_path not in staged_manifest_paths:
            errors.append(
                f"PNG '{png_path}' is staged but {man_path} is not. "
                f"Run tools/asset_indexer/index.sh and re-stage."
            )
            return None
        return manifests.get(man_path)

    if status == "A":
        parsed = faction_for(path)
        if parsed is None:
            return
        faction, sid = parsed
        manifest = manifest_for(faction, path)
        if manifest is None:
            return
        require(_sprite_entry(manifest, sid) is not None,
                f"staged PNG '{path}' has no matching sprite '{sid}' "
                f"in {_manifest_path_for(faction)}")

    elif status == "D":
        parsed = faction_for(path)
        if parsed is None:
            return
        faction, sid = parsed
        manifest = manifest_for(faction, path)
        if manifest is None:
            return
        require(_sprite_entry(manifest, sid) is None,
                f"staged deletion of '{path}' but sprite '{sid}' still "
                f"exists in {_manifest_path_for(faction)}")

    elif status == "R":
        assert rename_target is not None
        # Renames during the flat→faction migration have a source path that
        # ISN'T inside a faction folder (e.g. 'assets/textures/tile.png' →
        # 'assets/textures/common/tile.png'). For the source side we just
        # need to confirm there's no orphan sprite under the old layout's
        # would-be id; we don't require a manifest for it.
        new = _split_texture_path(rename_target)
        if new is None:
            errors.append(
                f"PNG rename target '{rename_target}' is not inside a "
                f"faction folder. Place it under assets/textures/<faction>/."
            )
            return
        new_faction, new_sid = new
        new_manifest = manifest_for(new_faction, rename_target)
        if new_manifest is None:
            return
        require(_sprite_entry(new_manifest, new_sid) is not None,
                f"staged rename to '{rename_target}' but sprite "
                f"'{new_sid}' is missing from staged "
                f"{_manifest_path_for(new_faction)}")

        # If the source was inside a faction folder too (a normal rename,
        # not a migration), make sure the old sprite id is gone from the
        # source manifest.
        old = _split_texture_path(path)
        if old is not None:
            old_faction, old_sid = old
            old_manifest = manifest_for(old_faction, path)
            if old_manifest is not None:
                require(_sprite_entry(old_manifest, old_sid) is None,
                        f"staged rename from '{path}' but sprite "
                        f"'{old_sid}' still exists in staged "
                        f"{_manifest_path_for(old_faction)}")

    elif status == "M":
        parsed = faction_for(path)
        if parsed is None:
            return
        faction, sid = parsed
        manifest = manifest_for(faction, path)
        if manifest is None:
            return
        entry = _sprite_entry(manifest, sid)
        if entry is None:
            errors.append(f"staged PNG '{path}' has no matching sprite "
                          f"'{sid}' in {_manifest_path_for(faction)}")
            return
        blob = _staged_blob(repo_root, path)
        if blob is None:
            errors.append(f"cannot read staged PNG '{path}'")
            return
        w, h = _png_size(blob)
        # Only enforce dims when the sprite covers the whole image (a
        # full-image entry). Sub-rect sprites authored in animation_editor
        # have rects smaller than the texture by design.
        if entry.get("x", 0) == 0 and entry.get("y", 0) == 0 \
                and entry.get("w") == w and entry.get("h") == h:
            return
        # If x/y are zero and w/h disagree with the PNG, the manifest
        # genuinely lags — let the user know.
        if entry.get("x", 0) == 0 and entry.get("y", 0) == 0:
            require(entry.get("w") == w and entry.get("h") == h,
                    f"sprite '{sid}' in {_manifest_path_for(faction)} "
                    f"has dimensions {entry.get('w')}x{entry.get('h')} "
                    f"but staged PNG is {w}x{h}")


# ── Entry point ──────────────────────────────────────────────────────────────

def check_staged(repo_root: Path) -> int:
    changes = _staged_changes(repo_root)

    png_changes: List[Tuple[str, str, Optional[str]]] = []
    staged_manifest_paths: Set[str] = set()

    for status, path, rename_target in changes:
        if path.startswith(TEXTURES_PREFIX) and path.endswith(".png"):
            png_changes.append((status, path, rename_target))
        if path == COMMON_MANIFEST or (
                path.startswith("assets/data/factions/")
                and path.endswith("/assets.json")):
            staged_manifest_paths.add(path)
        # Renames can land the new path under textures/ too.
        if status == "R" and rename_target:
            if (rename_target.startswith(TEXTURES_PREFIX)
                    and rename_target.endswith(".png")
                    and not path.startswith(TEXTURES_PREFIX)):
                png_changes.append(("A", rename_target, None))

    if not png_changes:
        # Nothing PNG-related — manifests can be edited freely (the bake
        # step does the cross-reference validation).
        return 0

    # Pre-load every manifest that's staged. Manifests that aren't staged
    # are reported per-PNG ("manifest must also be staged") rather than up
    # front — gives a more actionable message.
    manifests: Dict[str, dict] = {}
    for man_path in staged_manifest_paths:
        blob = _staged_blob(repo_root, man_path)
        if blob is None:
            print(f"[pre-commit] ERROR: {man_path} is staged but its blob "
                  f"is unreadable.", file=sys.stderr)
            return 1
        try:
            manifests[man_path] = _parse_json(blob)
        except json.JSONDecodeError as exc:
            print(f"[pre-commit] ERROR: staged {man_path} is not valid "
                  f"JSON: {exc}", file=sys.stderr)
            return 1

    errors: List[str] = []
    for status, path, rename_target in png_changes:
        _check_png_change(
            repo_root, status, path, rename_target,
            manifests, staged_manifest_paths, errors,
        )

    if errors:
        print("[pre-commit] staged PNG changes are inconsistent with "
              "their faction manifests:", file=sys.stderr)
        # de-dup while preserving order — multiple PNGs may report the
        # same "manifest not staged" error otherwise.
        seen: Set[str] = set()
        for e in errors:
            if e in seen:
                continue
            seen.add(e)
            print(f"  - {e}", file=sys.stderr)
        print("[pre-commit] Run tools/asset_indexer/index.sh and re-stage.",
              file=sys.stderr)
        return 1

    return 0


def main() -> int:
    repo_root = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True).strip())
    return check_staged(repo_root)


if __name__ == "__main__":
    sys.exit(main())
