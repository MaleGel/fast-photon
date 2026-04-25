"""
Asset indexer for fast-photon.

Scans `assets/textures/<faction>/`, `assets/audio/<faction>/<group>/`, and
`assets/fonts/` and rewrites the auto-generated sections of every faction
manifest. Each filesystem layout follows the same rule:

    <faction>           — a folder name. The literal name 'common' maps
                          to assets/assets.json (the global manifest).
                          Any other name maps to
                          assets/data/factions/<faction>/assets.json.

Only `textures`, `sprites`, and `sounds` are auto-generated, plus `fonts`
in the common manifest. Everything else (shaders, materials, animations,
animation_sets, manual sub-rect sprites) is left intact.

Per-asset-type conventions:

    assets/textures/<faction>/[nested/]<name>.png
        Emits one texture entry plus a full-image sprite, both named
        '[nested/]<name>'. Pre-existing sprite entries with non-default
        rects (sub-rects authored in animation_editor) are preserved
        when their texture exists on disk.

    assets/audio/<faction>/<group>/<file>.{wav,ogg,mp3}
        Emits one sound entry. 'group' is the directory immediately
        under the faction; loop defaults to True for group=='music',
        False otherwise. Override with a sidecar <file>.<ext>.meta:
            { "loop": bool, "group": str, "alias": str }

    assets/fonts/<name>.ttf  + sidecar <name>.ttf.meta
        { "sizes": [16, 24], "alias": "ui" }
        Always common. Same expansion rule as before.

Faction discovery is convention-based: any folder under assets/textures/
or assets/audio/ counts as a faction, except 'common' which routes to
the global manifest. New factions don't require any code changes — just
drop a folder.

Usage:
    python index.py <project_root>            # regenerate manifests
    python index.py <project_root> --check    # exit 1 if stale
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from PIL import Image


# Sections this tool owns *for any faction*: it overwrites textures,
# sprites and sounds wholesale on each run (modulo the sub-rect preservation
# below). Fonts go only into the common manifest.
GENERATED_FACTION_SECTIONS = ("textures", "sprites", "sounds")
GENERATED_COMMON_FONTS_KEY = "fonts"

SOUND_EXTENSIONS = {".wav", ".ogg", ".mp3"}

# Folder name that maps to assets/assets.json instead of a faction file.
COMMON_NAME = "common"


# ── Helpers ──────────────────────────────────────────────────────────────────

def _read_meta(meta_path: Path) -> Dict[str, Any]:
    if not meta_path.is_file():
        return {}
    with meta_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _id_relative_to(path: Path, base: Path) -> str:
    """Path id without extension, forward slashes."""
    return path.relative_to(base).with_suffix("").as_posix()


def _faction_manifest_path(project_root: Path, faction: str) -> Path:
    """Where this faction's hand-edited manifest lives."""
    if faction == COMMON_NAME:
        return project_root / "assets" / "assets.json"
    return project_root / "assets" / "data" / "factions" / faction / "assets.json"


def _discover_factions(project_root: Path) -> List[str]:
    """Union of factions that have either a texture folder, an audio
    folder, or an existing manifest file. The 'common' bucket always
    counts so the global manifest stays alive even when there's nothing
    to index."""
    found: set[str] = {COMMON_NAME}
    for typ in ("textures", "audio"):
        base = project_root / "assets" / typ
        if not base.is_dir():
            continue
        for child in base.iterdir():
            if child.is_dir():
                found.add(child.name)

    factions_root = project_root / "assets" / "data" / "factions"
    if factions_root.is_dir():
        for child in factions_root.iterdir():
            if child.is_dir() and (child / "assets.json").is_file():
                found.add(child.name)

    return sorted(found)


# ── Textures / sprites ──────────────────────────────────────────────────────

def _scan_faction_textures(project_root: Path, faction: str
                           ) -> Tuple[List[dict], List[dict]]:
    """Walk assets/textures/<faction>/ — one texture + one full-image sprite
    per PNG. Sprite ids are relative to the faction folder (no faction
    prefix; bake adds that)."""
    base = project_root / "assets" / "textures" / faction
    if not base.is_dir():
        return [], []

    textures: List[dict] = []
    sprites:  List[dict] = []
    for png in sorted(base.rglob("*.png")):
        sid = _id_relative_to(png, base)
        with Image.open(png) as img:
            w, h = img.size
        rel_path = png.relative_to(project_root).as_posix()
        textures.append({"name": sid, "path": rel_path})
        sprites.append({
            "name": sid, "texture": sid,
            "x": 0, "y": 0, "w": w, "h": h,
        })
    return textures, sprites


