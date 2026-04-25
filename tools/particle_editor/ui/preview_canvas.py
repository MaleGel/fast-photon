"""
Real-time particle preview.

CPU emulation of the engine's compute simulate pass:
  - per-frame Poisson-ish spawn (emit_rate * dt particles, with random
    fractional carry-over)
  - Euler integration: p += v * dt; v += g * dt
  - linear lerp of size + colour by time_alive / lifetime
  - hard cap at max_particles (oldest gets overwritten on overflow,
    matching the GPU ring-buffer semantics)

Render path:
  - When set_sprite() has supplied a pixmap, particles are drawn as a
    multiplicatively tinted scaled copy of that sprite — the same
    sampled * colour formula the engine's vfx.frag uses, just on the CPU.
  - Otherwise we fall back to coloured discs (cross-faction sprite refs
    or a missing texture take this branch).

Camera: fixed orthographic centred on the spawn point. Spawn is at world
origin so authors can ignore positioning entirely while tuning a system.
"""

from __future__ import annotations

import random
import time
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import QPointF, QRectF, Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QWidget


@dataclass
class ParticleSpec:
    """Mirrors the C++ ParticleSystem fields the preview cares about.
    Filled in by the EditorTab whenever a control changes."""
    max_particles: int = 256
    emit_rate: float = 16.0
    burst_count: int = 0
    lifetime_min: float = 1.0
    lifetime_max: float = 1.0
    velocity_min: tuple[float, float] = (0.0, 0.0)
    velocity_max: tuple[float, float] = (0.0, 0.0)
    gravity: tuple[float, float] = (0.0, 0.0)
    size_start: float = 0.5
    size_end: float = 0.0
    color_start: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    color_end:   tuple[float, float, float, float] = (1.0, 1.0, 1.0, 0.0)


@dataclass
class _Particle:
    x: float
    y: float
    vx: float
    vy: float
    life: float
    age: float = 0.0


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


