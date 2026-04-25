"""
Single-tab editor for one particle system at a time.

Layout:
    ┌─ Existing systems ─┬─ Form (name, sprite, ranges, colours) ─┐
    │  spark             │                                         │
    │  blood             │                                         │
    │                    │                                         │
    │ [New system]       ├──────────── Preview canvas ─────────────┤
    │ [Save system]      │                                         │
    └────────────────────┴─────────────────────────────────────────┘

Most controls write through to the preview spec on every edit, so the
canvas updates live. The manifest itself only changes on Save — until
then the form is in 'draft' state and Reload throws it away cleanly.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QColorDialog, QComboBox, QDoubleSpinBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMessageBox, QPushButton, QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from manifest import Manifest

from .preview_canvas import ParticleSpec, PreviewCanvas
from .thumbnail_cache import ThumbnailCache


def _vec2_or_default(v, fallback):
    if isinstance(v, list) and len(v) == 2:
        try:
            return float(v[0]), float(v[1])
        except (TypeError, ValueError):
            pass
    return fallback


def _vec4_or_default(v, fallback):
    if isinstance(v, list) and len(v) == 4:
        try:
            return tuple(float(x) for x in v)
        except (TypeError, ValueError):
            pass
    return fallback


class _ColorButton(QPushButton):
    """Small swatch button — clicking opens QColorDialog. Stores RGBA in
    [0..1] floats; emits no Qt signal of its own, callers reconnect to
    `clicked` and read `.rgba` after the dialog returns."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.rgba: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
        self.setFixedSize(56, 24)
        self._refresh()

    def set_rgba(self, rgba: tuple[float, float, float, float]) -> None:
        self.rgba = rgba
        self._refresh()

    def open_picker(self) -> bool:
        """Returns True if the user picked a new colour."""
        c = QColor.fromRgbF(*self.rgba)
        # ShowAlphaChannel — particles fade out, the alpha column matters.
        picked = QColorDialog.getColor(
            c, self, "Pick colour",
            QColorDialog.ColorDialogOption.ShowAlphaChannel,
        )
        if not picked.isValid():
            return False
        self.rgba = picked.getRgbF()[:4]
        self._refresh()
        return True

    def _refresh(self) -> None:
        r, g, b, a = self.rgba
        # Show the swatch as opaque-ish; alpha is hinted via a faint
        # checkerboard outline rather than truly transparent buttons,
        # which Qt struggles with on dark themes.
        rgb = QColor.fromRgbF(r, g, b).name()
        self.setStyleSheet(
            f"QPushButton {{ background-color: {rgb}; "
            f"border: 1px solid #555; }}"
        )
        self.setText(f"a={a:.2f}")


