"""
Read / write fast-photon's assets/assets.json while preserving any sections
this tool doesn't own. Mirrors the policy used by tools/asset_indexer/index.py.

Sections we edit:
    textures, sprites, animations, animation_sets

Sections we leave alone (passed through verbatim, in their original order):
    everything else (shaders, materials, fonts, sounds, ...).
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable


# Sections that this editor reads and rewrites. Anything else is preserved
# unchanged. Ordering here is also the order we use when emitting our
# sections — readers don't care, but humans do when diffing.
EDITED_SECTIONS = ("textures", "sprites", "animations", "animation_sets")


class Manifest:
    """In-memory view of assets.json with helpers for our edited sections.

    Tracks a `dirty` flag that callers raise via mark_dirty() after every
    mutation; load() / save() clear it. The MainWindow subscribes via
    add_dirty_listener() so it can render an unsaved indicator and prompt
    on close.
    """

    def __init__(self, path: Path):
        self.path = path
        self._raw: dict[str, Any] = {}
        self._dirty = False
        self._listeners: list[Callable[[bool], None]] = []
        self.load()

    # ── IO ──────────────────────────────────────────────────────────────────

    def load(self, path: Path | None = None) -> None:
        """Replace the in-memory document with whatever's on disk. If `path`
        is given, switch to that file (used by File→Open); otherwise just
        re-read self.path. Either way we end up clean (dirty=False)."""
        if path is not None:
            self.path = path
        if self.path.exists():
            with self.path.open("r", encoding="utf-8") as f:
                self._raw = json.load(f)
        else:
            self._raw = {}

        # Make sure every section we're going to read exists, so callers don't
        # have to .get(...) everywhere. If they're absent in the file we'll
        # write them on save — which is fine: empty arrays are valid.
        for section in EDITED_SECTIONS:
            self._raw.setdefault(section, [])

        self._set_dirty(False)

    def save(self) -> None:
        # Re-emit the document with: 1) every original section in its
        # original order, 2) any edited sections we haven't seen yet
        # appended at the end. This preserves human-friendly order for
        # diffing while still being safe against missing sections.
        seen: set[str] = set()
        out: dict[str, Any] = {}
        for key, value in self._raw.items():
            out[key] = value
            seen.add(key)
        for key in EDITED_SECTIONS:
            if key not in seen:
                out[key] = self._raw[key]

        # 4-space indent matches what asset_indexer produces.
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(out, f, indent=4)
            f.write("\n")
        self._set_dirty(False)

    # ── Edited-section accessors ───────────────────────────────────────────
    # Returns the live list — mutate it in place to record changes.

    @property
    def textures(self) -> list[dict[str, Any]]:
        return self._raw["textures"]

    @property
    def sprites(self) -> list[dict[str, Any]]:
        return self._raw["sprites"]

    @property
    def animations(self) -> list[dict[str, Any]]:
        return self._raw["animations"]

    @property
    def animation_sets(self) -> list[dict[str, Any]]:
        return self._raw["animation_sets"]

    # ── Dirty tracking ──────────────────────────────────────────────────────

    @property
    def dirty(self) -> bool:
        return self._dirty

    def mark_dirty(self) -> None:
        """Tabs call this after any in-memory mutation (add_state, etc.)."""
        self._set_dirty(True)

    def add_dirty_listener(self, callback: Callable[[bool], None]) -> None:
        """Notified whenever the dirty flag changes (False ↔ True)."""
        self._listeners.append(callback)

    def _set_dirty(self, value: bool) -> None:
        if self._dirty == value:
            return
        self._dirty = value
        for cb in list(self._listeners):
            try:
                cb(value)
            except Exception:
                # Listeners shouldn't take down a save/load — swallow.
                pass

    # ── Helpers ─────────────────────────────────────────────────────────────

    def upsert_texture(self, name: str, path: str) -> None:
        """Add or update a texture entry by name. Marks the manifest dirty."""
        for entry in self.textures:
            if entry.get("name") == name:
                if entry.get("path") != path:
                    entry["path"] = path
                    self.mark_dirty()
                return
        self.textures.append({"name": name, "path": path})
        self.mark_dirty()

    def upsert_sprite(self, name: str, texture: str,
                      x: int, y: int, w: int, h: int) -> None:
        """Add or update a sprite entry by name. Marks the manifest dirty."""
        new_data = {"texture": texture, "x": x, "y": y, "w": w, "h": h}
        for entry in self.sprites:
            if entry.get("name") == name:
                changed = any(entry.get(k) != v for k, v in new_data.items())
                if changed:
                    entry.update(new_data)
                    self.mark_dirty()
                return
        self.sprites.append({"name": name, **new_data})
        self.mark_dirty()

    def texture_names(self) -> list[str]:
        return [t.get("name", "") for t in self.textures]

    def sprite_names(self) -> list[str]:
        return [s.get("name", "") for s in self.sprites]
