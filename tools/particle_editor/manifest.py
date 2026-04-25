"""
Read / write a fast-photon manifest while preserving any sections this
tool doesn't own. Same policy as tools/animation_editor — duplicated
intentionally so each tool stays self-contained until we have a clear
need for a shared module.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable


# Sections this editor edits. Anything else is preserved verbatim.
EDITED_SECTIONS = ("particle_systems",)


class Manifest:
    def __init__(self, path: Path):
        self.path = path
        self._raw: dict[str, Any] = {}
        self._dirty = False
        self._listeners: list[Callable[[bool], None]] = []
        self.load()

    # ── IO ──────────────────────────────────────────────────────────────────

    def load(self, path: Path | None = None) -> None:
        if path is not None:
            self.path = path
        if self.path.exists():
            with self.path.open("r", encoding="utf-8") as f:
                self._raw = json.load(f)
        else:
            self._raw = {}
        for section in EDITED_SECTIONS:
            self._raw.setdefault(section, [])
        self._set_dirty(False)

    def save(self) -> None:
        seen: set[str] = set()
        out: dict[str, Any] = {}
        for key, value in self._raw.items():
            out[key] = value
            seen.add(key)
        for key in EDITED_SECTIONS:
            if key not in seen:
                out[key] = self._raw[key]
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(out, f, indent=4)
            f.write("\n")
        self._set_dirty(False)

    # ── Edited-section accessors ───────────────────────────────────────────

    @property
    def particle_systems(self) -> list[dict[str, Any]]:
        return self._raw["particle_systems"]

    # Read-only accessors for cross-references the form needs.
    @property
    def sprites(self) -> list[dict[str, Any]]:
        return list(self._raw.get("sprites", []) or [])

    @property
    def textures(self) -> list[dict[str, Any]]:
        return list(self._raw.get("textures", []) or [])

    def sprite_names(self) -> list[str]:
        return [s.get("name", "") for s in self.sprites]

    # ── Dirty tracking ─────────────────────────────────────────────────────

    @property
    def dirty(self) -> bool:
        return self._dirty

    def mark_dirty(self) -> None:
        self._set_dirty(True)

    def add_dirty_listener(self, callback: Callable[[bool], None]) -> None:
        self._listeners.append(callback)

    def _set_dirty(self, value: bool) -> None:
        if self._dirty == value:
            return
        self._dirty = value
        for cb in list(self._listeners):
            try:
                cb(value)
            except Exception:
                pass