def _is_full_image_sprite(sprite: dict, texture_lookup: Dict[str, dict]) -> bool:
    """A sprite is 'full-image' when its rect covers exactly the texture
    it references and its name matches that texture. Those are what the
    indexer would produce on its own, so they're safe to overwrite.
    Anything else (sub-rects from animation_editor) we preserve."""
    name = sprite.get("name")
    tex_name = sprite.get("texture")
    if not name or not tex_name or name != tex_name:
        return False
    tex = texture_lookup.get(tex_name)
    if not tex:
        return False
    # Try to read the actual image dimensions from disk; fall back to the
    # sprite's own rect if the file isn't available.
    return (sprite.get("x", 0) == 0 and sprite.get("y", 0) == 0
            and sprite.get("w", -1) == tex.get("_w", -2)
            and sprite.get("h", -1) == tex.get("_h", -2))


def _merge_sprites(generated_textures: List[dict],
                   generated_sprites: List[dict],
                   existing_sprites: List[dict],
                   project_root: Path) -> List[dict]:
    """Keep manual sub-rect sprites; overwrite full-image sprites."""
    # Cache image sizes so we can tell full-image rects from sub-rects.
    tex_by_name: Dict[str, dict] = {}
    for t in generated_textures:
        full = project_root / t["path"]
        try:
            with Image.open(full) as img:
                w, h = img.size
        except (FileNotFoundError, OSError):
            continue
        tex_by_name[t["name"]] = {**t, "_w": w, "_h": h}

    # Names the indexer is producing this run.
    generated_names = {s["name"] for s in generated_sprites}

    merged: List[dict] = list(generated_sprites)
    for s in existing_sprites:
        name = s.get("name")
        if not name:
            continue
        # If the existing sprite has the same name as a generated one but
        # is a manual sub-rect, the generated full-image entry would
        # shadow it. Replace the generated entry instead.
        if name in generated_names:
            if not _is_full_image_sprite(s, tex_by_name):
                # Drop the generated full-image and keep this one.
                merged = [m for m in merged if m.get("name") != name]
                merged.append(s)
            # else: generated entry already matches what we'd produce.
            continue
        # Sprite the indexer doesn't know about. Preserve it as long as
        # its referenced texture still exists locally — otherwise it's
        # an orphan from a deleted PNG.
        if s.get("texture") in tex_by_name:
            merged.append(s)
        # else: silently drop the orphan; the user removed the source PNG.

    merged.sort(key=lambda e: e["name"])
    return merged


# ── Sounds ──────────────────────────────────────────────────────────────────

def _scan_faction_sounds(project_root: Path, faction: str) -> List[dict]:
    """Walk assets/audio/<faction>/<group>/... — one entry per audio file."""
    base = project_root / "assets" / "audio" / faction
    if not base.is_dir():
        return []

    sounds: List[dict] = []
    for entry in sorted(base.rglob("*")):
        if not entry.is_file():
            continue
        if entry.suffix.lower() not in SOUND_EXTENSIONS:
            continue

        rel_inside_faction = entry.relative_to(base)
        parts = rel_inside_faction.parts
        if len(parts) < 2:
            raise RuntimeError(
                f"audio file '{entry}' is not inside a group directory. "
                f"Place it under assets/audio/{faction}/<group>/ "
                f"(e.g. assets/audio/{faction}/sfx/{entry.name})."
            )

        group = parts[0]
        default_loop = (group == "music")

        meta_path = entry.with_suffix(entry.suffix + ".meta")
        meta = _read_meta(meta_path)
        loop  = bool(meta.get("loop",  default_loop))
        group = str (meta.get("group", group))
        alias: Optional[str] = meta.get("alias")

        # Sound ids are flat (no '/') so the bake step's namespacing rules
        # don't mistake a group prefix for a cross-faction reference.
        # Default id is the file stem; alias overrides.
        default_id = entry.stem
        sound_id   = alias if alias else default_id

        sounds.append({
            "name":  sound_id,
            "path":  entry.relative_to(project_root).as_posix(),
            "group": group,
            "loop":  loop,
        })
    # Detect collisions inside this faction (two files with the same stem
    # in different groups, or duplicate aliases).
    seen: Dict[str, str] = {}
    for s in sounds:
        if s["name"] in seen:
            raise RuntimeError(
                f"sound name collision in faction '{faction}': "
                f"'{s['name']}' produced from both '{seen[s['name']]}' "
                f"and '{s['path']}'. Set a different 'alias' in one of "
                f"their .meta files."
            )
        seen[s["name"]] = s["path"]
    sounds.sort(key=lambda e: e["name"])
    return sounds


# ── Fonts (common only) ─────────────────────────────────────────────────────