class EditorTab(QWidget):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root
        self._thumbs = ThumbnailCache(manifest, project_root)

        self._build_ui()
        self._wire_signals()
        self.refresh_from_manifest()

    # ── UI ──────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # ── Existing list + buttons ────────────────────────────────────────
        self.existing_list = QListWidget()
        self.new_btn  = QPushButton("New system")
        self.save_btn = QPushButton("Save system")
        self.save_btn.setStyleSheet("font-weight: bold; padding: 6px;")

        existing_box = QGroupBox("Existing systems (double-click to load)")
        existing_layout = QVBoxLayout(existing_box)
        existing_layout.addWidget(self.existing_list, stretch=1)
        existing_layout.addWidget(self.new_btn)
        existing_layout.addWidget(self.save_btn)

        # ── Form ───────────────────────────────────────────────────────────
        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText("spark")

        self.sprite_combo = QComboBox()
        self.sprite_combo.setEditable(True)
        self.sprite_combo.setToolTip(
            "Sprite id local to the open manifest, or a cross-faction "
            "reference like 'common/foo'."
        )

        self.max_particles_spin = QSpinBox()
        self.max_particles_spin.setRange(1, 4096)
        self.max_particles_spin.setValue(256)

        self.emit_rate_spin = QDoubleSpinBox()
        self.emit_rate_spin.setRange(0.0, 1000.0)
        self.emit_rate_spin.setDecimals(1)
        self.emit_rate_spin.setSingleStep(1.0)
        self.emit_rate_spin.setValue(16.0)

        self.burst_spin = QSpinBox()
        self.burst_spin.setRange(0, 4096)
        self.burst_spin.setValue(0)

        self.life_min = self._make_double(0.0, 60.0, 0.05, 1.0)
        self.life_max = self._make_double(0.0, 60.0, 0.05, 1.0)

        self.vel_min_x = self._make_double(-100.0, 100.0, 0.1, 0.0)
        self.vel_min_y = self._make_double(-100.0, 100.0, 0.1, 0.0)
        self.vel_max_x = self._make_double(-100.0, 100.0, 0.1, 0.0)
        self.vel_max_y = self._make_double(-100.0, 100.0, 0.1, 0.0)

        self.gravity_x = self._make_double(-100.0, 100.0, 0.1, 0.0)
        self.gravity_y = self._make_double(-100.0, 100.0, 0.1, 0.0)

        self.size_start = self._make_double(0.0, 32.0, 0.05, 0.5)
        self.size_end   = self._make_double(0.0, 32.0, 0.05, 0.0)

        self.color_start = _ColorButton()
        self.color_start.set_rgba((1.0, 1.0, 1.0, 1.0))
        self.color_end = _ColorButton()
        self.color_end.set_rgba((1.0, 1.0, 1.0, 0.0))

        # Form layout — two-column ranges live in their own row widgets so
        # the labels in the form line up.
        form_box = QGroupBox("System")
        form = QFormLayout(form_box)
        form.addRow("Name", self.name_edit)
        form.addRow("Sprite", self.sprite_combo)
        form.addRow("Max particles", self.max_particles_spin)
        form.addRow("Emit rate", self.emit_rate_spin)
        form.addRow("Burst count", self.burst_spin)
        form.addRow("Lifetime",  self._h_pair(self.life_min, "—", self.life_max))
        form.addRow("Velocity X", self._h_pair(self.vel_min_x, "—", self.vel_max_x))
        form.addRow("Velocity Y", self._h_pair(self.vel_min_y, "—", self.vel_max_y))
        form.addRow("Gravity",    self._h_pair(self.gravity_x, ",", self.gravity_y))
        form.addRow("Size",       self._h_pair(self.size_start, "→", self.size_end))
        form.addRow("Colour",     self._h_pair(self.color_start, "→", self.color_end))

        # ── Preview ────────────────────────────────────────────────────────
        self.preview = PreviewCanvas()
        self.pause_btn = QPushButton("Pause")
        self.pause_btn.setCheckable(True)
        self.reset_btn = QPushButton("Reset")

        preview_controls = QHBoxLayout()
        preview_controls.addWidget(self.pause_btn)
        preview_controls.addWidget(self.reset_btn)
        preview_controls.addStretch()

        preview_box = QGroupBox("Preview")
        preview_layout = QVBoxLayout(preview_box)
        preview_layout.addLayout(preview_controls)
        preview_layout.addWidget(self.preview, stretch=1)

        # ── Centre column: form + preview stacked ─────────────────────────
        centre = QWidget()
        centre_layout = QVBoxLayout(centre)
        centre_layout.addWidget(form_box)
        centre_layout.addWidget(preview_box, stretch=1)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(existing_box)
        splitter.addWidget(centre)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)

        outer = QHBoxLayout(self)
        outer.addWidget(splitter)

    def _make_double(self, lo: float, hi: float, step: float, value: float) -> QDoubleSpinBox:
        sp = QDoubleSpinBox()
        sp.setRange(lo, hi)
        sp.setDecimals(2)
        sp.setSingleStep(step)
        sp.setValue(value)
        return sp

    def _h_pair(self, a: QWidget, sep: str, b: QWidget) -> QWidget:
        w = QWidget()
        layout = QHBoxLayout(w)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(a)
        if sep:
            layout.addWidget(QLabel(sep))
        layout.addWidget(b)
        return w

    def _wire_signals(self) -> None:
        self.new_btn.clicked.connect(self._on_new)
        self.save_btn.clicked.connect(self._on_save)
        self.existing_list.itemDoubleClicked.connect(self._on_load_existing)

        self.color_start.clicked.connect(
            lambda: self.color_start.open_picker() and self._on_form_changed()
        )
        self.color_end.clicked.connect(
            lambda: self.color_end.open_picker() and self._on_form_changed()
        )

        # Live preview: anything the user changes in the form pushes a
        # fresh ParticleSpec to the canvas. Save isn't required to see
        # tweaks reflected.
        for spin in (self.max_particles_spin, self.burst_spin):
            spin.valueChanged.connect(self._on_form_changed)
        for spin in (self.emit_rate_spin, self.life_min, self.life_max,
                     self.vel_min_x, self.vel_min_y,
                     self.vel_max_x, self.vel_max_y,
                     self.gravity_x, self.gravity_y,
                     self.size_start, self.size_end):
            spin.valueChanged.connect(self._on_form_changed)

        # Sprite combo: both list selection and free-form typing must
        # update the preview, so listen on currentTextChanged (covers
        # both code paths — index change *and* edit field commit).
        self.sprite_combo.currentTextChanged.connect(self._on_sprite_changed)

        self.pause_btn.toggled.connect(self.preview.set_paused)
        self.reset_btn.clicked.connect(self.preview.reset)

    # ── Manifest ↔ form ─────────────────────────────────────────────────────

    def refresh_from_manifest(self) -> None:
        # Reload of the manifest (or initial open) invalidates every
        # cached pixmap — paths and sprite rects may have been edited
        # outside this tool.
        self._thumbs.invalidate()

        self.existing_list.clear()
        for ps in self.manifest.particle_systems:
            name = ps.get("name", "")
            self.existing_list.addItem(
                f"{name}    ({ps.get('emit_rate', 0)}/s)"
            )

        # Sprite combo: local sprite ids only. Cross-refs the user types
        # into the editable field aren't validated here — save-time
        # validation lets `/`-qualified names through.
        prev = self.sprite_combo.currentText()
        self.sprite_combo.blockSignals(True)
        self.sprite_combo.clear()
        self.sprite_combo.addItems(self.manifest.sprite_names())
        if prev:
            self.sprite_combo.setCurrentText(prev)
        self.sprite_combo.blockSignals(False)

        # Push the sprite to the preview manually because we suppressed
        # combo signals while repopulating it.
        self._on_sprite_changed(self.sprite_combo.currentText())
        self._on_form_changed()

    def _on_sprite_changed(self, text: str) -> None:
        # ThumbnailCache returns None for cross-faction refs and missing
        # textures; PreviewCanvas falls back to coloured discs in that
        # case, so we don't need to special-case anything here.
        pix = self._thumbs.get(text.strip())
        self.preview.set_sprite(pix)

    def _on_form_changed(self) -> None:
        self.preview.set_spec(self._read_form())

    def _read_form(self) -> ParticleSpec:
        return ParticleSpec(
            max_particles=self.max_particles_spin.value(),
            emit_rate=float(self.emit_rate_spin.value()),
            burst_count=self.burst_spin.value(),
            lifetime_min=float(self.life_min.value()),
            lifetime_max=float(self.life_max.value()),
            velocity_min=(float(self.vel_min_x.value()), float(self.vel_min_y.value())),
            velocity_max=(float(self.vel_max_x.value()), float(self.vel_max_y.value())),
            gravity=(float(self.gravity_x.value()), float(self.gravity_y.value())),
            size_start=float(self.size_start.value()),
            size_end=float(self.size_end.value()),
            color_start=self.color_start.rgba,
            color_end=self.color_end.rgba,
        )

    def _populate_form(self, ps: dict) -> None:
        self.name_edit.setText(ps.get("name", ""))
        self.sprite_combo.setCurrentText(ps.get("sprite", ""))

        self.max_particles_spin.setValue(int(ps.get("max_particles", 256)))
        self.emit_rate_spin.setValue(float(ps.get("emit_rate", 16.0)))
        self.burst_spin.setValue(int(ps.get("burst_count", 0)))
        self.life_min.setValue(float(ps.get("lifetime_min", 1.0)))
        self.life_max.setValue(float(ps.get("lifetime_max", 1.0)))

        vmin = _vec2_or_default(ps.get("velocity_min"), (0.0, 0.0))
        vmax = _vec2_or_default(ps.get("velocity_max"), (0.0, 0.0))
        self.vel_min_x.setValue(vmin[0]); self.vel_min_y.setValue(vmin[1])
        self.vel_max_x.setValue(vmax[0]); self.vel_max_y.setValue(vmax[1])

        g = _vec2_or_default(ps.get("gravity"), (0.0, 0.0))
        self.gravity_x.setValue(g[0]); self.gravity_y.setValue(g[1])

        self.size_start.setValue(float(ps.get("size_start", 0.5)))
        self.size_end.setValue(float(ps.get("size_end", 0.0)))

        cs = _vec4_or_default(ps.get("color_start"), (1.0, 1.0, 1.0, 1.0))
        ce = _vec4_or_default(ps.get("color_end"),   (1.0, 1.0, 1.0, 0.0))
        self.color_start.set_rgba(cs)
        self.color_end.set_rgba(ce)

        # Form changed → push the new spec to preview. reset() also
        # re-fires the burst so users get an immediate visual on load.
        self._on_form_changed()
        self.preview.reset()

    # ── Buttons ─────────────────────────────────────────────────────────────

    def _on_new(self) -> None:
        # Wipe everything to the defaults a fresh ParticleSpec would have.
        self.name_edit.clear()
        self.sprite_combo.setCurrentText("")
        self.max_particles_spin.setValue(256)
        self.emit_rate_spin.setValue(16.0)
        self.burst_spin.setValue(0)
        self.life_min.setValue(1.0)
        self.life_max.setValue(1.0)
        for sp in (self.vel_min_x, self.vel_min_y,
                   self.vel_max_x, self.vel_max_y,
                   self.gravity_x, self.gravity_y):
            sp.setValue(0.0)
        self.size_start.setValue(0.5)
        self.size_end.setValue(0.0)
        self.color_start.set_rgba((1.0, 1.0, 1.0, 1.0))
        self.color_end.set_rgba((1.0, 1.0, 1.0, 0.0))
        self._on_form_changed()
        self.preview.reset()

    def _on_load_existing(self, item: QListWidgetItem) -> None:
        text = item.text()
        name = text.split()[0] if text else ""
        entry = next(
            (p for p in self.manifest.particle_systems if p.get("name") == name),
            None,
        )
        if entry is None:
            return
        self._populate_form(entry)

    def _on_save(self) -> None:
        spec_dict, err = self._validated_dict()
        if err:
            QMessageBox.critical(self, "Cannot save", err)
            return

        # Upsert by name.
        target: Optional[dict] = None
        for entry in self.manifest.particle_systems:
            if entry.get("name") == spec_dict["name"]:
                target = entry
                break
        if target is None:
            self.manifest.particle_systems.append(spec_dict)
        else:
            target.clear()
            target.update(spec_dict)

        self.manifest.mark_dirty()
        self.refresh_from_manifest()

        win = self.window()
        if hasattr(win, "save_manifest"):
            if win.save_manifest():
                win.statusBar().showMessage(
                    f"Saved particle system '{spec_dict['name']}' to disk.",
                    5000,
                )

    def _validated_dict(self) -> tuple[dict, str | None]:
        """Returns (entry_dict, error_or_None). Mirrors the bake-side
        invariants so authors hit them in the editor."""
        name = self.name_edit.text().strip()
        if not name:
            return {}, "System name is required."
        if "/" in name:
            return {}, ("Use the local name only (no '/'); the bake step "
                        "adds the faction prefix automatically.")

        sprite = self.sprite_combo.currentText().strip()
        if not sprite:
            return {}, "Sprite is required."
        # Local sprite must exist; cross-ref ('foo/bar') is validated by bake.
        if "/" not in sprite and sprite not in set(self.manifest.sprite_names()):
            return {}, (f"Sprite '{sprite}' is not in this manifest. "
                        f"For cross-faction refs use 'common/...' style.")

        if self.life_min.value() > self.life_max.value():
            return {}, "Lifetime min must be ≤ max."
        if self.vel_min_x.value() > self.vel_max_x.value():
            return {}, "Velocity X min must be ≤ max."
        if self.vel_min_y.value() > self.vel_max_y.value():
            return {}, "Velocity Y min must be ≤ max."

        return {
            "name":          name,
            "max_particles": self.max_particles_spin.value(),
            "emit_rate":     float(self.emit_rate_spin.value()),
            "burst_count":   self.burst_spin.value(),
            "lifetime_min":  float(self.life_min.value()),
            "lifetime_max":  float(self.life_max.value()),
            "velocity_min":  [float(self.vel_min_x.value()), float(self.vel_min_y.value())],
            "velocity_max":  [float(self.vel_max_x.value()), float(self.vel_max_y.value())],
            "gravity":       [float(self.gravity_x.value()), float(self.gravity_y.value())],
            "size_start":    float(self.size_start.value()),
            "size_end":      float(self.size_end.value()),
            "color_start":   list(self.color_start.rgba),
            "color_end":     list(self.color_end.rgba),
            "sprite":        sprite,
        }, None

    # ── External hooks ──────────────────────────────────────────────────────

    def on_manifest_reloaded(self) -> None:
        self._on_new()
        self.refresh_from_manifest()
