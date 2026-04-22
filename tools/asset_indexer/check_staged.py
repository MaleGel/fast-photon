"""
Fast pre-commit check for fast-photon.

Verifies that staged asset changes (PNG files under assets/textures/,
assets/atlases/atlas.json, assets/assets.json) are mutually consistent
with each other — without touching the working copy or HEAD.

Contract:
    * For every staged PNG: assets.json must have a matching sprite entry
      (present for added/modified, absent for deleted, renamed appropriately).
    * If atlas.json is staged: its pages/sprites must match assets.json
      textures/sprites entries.
    * If only assets.json is staged but nothing else asset-related:
      that's fine — we only verify what this commit actually touches.
"""

import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from PIL import Image


# ── Paths we care about ──────────────────────────────────────────────────────

TEXTURES_PREFIX = "assets/textures/"
ATLASES_PREFIX  = "assets/atlases/"
ATLAS_JSON      = "assets/atlases/atlas.json"
ASSETS_JSON     = "assets/assets.json"


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
    fields = raw.split("\x00")
    fields = [f for f in fields if f]  # strip trailing empty

    out: List[Tuple[str, str, Optional[str]]] = []
    i = 0
    while i < len(fields):
        status = fields[i]
        if status.startswith("R"):
            # rename: three tokens — status, old_path, new_path
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


# ── assets.json parsing ──────────────────────────────────────────────────────

def _parse_assets_json(data: bytes) -> dict:
    return json.loads(data.decode("utf-8"))


def _sprite_entry(assets: dict, sprite_id: str) -> Optional[dict]:
    for s in assets.get("sprites", []):
        if s.get("name") == sprite_id:
            return s
    return None


def _texture_ids(assets: dict) -> Set[str]:
    return {t.get("name") for t in assets.get("textures", [])}


def _png_size(blob: bytes) -> Tuple[int, int]:
    with Image.open(io.BytesIO(blob)) as img:
        return img.width, img.height


# ── Checks ───────────────────────────────────────────────────────────────────

def _sprite_id_from_path(repo_path: str) -> str:
    """assets/textures/foo/bar.png → 'foo/bar'"""
    rel = repo_path[len(TEXTURES_PREFIX):]
    return rel.rsplit(".", 1)[0]


def _check_png_change(
    repo_root: Path,
    status: str,
    path: str,
    rename_target: Optional[str],
    assets: dict,
    errors: List[str],
) -> None:
    sprite_ids = {s.get("name") for s in assets.get("sprites", [])}

    def _require(cond: bool, msg: str) -> None:
        if not cond:
            errors.append(msg)

    if status == "A":
        sid = _sprite_id_from_path(path)
        _require(sid in sprite_ids,
                 f"staged PNG '{path}' has no matching sprite '{sid}' in staged assets.json")

    elif status == "D":
        sid = _sprite_id_from_path(path)
        _require(sid not in sprite_ids,
                 f"staged deletion of '{path}' but sprite '{sid}' still exists in staged assets.json")

    elif status == "R":
        assert rename_target is not None
        old_sid = _sprite_id_from_path(path)
        new_sid = _sprite_id_from_path(rename_target)
        _require(old_sid not in sprite_ids,
                 f"staged rename from '{path}' but sprite '{old_sid}' still exists in staged assets.json")
        _require(new_sid in sprite_ids,
                 f"staged rename to '{rename_target}' but sprite '{new_sid}' is missing from staged assets.json")

    elif status == "M":
        sid = _sprite_id_from_path(path)
        entry = _sprite_entry(assets, sid)
        if entry is None:
            errors.append(f"staged PNG '{path}' has no matching sprite '{sid}' in staged assets.json")
            return
        blob = _staged_blob(repo_root, path)
        if blob is None:
            errors.append(f"cannot read staged PNG '{path}'")
            return
        w, h = _png_size(blob)
        _require(entry.get("w") == w and entry.get("h") == h,
                 f"sprite '{sid}' dimensions in assets.json ({entry.get('w')}x{entry.get('h')}) "
                 f"do not match staged PNG ({w}x{h})")