def _scan_fonts(project_root: Path) -> List[dict]:
    base = project_root / "assets" / "fonts"
    fonts: List[dict] = []
    if not base.is_dir():
        return fonts

    for ttf in sorted(base.rglob("*.ttf")):
        meta_path = ttf.with_suffix(ttf.suffix + ".meta")
        meta = _read_meta(meta_path)
        sizes: List[int] = meta.get("sizes", [])
        alias: Optional[str] = meta.get("alias")

        if not sizes:
            print(f"[index] WARNING: font '{ttf.name}' has no sizes meta; "
                  f"skipping (create {meta_path.name} with "
                  f"{{\"sizes\": [24]}} to register it)")
            continue

        stem = alias if alias else ttf.stem
        rel_path = ttf.relative_to(project_root).as_posix()
        for size in sizes:
            fonts.append({
                "name": f"{stem}_{size}",
                "path": rel_path,
                "size": size,
            })
    fonts.sort(key=lambda e: e["name"])
    return fonts


# ── Per-faction merge ────────────────────────────────────────────────────────

def _build_faction_manifest(project_root: Path, faction: str,
                            existing: Dict[str, Any]) -> Dict[str, Any]:
    textures, full_sprites = _scan_faction_textures(project_root, faction)
    sounds                 = _scan_faction_sounds  (project_root, faction)

    sprites = _merge_sprites(
        generated_textures=textures,
        generated_sprites=full_sprites,
        existing_sprites=existing.get("sprites", []) or [],
        project_root=project_root,
    )

    # Common manifest keeps human-authored fonts in sync with disk too.
    extra_generated: Dict[str, Any] = {}
    if faction == COMMON_NAME:
        extra_generated[GENERATED_COMMON_FONTS_KEY] = _scan_fonts(project_root)

    generated: Dict[str, Any] = {
        "textures": textures,
        "sprites":  sprites,
        "sounds":   sounds,
        **extra_generated,
    }

    # Start from existing keys (preserves shaders, materials, animations,
    # animation_sets and any future authored sections) in their original
    # order; overwrite only the ones we own.
    result: Dict[str, Any] = {}
    seen_keys: set[str] = set()
    for key, value in existing.items():
        result[key] = generated[key] if key in generated else value
        seen_keys.add(key)
    # Append owned sections that weren't there yet.
    for key in (*GENERATED_FACTION_SECTIONS, *extra_generated.keys()):
        if key not in seen_keys:
            result[key] = generated[key]

    # If a faction file is brand-new (didn't exist on disk), seed empty
    # animation arrays so animation_editor sees something it can extend
    # without writing the keys itself first.
    if faction != COMMON_NAME:
        result.setdefault("animations", [])
        result.setdefault("animation_sets", [])

    return result


# ── IO ───────────────────────────────────────────────────────────────────────

def _load_existing(path: Path) -> Dict[str, Any]:
    if not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _serialize(data: Dict[str, Any]) -> str:
    return json.dumps(data, indent=4, ensure_ascii=False) + "\n"


# ── CLI ──────────────────────────────────────────────────────────────────────

def _process_faction(project_root: Path, faction: str, *, check_only: bool
                     ) -> Tuple[bool, Optional[Path]]:
    """Returns (changed, path). 'changed' means the file would differ;
    in check mode we never write, in write mode we write iff changed."""
    target = _faction_manifest_path(project_root, faction)
    target.parent.mkdir(parents=True, exist_ok=True)

    existing = _load_existing(target)
    new_data = _build_faction_manifest(project_root, faction, existing)
    new_text = _serialize(new_data)

    current = target.read_text(encoding="utf-8") if target.is_file() else ""
    if current == new_text:
        return False, target
    if not check_only:
        target.write_text(new_text, encoding="utf-8")
    return True, target


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Regenerate textures/sprites/sounds (and common fonts) "
                    "across every faction manifest."
    )
    ap.add_argument("project_root", type=Path)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any manifest would change; do not write")
    args = ap.parse_args()

    if not args.project_root.is_dir():
        print(f"[index] ERROR: not a directory: {args.project_root}", file=sys.stderr)
        return 1

    factions = _discover_factions(args.project_root)
    print(f"[index] factions: {', '.join(factions)}")

    any_changed = False
    try:
        for faction in factions:
            changed, target = _process_faction(
                args.project_root, faction, check_only=args.check
            )
            if changed:
                any_changed = True
                verb = "stale" if args.check else "wrote"
                print(f"[index] {verb}: {target}")
            else:
                print(f"[index] up to date: {target}")
    except RuntimeError as exc:
        print(f"[index] ERROR: {exc}", file=sys.stderr)
        return 1

    if args.check and any_changed:
        print("[index] manifests are out of sync with asset folders.", file=sys.stderr)
        print("[index] Run tools/asset_indexer/index.sh to regenerate.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
