"""
"Sprites" tab.

Workflow:
  1. Load PNG  → image appears in the canvas.
  2. Pick mode:
       Auto    — set rows / cols / padding / spacing → frames are generated
                 by even subdivision.
       Manual  — drag rectangles directly on the canvas; each drag adds one
                 frame.
  3. Set frame name prefix (e.g. "warrior_walk") → list shows generated
     names "warrior_walk_00", "warrior_walk_01", ...
  4. Set texture name (single texture entry) and texture path (relative to
     repo root, e.g. "assets/textures/warrior.png").
  5. Click "Add to manifest" → upserts the texture and one sprite per frame.
  6. File → Save (Ctrl+S) writes assets.json.

The list of frames lives on this tab. The canvas reads it via set_frames()
to render overlays.
"""

from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QButtonGroup, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QListWidget, QMessageBox, QPushButton, QRadioButton,
    QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from manifest import Manifest

from .sheet_canvas import SheetCanvas


class SheetSlicerTab(QWidget):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root

        # Frames currently displayed. Each entry: {"x", "y", "w", "h"}.
        self._frames: list[dict] = []

        self._build_ui()
        self._wire_signals()
        self._refresh_frame_names()

    # ── UI ──────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # Left side: canvas with the sheet.
        self.canvas = SheetCanvas()

        # Right side: stacked control groups.
        self.load_btn = QPushButton("Load PNG…")
        self.image_label = QLabel("(no image loaded)")
        self.image_label.setStyleSheet("color: #888;")

        # Mode selector.
        self.auto_radio   = QRadioButton("Auto grid")
        self.manual_radio = QRadioButton("Manual draw")
        self.auto_radio.setChecked(True)
        mode_group = QButtonGroup(self)
        mode_group.addButton(self.auto_radio)
        mode_group.addButton(self.manual_radio)

        mode_box = QGroupBox("Mode")
        mode_layout = QHBoxLayout(mode_box)
        mode_layout.addWidget(self.auto_radio)
        mode_layout.addWidget(self.manual_radio)

        # Auto-grid parameters.
        self.cols_spin    = QSpinBox(); self.cols_spin.setRange(1, 256); self.cols_spin.setValue(4)
        self.rows_spin    = QSpinBox(); self.rows_spin.setRange(1, 256); self.rows_spin.setValue(1)
        self.padding_spin = QSpinBox(); self.padding_spin.setRange(0, 1024); self.padding_spin.setValue(0)
        self.spacing_spin = QSpinBox(); self.spacing_spin.setRange(0, 1024); self.spacing_spin.setValue(0)

        auto_box = QGroupBox("Auto grid")
        auto_form = QFormLayout(auto_box)
        auto_form.addRow("Cols",    self.cols_spin)
        auto_form.addRow("Rows",    self.rows_spin)
        auto_form.addRow("Padding (outer)", self.padding_spin)
        auto_form.addRow("Spacing (inner)", self.spacing_spin)

        # Naming.
        self.texture_name_edit = QLineEdit()
        self.texture_name_edit.setPlaceholderText("warrior")
        self.texture_path_edit = QLineEdit()
        self.texture_path_edit.setPlaceholderText("assets/textures/warrior.png")
        self.prefix_edit = QLineEdit()
        self.prefix_edit.setPlaceholderText("warrior_walk")

        naming_box = QGroupBox("Naming")
        naming_form = QFormLayout(naming_box)
        naming_form.addRow("Texture name", self.texture_name_edit)
        naming_form.addRow("Texture path", self.texture_path_edit)
        naming_form.addRow("Sprite prefix", self.prefix_edit)

        # Frame list (read-only display of generated names).
        self.frame_list = QListWidget()
        self.clear_btn  = QPushButton("Clear frames")

        list_box = QGroupBox("Frames")
        list_layout = QVBoxLayout(list_box)
        list_layout.addWidget(self.frame_list)
        list_layout.addWidget(self.clear_btn)

        # Action buttons.
        self.add_btn = QPushButton("Add to manifest")
        self.add_btn.setStyleSheet("font-weight: bold; padding: 6px;")

        # Right column.
        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.addWidget(self.load_btn)
        right_layout.addWidget(self.image_label)
        right_layout.addWidget(mode_box)
        right_layout.addWidget(auto_box)
        right_layout.addWidget(naming_box)
        right_layout.addWidget(list_box, stretch=1)
        right_layout.addWidget(self.add_btn)

        # Splitter so user can resize side panel.
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.canvas)
        splitter.addWidget(right)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 1)

        outer = QHBoxLayout(self)
        outer.addWidget(splitter)

    def _wire_signals(self) -> None:
        self.load_btn.clicked.connect(self._on_load)

        self.auto_radio.toggled.connect(self._on_mode_changed)
        self.manual_radio.toggled.connect(self._on_mode_changed)

        for spin in (self.cols_spin, self.rows_spin,
                     self.padding_spin, self.spacing_spin):
            spin.valueChanged.connect(self._regenerate_auto_frames)

        self.prefix_edit.textChanged.connect(self._refresh_frame_names)

        self.canvas.manual_rect_drawn.connect(self._on_manual_rect)

        self.clear_btn.clicked.connect(self._on_clear_frames)
        self.add_btn.clicked.connect(self._on_add_to_manifest)

    # ── Loading ─────────────────────────────────────────────────────────────

    def _on_load(self) -> None:
        # Default to assets/textures/ if it exists; otherwise repo root.
        start = self.project_root / "assets" / "textures"
        if not start.exists():
            start = self.project_root

        path, _ = QFileDialog.getOpenFileName(
            self, "Load sprite sheet",
            str(start),
            "Images (*.png *.jpg *.jpeg)",
        )
        if not path:
            return

        if not self.canvas.load_image(path):
            QMessageBox.critical(self, "Load failed", f"Could not open '{path}'.")
            return

        # Auto-fill texture path with a repo-relative path when possible —
        # makes the common case zero-typing.
        try:
            rel = os.path.relpath(path, self.project_root).replace("\\", "/")
            self.texture_path_edit.setText(rel)

            # Suggest a texture name from the file stem.
            stem = Path(path).stem
            if not self.texture_name_edit.text():
                self.texture_name_edit.setText(stem)
            if not self.prefix_edit.text():
                self.prefix_edit.setText(stem)
        except ValueError:
            # rel path can fail across drives on Windows — leave fields alone.
            pass

        w, h = self.canvas.image_size()
        self.image_label.setText(f"{Path(path).name}  ({w}×{h})")
        self._regenerate_auto_frames()

    # ── Mode switching ──────────────────────────────────────────────────────

    def _on_mode_changed(self) -> None:
        manual = self.manual_radio.isChecked()
        self.canvas.set_manual_mode(manual)
        # When switching modes we don't auto-clear — user might want to keep
        # auto-frames and add manual ones, or vice versa. Clear button is
        # explicit if they want a fresh start.
        if not manual:
            # Switching back to Auto regenerates from current params, since
            # any manual frames would be inconsistent with the controls.
            self._regenerate_auto_frames()

    # ── Frame generation ────────────────────────────────────────────────────

    def _regenerate_auto_frames(self) -> None:
        if not self.auto_radio.isChecked():
            return
        if not self.canvas.has_image():
            return

        img_w, img_h = self.canvas.image_size()
        cols = self.cols_spin.value()
        rows = self.rows_spin.value()
        pad  = self.padding_spin.value()
        gap  = self.spacing_spin.value()

        # Available area after stripping outer padding.
        usable_w = img_w - 2 * pad
        usable_h = img_h - 2 * pad
        if usable_w <= 0 or usable_h <= 0:
            self._set_frames([])
            return

        # Each frame is (usable - (n-1)*gap) / n. Integer division means
        # the last column/row may exclude the final stray pixels — that
        # matches what artists usually intend with even grids.
        frame_w = (usable_w - (cols - 1) * gap) // cols
        frame_h = (usable_h - (rows - 1) * gap) // rows
        if frame_w <= 0 or frame_h <= 0:
            self._set_frames([])
            return

        frames = []
        for r in range(rows):
            for c in range(cols):
                x = pad + c * (frame_w + gap)
                y = pad + r * (frame_h + gap)
                frames.append({"x": x, "y": y, "w": frame_w, "h": frame_h})
        self._set_frames(frames)

    def _on_manual_rect(self, x: int, y: int, w: int, h: int) -> None:
        # Manual draw appends — switching to Manual is an "additive" mode.
        # Auto frames stay (the canvas keeps drawing them); user can hit
        # Clear to start fresh.
        self._frames.append({"x": x, "y": y, "w": w, "h": h})
        self.canvas.set_frames(self._frames)
        self._refresh_frame_names()

    def _on_clear_frames(self) -> None:
        self._frames = []
        self.canvas.set_frames([])
        self._refresh_frame_names()

    def _set_frames(self, frames: list[dict]) -> None:
        self._frames = frames
        self.canvas.set_frames(self._frames)
        self._refresh_frame_names()

    def _refresh_frame_names(self) -> None:
        prefix = self.prefix_edit.text().strip() or "frame"
        self.frame_list.clear()
        for i, f in enumerate(self._frames):
            self.frame_list.addItem(
                f"{prefix}_{i:02d}    "
                f"x={f['x']} y={f['y']} {f['w']}×{f['h']}"
            )

    def _generated_names(self) -> list[str]:
        prefix = self.prefix_edit.text().strip() or "frame"
        return [f"{prefix}_{i:02d}" for i in range(len(self._frames))]

    # ── Add to manifest ─────────────────────────────────────────────────────

    def _on_add_to_manifest(self) -> None:
        if not self._frames:
            QMessageBox.warning(self, "Nothing to add", "No frames defined.")
            return

        tex_name = self.texture_name_edit.text().strip()
        tex_path = self.texture_path_edit.text().strip()
        if not tex_name or not tex_path:
            QMessageBox.warning(self, "Texture missing",
                                "Texture name and path are required.")
            return

        prefix = self.prefix_edit.text().strip()
        if not prefix:
            QMessageBox.warning(self, "Prefix missing",
                                "A sprite prefix is required so frames have names.")
            return

        # Upsert one texture entry, then one sprite per frame.
        self.manifest.upsert_texture(tex_name, tex_path)
        names = self._generated_names()
        for name, frame in zip(names, self._frames):
            self.manifest.upsert_sprite(
                name, tex_name,
                frame["x"], frame["y"], frame["w"], frame["h"],
            )

        # Auto-save so the user doesn't have to remember Ctrl+S.
        win = self.window()
        if hasattr(win, "save_manifest"):
            if win.save_manifest():
                win.statusBar().showMessage(
                    f"Added '{tex_name}' + {len(names)} sprites to disk.",
                    5000,
                )
        else:
            QMessageBox.information(
                self, "Added",
                f"Added '{tex_name}' + {len(names)} sprites to the manifest.",
            )

    # ── External hooks ──────────────────────────────────────────────────────

    def on_manifest_reloaded(self) -> None:
        # Step 1 doesn't display existing manifest entries — clear the
        # working frame list so the user doesn't accidentally re-add stale
        # state on top of fresh data from disk.
        self._on_clear_frames()
