"""
Loads texture PNGs once and serves cropped sprite QPixmaps on demand.
Same texture is reused across every sprite that references it; same sprite
is cached after the first crop.

Lookup is by sprite name → QPixmap. The cache pulls coordinates from the
manifest the first time a sprite is requested.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtGui import QPixmap

from manifest import Manifest


class ThumbnailCache:
    def __init__(self, manifest: Manifest, project_root: Path):
        self.manifest = manifest
        self.project_root = project_root

        # Two-level cache: full texture pixmap + per-sprite crop.
        # Cleared when the user reloads the manifest, since paths and rects
        # may have changed.
        self._textures: dict[str, QPixmap] = {}   # texture name → full pixmap
        self._sprites:  dict[str, QPixmap] = {}   # sprite  name → cropped pixmap

    # ── Public ──────────────────────────────────────────────────────────────

    def get(self, sprite_name: str) -> Optional[QPixmap]:
        cached = self._sprites.get(sprite_name)
        if cached is not None:
            return cached

        sprite_entry = self._find(self.manifest.sprites, sprite_name)
        if sprite_entry is None:
            return None
        tex_name = sprite_entry.get("texture", "")
        tex_pix = self._load_texture(tex_name)
        if tex_pix is None:
            return None

        x = int(sprite_entry.get("x", 0))
        y = int(sprite_entry.get("y", 0))
        w = int(sprite_entry.get("w", tex_pix.width()))
        h = int(sprite_entry.get("h", tex_pix.height()))

        # Clamp the crop to the texture bounds — out-of-bounds Qt copy() is
        # silently empty, which would just show as a blank thumbnail.
        x = max(0, min(x, tex_pix.width() - 1))
        y = max(0, min(y, tex_pix.height() - 1))
        w = max(1, min(w, tex_pix.width()  - x))
        h = max(1, min(h, tex_pix.height() - y))

        crop = tex_pix.copy(x, y, w, h)
        self._sprites[sprite_name] = crop
        return crop

    def invalidate(self) -> None:
        self._textures.clear()
        self._sprites.clear()

    # ── Internal ────────────────────────────────────────────────────────────

    def _find(self, entries: list[dict], name: str) -> Optional[dict]:
        for e in entries:
            if e.get("name") == name:
                return e
        return None

    def _load_texture(self, name: str) -> Optional[QPixmap]:
        cached = self._textures.get(name)
        if cached is not None:
            return cached

        tex_entry = self._find(self.manifest.textures, name)
        if tex_entry is None:
            return None
        rel_path = tex_entry.get("path", "")
        full_path = self.project_root / rel_path
        pix = QPixmap(str(full_path))
        if pix.isNull():
            return None
        self._textures[name] = pix
        return pix
