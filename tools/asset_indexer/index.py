"""
Asset indexer for fast-photon.

Regenerates the 'textures' and 'sprites' sections of assets/assets.json
by scanning the asset folders on disk. The 'shaders' and 'materials'
sections are human-authored and preserved verbatim.

Modes:
    - baked: assets/atlases/atlas.json exists → read pages + UVs from it.
    - raw:   no atlas → every PNG under assets/textures/ becomes its own
             texture + a sprite covering the whole image.

Usage:
    python index.py <project_root>            # regenerate assets.json
    python index.py <project_root> --check    # exit 1 if stale (pre-commit)
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple

from PIL import Image


# Canonical key order in the emitted JSON. Stable across runs → clean diffs.
SECTION_ORDER = ["shaders", "textures", "sprites", "materials"]

# Sections the indexer owns (overwritten). Others are preserved verbatim.
GENERATED_SECTIONS = {"textures", "sprites"}


# ── Discovery helpers ────────────────────────────────────────────────────────

def sprite_id_from_path(path: Path, root: Path) -> str:
    """PNG path → sprite id (relative, no extension, forward slashes)."""
    rel = path.relative_to(root).with_suffix("")
    return rel.as_posix()


def scan_raw_textures(textures_dir: Path) -> Tuple[List[dict], List[dict]]:
    """Every PNG = its own texture + a sprite spanning the whole image."""
    textures: List[dict] = []
    sprites:  List[dict] = []

    for png in sorted(textures_dir.rglob("*.png")):
        sid = sprite_id_from_path(png, textures_dir)
        with Image.open(png) as img:
            w, h = img.size
        rel_path = png.relative_to(textures_dir.parent.parent).as_posix()
        textures.append({ "name": sid, "path": rel_path })
        sprites.append({
            "name": sid, "texture": sid,
            "x": 0, "y": 0, "w": w, "h": h,
        })

    return textures, sprites


def scan_baked_atlas(atlas_json_path: Path, project_root: Path) -> Tuple[List[dict], List[dict]]:
    """Read atlas.json; one texture per page, one sprite per packed rect."""
    with atlas_json_path.open("r", encoding="utf-8") as f:
        atlas = json.load(f)

    atlases_dir  = atlas_json_path.parent
    atlases_rel  = atlases_dir.relative_to(project_root).as_posix()

    textures: List[dict] = []
    for page in atlas.get("pages", []):
        page_name = f"atlas_{page['index']}"
        textures.append({
            "name": page_name,
            "path": f"{atlases_rel}/{page['image']}",
        })

    sprites: List[dict] = []
    for s in atlas.get("sprites", []):
        sprites.append({
            "name":    s["name"],
            "texture": f"atlas_{s['page']}",
            "x":       s["x"],
            "y":       s["y"],
            "w":       s["w"],
            "h":       s["h"],
        })

    return textures, sprites


# ── Main indexing pipeline ───────────────────────────────────────────────────

def build_indexed_json(project_root: Path, existing: Dict[str, Any]) -> Dict[str, Any]:
    """Return a new assets.json dict. Keeps hand-written sections as-is."""
    atlas_json = project_root / "assets" / "atlases" / "atlas.json"
    textures_dir = project_root / "assets" / "textures"

    if atlas_json.is_file():
        print(f"[index] baked mode (atlas: {atlas_json})")
        textures, sprites = scan_baked_atlas(atlas_json, project_root)
    elif textures_dir.is_dir():
        print(f"[index] raw mode (scanning {textures_dir})")
        textures, sprites = scan_raw_textures(textures_dir)
    else:
        print("[index] no textures and no atlas — emitting empty sections")
        textures, sprites = [], []

    # Stable sort for diff-friendliness.
    textures.sort(key=lambda e: e["name"])
    sprites.sort(key=lambda e: e["name"])

    # Assemble final dict in canonical key order.
    result: Dict[str, Any] = {}
    for key in SECTION_ORDER:
        if key in GENERATED_SECTIONS:
            result[key] = {"textures": textures, "sprites": sprites}[key]
        else:
            result[key] = existing.get(key, [])
    return result


def load_existing(path: Path) -> Dict[str, Any]:
    if not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def serialize(data: Dict[str, Any]) -> str:
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


# ── Commands ─────────────────────────────────────────────────────────────────

def cmd_write(project_root: Path) -> int:
    assets_json = project_root / "assets" / "assets.json"
    existing    = load_existing(assets_json)
    new_data    = build_indexed_json(project_root, existing)
    new_text    = serialize(new_data)

    current = assets_json.read_text(encoding="utf-8") if assets_json.is_file() else ""
    if current == new_text:
        print(f"[index] {assets_json} already up to date")
        return 0

    assets_json.write_text(new_text, encoding="utf-8")
    print(f"[index] wrote {assets_json}")
    return 0


def cmd_check(project_root: Path) -> int:
    """Same logic as write, but instead of writing — diff & exit 1 if stale."""
    assets_json = project_root / "assets" / "assets.json"
    existing    = load_existing(assets_json)
    new_data    = build_indexed_json(project_root, existing)
    new_text    = serialize(new_data)

    current = assets_json.read_text(encoding="utf-8") if assets_json.is_file() else ""
    if current == new_text:
        print(f"[index] {assets_json} is up to date")
        return 0

    print(f"[index] ERROR: {assets_json} is out of sync with asset folders.")
    print(f"[index] Run tools/asset_indexer/index.sh to regenerate.")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Regenerate textures/sprites sections of assets.json.")
    ap.add_argument("project_root", type=Path, help="path to the project root (contains assets/)")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if assets.json would change, but do not write")
    args = ap.parse_args()

    if not args.project_root.is_dir():
        print(f"[index] ERROR: not a directory: {args.project_root}")
        return 1

    return cmd_check(args.project_root) if args.check else cmd_write(args.project_root)


if __name__ == "__main__":
    sys.exit(main())
