"""
Manifest baker for fast-photon.

Consolidates the per-faction `assets/data/factions/<name>/assets.json` files
on top of the common `assets/assets.json`, applying namespacing rules and
strict validation, and writes a single `assets/assets.baked.json` that the
engine reads at startup.

Conventions:

    assets/assets.json
        Common — shaders, materials, fonts, music, anything that isn't
        owned by a specific faction. Names here are NOT prefixed (they
        live in the global namespace as-is).

    assets/data/factions/<faction>/assets.json
        Per-faction — textures, sprites, animations, animation_sets,
        sounds. Every locally-defined `name` is rewritten to
        `<faction>/<name>` in the output. Local references (sprite →
        texture, animation → frames, set state → animation, transition
        → target, …) are rewritten the same way.

Cross-faction / cross-common references:

    A name containing '/' is treated as already-qualified and left
    untouched. So a per-faction file may reference `common/ui_font` or
    `player/idle_anim` directly. Bake validates that the qualified name
    resolves to something in the consolidated namespace.

Validation is strict: any dangling reference fails the bake (non-zero
exit). The engine's loader assumes everything is resolvable, so a broken
manifest would otherwise blow up at runtime with worse diagnostics.

Usage:
    python bake.py <project_root>           # writes assets/assets.baked.json
    python bake.py <project_root> --check   # exit 1 if output is stale
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Iterable


# Sections we know how to merge + namespace. Order matters for predictable
# diffs in the baked file.
MERGED_SECTIONS = (
    "shaders", "fonts", "textures", "sprites", "materials",
    "sounds", "animations", "animation_sets", "particle_systems",
)


# ── Namespace rewrite ────────────────────────────────────────────────────────

def _qualify(name: str, faction: str) -> str:
    """If `name` is unqualified (no '/'), prepend `<faction>/`. Otherwise
    leave it as-is — it's a cross-faction reference."""
    if not name:
        return name
    if "/" in name:
        return name
    return f"{faction}/{name}"


def _qualify_list(names: Iterable[str], faction: str) -> list[str]:
    return [_qualify(n, faction) for n in names]


def _rewrite_faction_section(section: str, entries: list[dict],
                             faction: str) -> list[dict]:
    """Return new entries with names + cross-references qualified."""
    out: list[dict] = []
    for entry in entries:
        e = dict(entry)
        if "name" in e:
            e["name"] = _qualify(e["name"], faction)

        if section == "sprites":
            if "texture" in e:
                e["texture"] = _qualify(e["texture"], faction)
        elif section == "materials":
            for key in ("texture",):
                if key in e:
                    e[key] = _qualify(e[key], faction)
        elif section == "animations":
            if "frames" in e:
                e["frames"] = _qualify_list(e["frames"], faction)
        elif section == "animation_sets":
            states = dict(e.get("states") or {})
            for sname, state in list(states.items()):
                state = dict(state)
                if "animation" in state:
                    state["animation"] = _qualify(state["animation"], faction)
                if "next" in state:
                    state["next"] = _qualify(state["next"], faction)
                trs = []
                for tr in state.get("transitions", []) or []:
                    tr = dict(tr)
                    if "target" in tr:
                        tr["target"] = _qualify(tr["target"], faction)
                    trs.append(tr)
                if trs:
                    state["transitions"] = trs
                states[sname] = state
            e["states"] = states
            if "default" in e:
                e["default"] = _qualify(e["default"], faction)
            # Layout dict is keyed by *unqualified* state names — leave keys
            # alone (engine ignores _layout).
        elif section == "particle_systems":
            if "sprite" in e:
                e["sprite"] = _qualify(e["sprite"], faction)
        # Other sections (shaders, fonts, sounds, textures) need only the
        # 'name' field rewritten, which we already did above.
        out.append(e)
    return out


# ── Loading + merging ────────────────────────────────────────────────────────

def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _faction_files(project_root: Path) -> list[tuple[str, Path]]:
    """Return (faction_name, path) pairs for every factions/*/assets.json
    under the project. Sorted by faction name for deterministic output."""
    base = project_root / "assets" / "data" / "factions"
    if not base.is_dir():
        return []
    pairs: list[tuple[str, Path]] = []
    for sub in sorted(base.iterdir()):
        manifest = sub / "assets.json"
        if sub.is_dir() and manifest.is_file():
            pairs.append((sub.name, manifest))
    return pairs