class PreviewCanvas(QWidget):
    DEFAULT_PX_PER_UNIT = 80.0

    def __init__(self) -> None:
        super().__init__()
        self.setMinimumSize(400, 300)
        self.setStyleSheet("background: #1c1c20;")

        self._spec = ParticleSpec()
        self._particles: list[_Particle] = []
        self._spawn_carry = 0.0
        self._burst_pending = False
        self._paused = False
        self._last_time = time.monotonic()
        self._px_per_unit = self.DEFAULT_PX_PER_UNIT

        # Source sprite, set by EditorTab whenever the sprite combobox
        # changes. None → fallback to coloured-disc rendering.
        self._sprite_pixmap: Optional[QPixmap] = None

        # 60 Hz tick. Wall-clock dt rather than fixed 1/60 so the
        # preview behaves the same on machines that can't sustain it.
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(16)

    # ── Public ──────────────────────────────────────────────────────────────

    def set_spec(self, spec: ParticleSpec) -> None:
        """Apply new particle parameters. Doesn't clear live particles —
        switching a colour mid-flight then visibly transitions, which is
        the friendlier behaviour for incremental tweaking."""
        self._spec = spec
        if len(self._particles) > spec.max_particles:
            self._particles = self._particles[-spec.max_particles:]

    def set_sprite(self, pixmap: Optional[QPixmap]) -> None:
        """Swap the source sprite. None falls back to coloured discs."""
        self._sprite_pixmap = pixmap

    def reset(self) -> None:
        """Wipe live particles and re-fire the burst on next tick."""
        self._particles.clear()
        self._spawn_carry = 0.0
        self._burst_pending = self._spec.burst_count > 0

    def set_paused(self, paused: bool) -> None:
        self._paused = paused

    # ── Tick ────────────────────────────────────────────────────────────────

    def _tick(self) -> None:
        now = time.monotonic()
        dt = now - self._last_time
        self._last_time = now
        if self._paused:
            self.update()
            return
        self._advance(dt)
        self.update()

    def _advance(self, dt: float) -> None:
        s = self._spec

        if self._burst_pending and s.burst_count > 0:
            self._burst_pending = False
            for _ in range(s.burst_count):
                self._spawn_one()

        if s.emit_rate > 0.0:
            self._spawn_carry += s.emit_rate * dt
            while self._spawn_carry >= 1.0:
                self._spawn_carry -= 1.0
                self._spawn_one()

        new_list: list[_Particle] = []
        for p in self._particles:
            p.age += dt
            if p.age >= p.life:
                continue
            p.vx += s.gravity[0] * dt
            p.vy += s.gravity[1] * dt
            p.x  += p.vx * dt
            p.y  += p.vy * dt
            new_list.append(p)
        self._particles = new_list

    def _spawn_one(self) -> None:
        s = self._spec
        if len(self._particles) >= s.max_particles:
            self._particles.pop(0)
        life = random.uniform(s.lifetime_min, max(s.lifetime_min, s.lifetime_max))
        self._particles.append(_Particle(
            x=0.0, y=0.0,
            vx=random.uniform(s.velocity_min[0], s.velocity_max[0]),
            vy=random.uniform(s.velocity_min[1], s.velocity_max[1]),
            life=max(life, 0.0001),
        ))

    # ── Render ──────────────────────────────────────────────────────────────

    def paintEvent(self, _event) -> None:
        s = self._spec
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)

        painter.fillRect(self.rect(), QColor(28, 28, 32))

        cx = self.width() / 2.0
        cy = self.height() / 2.0

        sprite = self._sprite_pixmap
        sprite_w = sprite.width()  if sprite else 0
        sprite_h = sprite.height() if sprite else 0

        for p in self._particles:
            t = min(p.age / p.life, 1.0)
            size = max(_lerp(s.size_start, s.size_end, t), 0.0)
            r = max(0.0, min(1.0, _lerp(s.color_start[0], s.color_end[0], t)))
            g = max(0.0, min(1.0, _lerp(s.color_start[1], s.color_end[1], t)))
            b = max(0.0, min(1.0, _lerp(s.color_start[2], s.color_end[2], t)))
            a = max(0.0, min(1.0, _lerp(s.color_start[3], s.color_end[3], t)))

            sx = cx + p.x * self._px_per_unit
            sy = cy + p.y * self._px_per_unit
            half = size * self._px_per_unit * 0.5
            if half <= 0.0:
                continue

            if sprite is not None and sprite_w > 0 and sprite_h > 0:
                # Two-stage tint:
                #   1. drawPixmap — the sprite as-is, attenuated by the
                #      particle's alpha.
                #   2. fillRect with the tint colour and CompositionMode
                #      _Multiply, so each channel becomes (sample * tint).
                # Equivalent to the engine's `sampled * fragColor` modulo
                # the alpha-only attenuation we apply via setOpacity.
                rect = QRectF(sx - half, sy - half, half * 2.0, half * 2.0)
                painter.setOpacity(a)
                painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
                painter.drawPixmap(rect, sprite, QRectF(0, 0, sprite_w, sprite_h))
                # Multiply over the just-drawn rect. We only want the
                # tint to affect the sprite's footprint, so this fillRect
                # is bounded to the same rect; the multiply blends RGB
                # channels into what's already there.
                painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_Multiply)
                painter.fillRect(rect, QColor.fromRgbF(r, g, b, 1.0))
                # Reset for the next particle / overlays.
                painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
                painter.setOpacity(1.0)
            else:
                painter.setPen(Qt.PenStyle.NoPen)
                painter.setBrush(QBrush(QColor.fromRgbF(r, g, b, a)))
                painter.drawEllipse(QPointF(sx, sy), half, half)

        # Crosshair at the spawn point — orientation aid.
        painter.setOpacity(1.0)
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        painter.setPen(QPen(QColor(80, 80, 90), 1, Qt.PenStyle.DashLine))
        painter.drawLine(QPointF(cx - 20, cy), QPointF(cx + 20, cy))
        painter.drawLine(QPointF(cx, cy - 20), QPointF(cx, cy + 20))

        painter.setPen(QColor(180, 180, 195))
        text = f"{len(self._particles)} / {s.max_particles}"
        if self._paused:
            text += "  [PAUSED]"
        if sprite is None:
            text += "  (no sprite)"
        painter.drawText(8, 16, text)
