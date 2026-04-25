"""
"Animations" tab — assemble named animation tracks from existing sprites.

Layout:
    [ Existing animations ]  | [ Preview canvas        ]
    [ Sprite picker        ]  | [ Timeline (frames)     ]
    [ → Add to timeline    ]  | [ name / fps / save     ]

Preview is driven by a QTimer that ticks at min(60, fps*4) Hz to give a
smooth playhead even at low fps. Frame index advances based on real wall
time, so the preview shows the same timing as the engine's runtime.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QSize, Qt, QTimer
from PySide6.QtGui import QAction, QPixmap
from PySide6.QtWidgets import (
    QDoubleSpinBox, QFormLayout, QGroupBox, QHBoxLayout, QInputDialog,
    QLabel, QListWidget, QListWidgetItem, QMenu, QMessageBox, QPushButton,
    QSplitter, QVBoxLayout, QWidget,
)

from manifest import Manifest

from .thumbnail_cache import ThumbnailCache


# Item data role for storing the sprite name on a QListWidgetItem.
_SPRITE_NAME_ROLE = Qt.ItemDataRole.UserRole


class AnimationsTab(QWidget):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root
        self.thumbs = ThumbnailCache(manifest, project_root)

        # Playback state.
        self._playing = False
        self._playback_start = 0.0   # wall-clock seconds at last play()
        self._playback_frame = 0     # most recently displayed frame index

        self._build_ui()
        self._wire_signals()

        # Refresh-on-show: existing animations and sprite list need to be
        # populated from the current manifest.
        self.refresh_from_manifest()

        # Single timer drives preview repaint. Period chosen so we tick at
        # least four times per visible frame at the configured fps.
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick_preview)
        self._timer.start(16)  # ~60 Hz

    # ── UI ──────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # ── Left column ────────────────────────────────────────────────────
        self.existing_list = QListWidget()
        existing_box = QGroupBox("Existing animations")
        existing_layout = QVBoxLayout(existing_box)
        existing_layout.addWidget(self.existing_list)

        self.sprite_picker = QListWidget()
        # Bigger icons in the picker so artists can distinguish frames.
        self.sprite_picker.setIconSize(QSize(64, 64))
        self.sprite_picker.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)

        self.add_btn = QPushButton("→  Add to timeline")
        self.add_btn.setToolTip("Append the selected sprites as new frames.")

        self.add_xref_btn = QPushButton("+ Add cross-ref frame…")
        self.add_xref_btn.setToolTip(
            "Add a sprite from another faction (e.g. 'common/foo'). "
            "Cross-ref frames have no thumbnail because the sprite "
            "lives in a manifest we don't have open."
        )

        picker_box = QGroupBox("Sprite picker")
        picker_layout = QVBoxLayout(picker_box)
        picker_layout.addWidget(self.sprite_picker, stretch=1)
        picker_layout.addWidget(self.add_btn)
        picker_layout.addWidget(self.add_xref_btn)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addWidget(existing_box, stretch=1)
        left_layout.addWidget(picker_box, stretch=2)

        # ── Center column ──────────────────────────────────────────────────
        self.preview_label = QLabel()
        self.preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview_label.setMinimumHeight(256)
        self.preview_label.setStyleSheet("background: #222;")
        self.preview_label.setText("(empty timeline)")

        self.play_btn  = QPushButton("Play")
        self.stop_btn  = QPushButton("Stop")
        self.stop_btn.setEnabled(False)

        playback_row = QHBoxLayout()
        playback_row.addWidget(self.play_btn)
        playback_row.addWidget(self.stop_btn)
        playback_row.addStretch()

        # Timeline as a horizontal icon list. Each item carries its sprite
        # name in UserRole; reorder by removing+inserting.
        self.timeline = QListWidget()
        self.timeline.setFlow(QListWidget.Flow.LeftToRight)
        self.timeline.setIconSize(QSize(64, 64))
        self.timeline.setMovement(QListWidget.Movement.Static)
        self.timeline.setFixedHeight(96)
        self.timeline.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)

        # Meta fields.
        self.name_edit = QListWidget()  # placeholder — replaced below
        from PySide6.QtWidgets import QLineEdit
        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText("warrior_walk")

        self.fps_spin = QDoubleSpinBox()
        self.fps_spin.setRange(0.1, 120.0)
        self.fps_spin.setDecimals(1)
        self.fps_spin.setSingleStep(0.5)
        self.fps_spin.setValue(8.0)

        meta_box = QGroupBox("Animation")
        meta_form = QFormLayout(meta_box)
        meta_form.addRow("Name", self.name_edit)
        meta_form.addRow("FPS",  self.fps_spin)

        self.save_btn = QPushButton("Save animation")
        self.save_btn.setStyleSheet("font-weight: bold; padding: 6px;")

        center = QWidget()
        center_layout = QVBoxLayout(center)
        center_layout.addWidget(self.preview_label, stretch=1)
        center_layout.addLayout(playback_row)
        center_layout.addWidget(QLabel("Timeline:"))
        center_layout.addWidget(self.timeline)
        center_layout.addWidget(meta_box)
        center_layout.addWidget(self.save_btn)

        # ── Splitter ────────────────────────────────────────────────────────
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left)
        splitter.addWidget(center)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 3)

        outer = QHBoxLayout(self)
        outer.addWidget(splitter)

    def _wire_signals(self) -> None:
        self.add_btn.clicked.connect(self._on_add_selected)
        self.add_xref_btn.clicked.connect(self._on_add_xref_frame)
        self.sprite_picker.itemDoubleClicked.connect(
            lambda _: self._on_add_selected()
        )

        self.play_btn.clicked.connect(self._on_play)
        self.stop_btn.clicked.connect(self._on_stop)

        self.timeline.customContextMenuRequested.connect(self._timeline_context_menu)

        self.fps_spin.valueChanged.connect(self._on_fps_changed)

        self.save_btn.clicked.connect(self._on_save)
        self.existing_list.itemDoubleClicked.connect(self._on_load_existing)

    # ── Manifest ↔ UI sync ──────────────────────────────────────────────────

    def refresh_from_manifest(self) -> None:
        self.thumbs.invalidate()

        # Sprite picker: every sprite gets a thumbnail entry.
        self.sprite_picker.clear()
        for s in self.manifest.sprites:
            name = s.get("name", "")
            if not name:
                continue
            item = QListWidgetItem(name)
            pix = self.thumbs.get(name)
            if pix is not None:
                item.setIcon(pix)
            item.setData(_SPRITE_NAME_ROLE, name)
            self.sprite_picker.addItem(item)

        # Existing animations list.
        self.existing_list.clear()
        for a in self.manifest.animations:
            name = a.get("name", "")
            frames = a.get("frames", [])
            fps = a.get("fps", 8.0)
            self.existing_list.addItem(f"{name}    ({len(frames)} frames @ {fps} fps)")

    # ── Timeline operations ─────────────────────────────────────────────────

    def _on_add_selected(self) -> None:
        for item in self.sprite_picker.selectedItems():
            name = item.data(_SPRITE_NAME_ROLE)
            self._append_timeline_frame(name)

    def _append_timeline_frame(self, sprite_name: str) -> None:
        item = QListWidgetItem(sprite_name)
        pix = self.thumbs.get(sprite_name)
        if pix is not None:
            item.setIcon(pix)
        else:
            # Cross-ref or stale local id — no thumbnail. Mark visually so
            # the user notices it isn't being previewed locally.
            item.setToolTip(
                f"'{sprite_name}' isn't in this manifest; will be resolved "
                f"by the bake step."
            )
        item.setData(_SPRITE_NAME_ROLE, sprite_name)
        self.timeline.addItem(item)

    def _on_add_xref_frame(self) -> None:
        text, ok = QInputDialog.getText(
            self, "Add cross-ref frame",
            "Qualified sprite id (e.g. 'common/foo'):",
        )
        if not ok:
            return
        text = text.strip()
        if not text:
            return
        if "/" not in text:
            QMessageBox.warning(
                self, "Not a cross-ref",
                "Cross-ref names must contain '/'. For local sprites, "
                "use the picker above instead.",
            )
            return
        self._append_timeline_frame(text)
        self._refresh_frame_names()

    def _timeline_context_menu(self, pos) -> None:
        item = self.timeline.itemAt(pos)
        if item is None:
            return
        row = self.timeline.row(item)

        menu = QMenu(self)
        act_left  = QAction("Move left",  self)
        act_right = QAction("Move right", self)
        act_remove = QAction("Remove", self)

        act_left.setEnabled(row > 0)
        act_right.setEnabled(row < self.timeline.count() - 1)

        act_left.triggered.connect (lambda: self._move_timeline_item(row, row - 1))
        act_right.triggered.connect(lambda: self._move_timeline_item(row, row + 1))
        act_remove.triggered.connect(lambda: self._remove_timeline_item(row))

        menu.addAction(act_left)
        menu.addAction(act_right)
        menu.addSeparator()
        menu.addAction(act_remove)
        menu.exec(self.timeline.viewport().mapToGlobal(pos))

    def _move_timeline_item(self, src: int, dst: int) -> None:
        item = self.timeline.takeItem(src)
        if item is None:
            return
        self.timeline.insertItem(dst, item)
        self.timeline.setCurrentRow(dst)

    def _remove_timeline_item(self, row: int) -> None:
        self.timeline.takeItem(row)

    def _timeline_sprite_names(self) -> list[str]:
        return [
            self.timeline.item(i).data(_SPRITE_NAME_ROLE)
            for i in range(self.timeline.count())
        ]

    # ── Preview ─────────────────────────────────────────────────────────────

    def _on_play(self) -> None:
        if self.timeline.count() == 0:
            return
        self._playing = True
        self._playback_start = time.monotonic()
        self._playback_frame = -1   # forces immediate redraw
        self.play_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)

    def _on_stop(self) -> None:
        self._playing = False
        self._playback_frame = 0
        self.play_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self._draw_preview_frame(0)

    def _on_fps_changed(self) -> None:
        # Restart playback timing so the new fps takes effect on next tick.
        if self._playing:
            self._playback_start = time.monotonic()
            self._playback_frame = -1

    def _tick_preview(self) -> None:
        count = self.timeline.count()
        if count == 0:
            self.preview_label.setText("(empty timeline)")
            self.preview_label.setPixmap(QPixmap())
            return
        if not self._playing:
            # Static preview shows the currently selected frame, or frame 0.
            row = max(0, self.timeline.currentRow())
            if row != self._playback_frame:
                self._draw_preview_frame(row)
                self._playback_frame = row
            return

        fps = max(0.1, self.fps_spin.value())
        elapsed = time.monotonic() - self._playback_start
        idx = int(elapsed * fps) % count
        if idx != self._playback_frame:
            self._draw_preview_frame(idx)
            self._playback_frame = idx

    def _draw_preview_frame(self, idx: int) -> None:
        if idx < 0 or idx >= self.timeline.count():
            return
        sprite_name = self.timeline.item(idx).data(_SPRITE_NAME_ROLE)
        pix = self.thumbs.get(sprite_name)
        if pix is None:
            self.preview_label.setText(f"(no thumbnail for '{sprite_name}')")
            return
        # Scale up to fill the preview area while keeping aspect.
        target = self.preview_label.size()
        scaled = pix.scaled(
            target,
            Qt.AspectRatioMode.KeepAspectRatio,
            # Nearest-neighbour preserves pixel-art look. If artists ever
            # need smooth scaling, we can expose a toggle.
            Qt.TransformationMode.FastTransformation,
        )
        self.preview_label.setPixmap(scaled)

    # ── Save / load ─────────────────────────────────────────────────────────

    def _on_save(self) -> None:
        name = self.name_edit.text().strip()
        if not name:
            QMessageBox.warning(self, "Name missing", "Animation name is required.")
            return
        frames = self._timeline_sprite_names()
        if not frames:
            QMessageBox.warning(self, "Empty timeline",
                                "Add at least one frame before saving.")
            return

        fps = float(self.fps_spin.value())

        # Upsert by name in manifest.animations.
        target: Optional[dict] = None
        for entry in self.manifest.animations:
            if entry.get("name") == name:
                target = entry
                break
        if target is None:
            target = {"name": name}
            self.manifest.animations.append(target)
        target["frames"] = frames
        target["fps"]    = fps

        self.manifest.mark_dirty()
        self.refresh_from_manifest()

        win = self.window()
        if hasattr(win, "save_manifest"):
            if win.save_manifest():
                win.statusBar().showMessage(
                    f"Saved animation '{name}' ({len(frames)} frames @ {fps} fps) to disk.",
                    5000,
                )

    def _on_load_existing(self, item: QListWidgetItem) -> None:
        # The visible label is "name    (N frames @ F fps)" — first token is
        # the canonical name.
        text = item.text()
        name = text.split()[0] if text else ""
        entry = next((a for a in self.manifest.animations if a.get("name") == name), None)
        if entry is None:
            return

        self.name_edit.setText(name)
        self.fps_spin.setValue(float(entry.get("fps", 8.0)))

        self.timeline.clear()
        for sprite_name in entry.get("frames", []):
            self._append_timeline_frame(sprite_name)

        self._on_stop()  # reset playback state, draw frame 0

    # ── External hooks ──────────────────────────────────────────────────────

    def on_manifest_reloaded(self) -> None:
        self.timeline.clear()
        self.name_edit.setText("")
        self.fps_spin.setValue(8.0)
        self._on_stop()
        self.refresh_from_manifest()