def _bake(project_root: Path) -> dict[str, Any]:
    """Build the consolidated manifest in memory."""
    common_path = project_root / "assets" / "assets.json"
    common = _load_json(common_path) if common_path.exists() else {}

    # Start from common: every section copied verbatim, no rewriting.
    out: dict[str, list[Any]] = {sec: list(common.get(sec, []) or [])
                                 for sec in MERGED_SECTIONS}
    # Preserve any sections the common file has that we don't formally
    # know about (future-proofing — if someone adds a new top-level key,
    # we don't want bake to silently drop it).
    extras: dict[str, Any] = {
        k: v for k, v in common.items() if k not in MERGED_SECTIONS
    }

    for faction, manifest_path in _faction_files(project_root):
        doc = _load_json(manifest_path)
        for section in MERGED_SECTIONS:
            entries = doc.get(section) or []
            if not entries:
                continue
            out[section].extend(_rewrite_faction_section(section, entries, faction))

    # Reassemble in stable order: extras first (preserves common file's
    # human-curated layout), then merged sections.
    final: dict[str, Any] = {}
    for k, v in extras.items():
        final[k] = v
    for section in MERGED_SECTIONS:
        final[section] = out[section]
    return final


# ── Validation ───────────────────────────────────────────────────────────────

def _validate(doc: dict[str, Any]) -> list[str]:
    """Return a list of human-readable error strings. Empty = OK."""
    errors: list[str] = []

    sprites      = {s["name"]: s for s in doc.get("sprites", []) if "name" in s}
    textures     = {t["name"]    for t in doc.get("textures", []) if "name" in t}
    animations   = {a["name"]: a for a in doc.get("animations", []) if "name" in a}
    anim_sets    = {s["name"]: s for s in doc.get("animation_sets", []) if "name" in s}
    shaders      = {s["name"]    for s in doc.get("shaders", []) if "name" in s}

    # sprites → textures
    for name, s in sprites.items():
        tex = s.get("texture")
        if tex and tex not in textures:
            errors.append(f"sprite '{name}' references unknown texture '{tex}'")

    # materials → textures + shaders
    for m in doc.get("materials", []):
        mname = m.get("name", "?")
        for ref_field, pool, label in (("texture", textures, "texture"),
                                       ("vert", shaders, "shader"),
                                       ("frag", shaders, "shader")):
            ref = m.get(ref_field)
            if ref and ref not in pool:
                errors.append(
                    f"material '{mname}' references unknown {label} '{ref}'"
                )

    # animations → sprites
    for name, a in animations.items():
        for fr in a.get("frames", []) or []:
            if fr not in sprites:
                errors.append(
                    f"animation '{name}' references unknown sprite '{fr}'"
                )

    # animation_sets → animations + own states
    for sname, s in anim_sets.items():
        states = s.get("states", {}) or {}
        default = s.get("default")
        if default and default not in states:
            errors.append(
                f"animation_set '{sname}' default '{default}' is not a state"
            )
        for st_name, st in states.items():
            anim = st.get("animation")
            if anim and anim not in animations:
                errors.append(
                    f"animation_set '{sname}' state '{st_name}' "
                    f"references unknown animation '{anim}'"
                )
            nxt = st.get("next")
            if nxt and nxt not in states:
                errors.append(
                    f"animation_set '{sname}' state '{st_name}' "
                    f"next='{nxt}' is not a state in this set"
                )
            for tr in st.get("transitions", []) or []:
                target = tr.get("target")
                if target and target not in states:
                    errors.append(
                        f"animation_set '{sname}' state '{st_name}' "
                        f"transition '{tr.get('trigger', '?')}' "
                        f"target '{target}' is not a state in this set"
                    )

    # particle_systems → sprites (sprite ref must resolve)
    for ps in doc.get("particle_systems", []) or []:
        psname = ps.get("name", "?")
        sref = ps.get("sprite")
        if sref and sref not in sprites:
            errors.append(
                f"particle_system '{psname}' references unknown sprite '{sref}'"
            )

    return errors


# ── CLI ──────────────────────────────────────────────────────────────────────

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("project_root", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="exit 1 if the baked output would change")
    args = parser.parse_args(argv)

    doc = _bake(args.project_root)
    errors = _validate(doc)
    if errors:
        for e in errors:
            print(f"bake error: {e}", file=sys.stderr)
        return 1

    out_path = args.project_root / "assets" / "assets.baked.json"
    new_text = json.dumps(doc, indent=4) + "\n"

    if args.check:
        if not out_path.exists():
            print(f"bake error: {out_path} does not exist (run bake.py to generate)",
                  file=sys.stderr)
            return 1
        existing = out_path.read_text(encoding="utf-8")
        if existing != new_text:
            print(f"bake error: {out_path} is stale (run bake.py to update)",
                  file=sys.stderr)
            return 1
        return 0

    out_path.write_text(new_text, encoding="utf-8")
    print(f"baked {out_path}  "
          f"({sum(len(doc.get(s, [])) for s in MERGED_SECTIONS)} entries across "
          f"{len(MERGED_SECTIONS)} sections)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
