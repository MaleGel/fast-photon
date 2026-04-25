"""
"Preview" tab — full state-machine playback.

Mirrors what AnimationSystem does at runtime:
  - Picks the current state's animation, advances time at its declared fps.
  - Looping animations wrap; one-shots auto-advance to the state's `next`
    field on completion.
  - Triggers are surfaced as a row of buttons populated from the current
    state's transitions list. Clicking a button switches state, exactly
    like AnimationSystem::trigger would in-engine.

A "Force state" dropdown bypasses transitions entirely for debugging
states that are unreachable via the default-state triggers chain.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QComboBox, QFormLayout, QGroupBox, QHBoxLayout, QLabel,
    QPushButton, QVBoxLayout, QWidget,
)

from manifest import Manifest

from .thumbnail_cache import ThumbnailCache


class PreviewTab(QWidget):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root
        self.thumbs = ThumbnailCache(manifest, project_root)

        # Currently-selected set + state. None when no set is loaded.
        self._set_data: Optional[dict] = None
        self._current_state: Optional[str] = None
        self._state_start_t: float = 0.0

        self._build_ui()
        self._wire_signals()
        self.refresh_from_manifest()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(16)  # ~60 Hz

    # ── UI ──────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # Top row: set picker + reset.
        self.set_combo = QComboBox()
        self.set_combo.setMinimumWidth(220)

        self.reset_btn = QPushButton("Reset to default")

        top_box = QGroupBox("Animation set")
        top_form = QFormLayout(top_box)
        top_form.addRow("Set", self.set_combo)
        top_form.addRow("",    self.reset_btn)

        # Preview canvas.
        self.canvas = QLabel()
        self.canvas.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.canvas.setMinimumHeight(360)
        self.canvas.setStyleSheet("background: #222;")
        self.canvas.setText("(select a set)")

        # State display + trigger row + force.
        self.state_label = QLabel("State: —")
        self.state_label.setStyleSheet(
            "color: #cde; font-size: 14px; font-weight: bold; padding: 4px;"
        )

        self.triggers_box = QGroupBox("Triggers (click to fire)")
        self._triggers_layout = QHBoxLayout(self.triggers_box)
        self._triggers_layout.addStretch()  # placeholder so empty box looks tidy

        self.force_combo = QComboBox()
        self.force_combo.setMinimumWidth(180)
        force_box = QGroupBox("Force state (debug)")
        force_form = QFormLayout(force_box)
        force_form.addRow("Jump to", self.force_combo)

        # Layout.
        outer = QVBoxLayout(self)
        outer.addWidget(top_box)
        outer.addWidget(self.canvas, stretch=1)
        outer.addWidget(self.state_label)
        outer.addWidget(self.triggers_box)
        outer.addWidget(force_box)

    def _wire_signals(self) -> None:
        self.set_combo.currentIndexChanged.connect(self._on_set_changed)
        self.reset_btn.clicked.connect(self._on_reset)
        self.force_combo.activated.connect(self._on_force_state)

    # ── Manifest sync ───────────────────────────────────────────────────────

    def refresh_from_manifest(self) -> None:
        self.thumbs.invalidate()

        # Repopulate set dropdown.
        prev = self.set_combo.currentText()
        self.set_combo.blockSignals(True)
        self.set_combo.clear()
        for s in self.manifest.animation_sets:
            name = s.get("name", "")
            if name:
                self.set_combo.addItem(name)
        # Keep the previous selection if still around; otherwise pick first.
        if prev:
            idx = self.set_combo.findText(prev)
            if idx >= 0:
                self.set_combo.setCurrentIndex(idx)
        self.set_combo.blockSignals(False)

        # Apply initial selection (or clear if no sets exist).
        self._on_set_changed()

    # ── Set / state management ──────────────────────────────────────────────

    def _on_set_changed(self) -> None:
        name = self.set_combo.currentText()
        self._set_data = next(
            (s for s in self.manifest.animation_sets if s.get("name") == name),
            None,
        )
        # Repopulate force-state dropdown to match.
        self.force_combo.clear()
        if self._set_data is not None:
            for state_name in (self._set_data.get("states") or {}):
                self.force_combo.addItem(state_name)
        self._on_reset()

    def _on_reset(self) -> None:
        if self._set_data is None:
            self._current_state = None
            self.state_label.setText("State: —")
            self.canvas.setText("(no set selected)")
            self.canvas.setPixmap(QPixmap())
            self._rebuild_trigger_buttons()
            return
        default = self._set_data.get("default", "")
        states  = self._set_data.get("states", {}) or {}
        if default not in states:
            # Fall back to the first state if default is missing/broken.
            default = next(iter(states), "")
        self._enter_state(default)

    def _enter_state(self, state_name: str) -> None:
        if self._set_data is None or not state_name:
            return
        states = self._set_data.get("states", {}) or {}
        if state_name not in states:
            return
        self._current_state = state_name
        self._state_start_t = time.monotonic()
        self.state_label.setText(f"State: {state_name}")
        self._rebuild_trigger_buttons()

    def _on_force_state(self, idx: int) -> None:
        name = self.force_combo.itemText(idx)
        if name:
            self._enter_state(name)

    # ── Trigger buttons ─────────────────────────────────────────────────────

    def _rebuild_trigger_buttons(self) -> None:
        # Wipe everything except the trailing stretch — Qt layouts don't have
        # a clear-all that preserves stretch items.
        while self._triggers_layout.count() > 1:
            item = self._triggers_layout.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()

        if self._set_data is None or self._current_state is None:
            return
        state = self._set_data.get("states", {}).get(self._current_state, {})
        for tr in state.get("transitions", []) or []:
            trig = tr.get("trigger", "")
            target = tr.get("target", "")
            if not trig or not target:
                continue
            btn = QPushButton(f"{trig}  →  {target}")
            btn.clicked.connect(lambda _=False, t=target: self._enter_state(t))
            # Insert before the trailing stretch (index = count - 1).
            self._triggers_layout.insertWidget(self._triggers_layout.count() - 1, btn)

    # ── Playback tick ───────────────────────────────────────────────────────

    def _tick(self) -> None:
        if self._set_data is None or self._current_state is None:
            return
        states = self._set_data.get("states", {}) or {}
        state  = states.get(self._current_state)
        if state is None:
            return

        anim_name = state.get("animation", "")
        anim = next(
            (a for a in self.manifest.animations if a.get("name") == anim_name),
            None,
        )
        if anim is None:
            self.canvas.setText(f"(animation '{anim_name}' missing)")
            return

        frames = anim.get("frames", []) or []
        if not frames:
            return
        fps = max(0.1, float(anim.get("fps", 8.0)))
        frame_dur = 1.0 / fps
        total_dur = frame_dur * len(frames)

        elapsed = time.monotonic() - self._state_start_t
        loop = bool(state.get("loop", True))

        if loop:
            wrapped = elapsed % total_dur
            idx = int(wrapped / frame_dur)
        else:
            if elapsed >= total_dur:
                # One-shot complete: auto-advance to 'next' if defined,
                # otherwise hold the last frame.
                nxt = state.get("next")
                if nxt and nxt in states:
                    self._enter_state(nxt)
                    return
                idx = len(frames) - 1
            else:
                idx = int(elapsed / frame_dur)
        idx = min(idx, len(frames) - 1)

        sprite_name = frames[idx]
        pix = self.thumbs.get(sprite_name)
        if pix is None:
            self.canvas.setText(f"(sprite '{sprite_name}' missing)")
            return
        scaled = pix.scaled(
            self.canvas.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )
        self.canvas.setPixmap(scaled)

    # ── External hooks ──────────────────────────────────────────────────────

    def on_manifest_reloaded(self) -> None:
        self.refresh_from_manifest()