def _check_atlas_change(
    repo_root: Path,
    status: str,
    assets: dict,
    errors: List[str],
) -> None:
    """atlas.json added/modified/deleted → consistency with assets.json."""
    texture_ids = _texture_ids(assets)
    has_atlas_textures = any(tid.startswith("atlas_") for tid in texture_ids)

    if status == "D":
        if has_atlas_textures:
            errors.append("atlas.json staged for deletion but assets.json still references atlas_* textures")
        return

    # A / M — read staged atlas.json and verify.
    blob = _staged_blob(repo_root, ATLAS_JSON)
    if blob is None:
        errors.append("atlas.json is marked staged but its staged blob is unreadable")
        return

    atlas = _parse_assets_json(blob)
    expected_textures = {f"atlas_{p['index']}" for p in atlas.get("pages", [])}
    missing = expected_textures - texture_ids
    if missing:
        errors.append(f"staged atlas.json declares pages {sorted(missing)} "
                      f"but assets.json has no matching atlas_* textures")

    expected_sprites = {s["name"] for s in atlas.get("sprites", [])}
    staged_sprite_ids = {s.get("name") for s in assets.get("sprites", [])}
    missing_sprites = expected_sprites - staged_sprite_ids
    if missing_sprites:
        errors.append(f"staged atlas.json has sprites {sorted(missing_sprites)} "
                      f"not present in staged assets.json")


# ── Entry point ──────────────────────────────────────────────────────────────

def check_staged(repo_root: Path) -> int:
    changes = _staged_changes(repo_root)

    png_changes:   List[Tuple[str, str, Optional[str]]] = []
    atlas_changes: List[Tuple[str, str, Optional[str]]] = []
    touches_assets_json = False

    for status, path, rename_target in changes:
        if path.startswith(TEXTURES_PREFIX) and path.endswith(".png"):
            png_changes.append((status, path, rename_target))
        elif path == ATLAS_JSON:
            atlas_changes.append((status, path, rename_target))
        elif path == ASSETS_JSON:
            touches_assets_json = True
        # Renames can land the new path under textures/ too.
        if status == "R" and rename_target:
            if rename_target.startswith(TEXTURES_PREFIX) and rename_target.endswith(".png") and not path.startswith(TEXTURES_PREFIX):
                png_changes.append(("A", rename_target, None))

    # Nothing asset-relevant → done.
    if not png_changes and not atlas_changes and not touches_assets_json:
        return 0

    # If any PNG/atlas change is present, assets.json must also be in this commit.
    if (png_changes or atlas_changes) and not touches_assets_json:
        print("[pre-commit] ERROR: asset files staged, but assets.json is not. "
              "Run tools/asset_indexer/index.sh and re-stage.", file=sys.stderr)
        return 1

    # Read staged assets.json.
    assets_blob = _staged_blob(repo_root, ASSETS_JSON)
    if assets_blob is None:
        # assets.json removed from stage but working copy may exist.
        print("[pre-commit] ERROR: assets.json is not staged.", file=sys.stderr)
        return 1

    try:
        assets = _parse_assets_json(assets_blob)
    except json.JSONDecodeError as e:
        print(f"[pre-commit] ERROR: staged assets.json is not valid JSON: {e}", file=sys.stderr)
        return 1

    errors: List[str] = []
    for status, path, rename_target in png_changes:
        _check_png_change(repo_root, status, path, rename_target, assets, errors)
    for status, _path, _rt in atlas_changes:
        _check_atlas_change(repo_root, status, assets, errors)

    if errors:
        print("[pre-commit] assets.json is inconsistent with staged asset files:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        print("[pre-commit] Run tools/asset_indexer/index.sh and re-stage.", file=sys.stderr)
        return 1

    return 0


def main() -> int:
    repo_root = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True).strip())
    return check_staged(repo_root)


if __name__ == "__main__":
    sys.exit(main())
