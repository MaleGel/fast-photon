"""
"Sets" tab — author AnimationSet entries (state machines) on a graph
canvas.

Layout:
    [ Existing sets        ] | [ Set name field             ]
    [ (double-click loads) ] | [ Graph canvas (state nodes) ]
    [ New / Save buttons   ] |
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtCore import QPointF, Qt
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout,
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QListWidget,
    QListWidgetItem, QMessageBox, QPushButton, QSplitter, QVBoxLayout,
    QWidget,
)

from manifest import Manifest

from .state_graph import EdgeItem, GraphView


class _StateDialog(QDialog):
    """Modal used both for 'Add state' and 'Edit state'.

    Caller pre-populates fields if editing; otherwise the dialog starts
    blank. The animation dropdown is filtered to whatever's currently in
    the manifest, so authors can't reference a non-existent track.
    """

    def __init__(self, parent: QWidget, animation_names: list[str],
                 *, title: str, name: str = "", animation: str = "",
                 loop: bool = True, lock_name: bool = False):
        super().__init__(parent)
        self.setWindowTitle(title)

        self.name_edit = QLineEdit(name)
        if lock_name:
            self.name_edit.setReadOnly(True)
            self.name_edit.setToolTip("State name can't be changed after creation. "
                                      "Delete and re-add if needed.")

        self.anim_combo = QComboBox()
        # Editable so users can type a cross-faction reference like
        # "common/idle" or "player/walk" — those names live in other
        # manifests we don't have visibility into. The bake step is
        # responsible for the global cross-reference check.
        self.anim_combo.setEditable(True)
        self.anim_combo.addItems(animation_names)
        if animation and animation in animation_names:
            self.anim_combo.setCurrentText(animation)
        elif animation:
            # Animation isn't in this manifest — could be a cross-ref (with
            # '/'), or a stale local id that's been deleted. We don't try
            # to distinguish here; just preserve what was entered so the
            # user can see and re-confirm or fix it.
            self.anim_combo.setCurrentText(animation)

        self.loop_check = QCheckBox()
        self.loop_check.setChecked(loop)

        form = QFormLayout()
        form.addRow("State name", self.name_edit)
        form.addRow("Animation",  self.anim_combo)
        form.addRow("Loop",       self.loop_check)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok |
            QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

    def values(self) -> tuple[str, str, bool]:
        return (
            self.name_edit.text().strip(),
            self.anim_combo.currentText().strip(),
            self.loop_check.isChecked(),
        )


class _TransitionDialog(QDialog):
    """Modal for adding / editing a trigger-driven transition.

    The source state is fixed by the caller (right-click context); user
    only picks a target and a trigger name.
    """

    def __init__(self, parent: QWidget, *, src_name: str, target_choices: list[str],
                 title: str, trigger: str = "", target: str = ""):
        super().__init__(parent)
        self.setWindowTitle(title)

        self.target_combo = QComboBox()
        self.target_combo.addItems(target_choices)
        if target and target in target_choices:
            self.target_combo.setCurrentText(target)

        self.trigger_edit = QLineEdit(trigger)
        self.trigger_edit.setPlaceholderText("attack")

        form = QFormLayout()
        form.addRow("Source", QLabel(src_name))
        form.addRow("Target", self.target_combo)
        form.addRow("Trigger", self.trigger_edit)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok |
            QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

    def values(self) -> tuple[str, str]:
        return self.target_combo.currentText().strip(), self.trigger_edit.text().strip()


class SetsTab(QWidget):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root

        self._build_ui()
        self._wire_signals()
        self.refresh_from_manifest()

    # ── UI ──────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # Left column.
        self.existing_list = QListWidget()
        self.new_btn  = QPushButton("New set")
        self.save_btn = QPushButton("Save set")
        self.save_btn.setStyleSheet("font-weight: bold; padding: 6px;")

        existing_box = QGroupBox("Existing sets (double-click to load)")
        existing_layout = QVBoxLayout(existing_box)
        existing_layout.addWidget(self.existing_list, stretch=1)
        existing_layout.addWidget(self.new_btn)
        existing_layout.addWidget(self.save_btn)

        # Center column: name field above, graph below.
        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText("warrior")
        name_row = QHBoxLayout()
        name_row.addWidget(QLabel("Set name:"))
        name_row.addWidget(self.name_edit, stretch=1)

        hint = QLabel(
            "Canvas right-click: Add state.   "
            "State right-click: Edit / Set default / Set next → / Add transition… / Delete.   "
            "Edge right-click: Edit / Delete.   "
            "Animation field accepts cross-references like 'common/idle'."
        )
        hint.setStyleSheet("color: #888;")
        hint.setWordWrap(True)

        self.graph = GraphView()
        # Wire canvas-level dialogs through callbacks; the GraphView itself
        # doesn't import the dialog classes.
        self.graph.add_state_callback       = self._open_add_dialog
        self.graph.edit_state_callback      = self._open_edit_dialog
        self.graph.add_transition_callback  = self._open_add_transition_dialog
        self.graph.edit_transition_callback = self._open_edit_transition_dialog

        center = QWidget()
        center_layout = QVBoxLayout(center)
        center_layout.addLayout(name_row)
        center_layout.addWidget(hint)
        center_layout.addWidget(self.graph, stretch=1)

        # Splitter.
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(existing_box)
        splitter.addWidget(center)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)

        outer = QHBoxLayout(self)
        outer.addWidget(splitter)

    def _wire_signals(self) -> None:
        self.new_btn.clicked.connect(self._on_new)
        self.save_btn.clicked.connect(self._on_save)
        self.existing_list.itemDoubleClicked.connect(self._on_load_existing)

    # ── Manifest ↔ UI ───────────────────────────────────────────────────────

    def refresh_from_manifest(self) -> None:
        self.existing_list.clear()
        for s in self.manifest.animation_sets:
            name = s.get("name", "")
            states = s.get("states", {}) or {}
            self.existing_list.addItem(f"{name}    ({len(states)} states)")

    def _animation_names(self) -> list[str]:
        return [a.get("name", "") for a in self.manifest.animations]

    # ── Dialogs ─────────────────────────────────────────────────────────────

    def _open_add_dialog(self, scene_pos: QPointF) -> None:
        anims = self._animation_names()
        if not anims:
            QMessageBox.warning(
                self, "No animations",
                "Define at least one animation in the Animations tab "
                "before adding states.",
            )
            return
        dlg = _StateDialog(self, anims, title="Add state")
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        name, anim, loop = dlg.values()
        if not name:
            QMessageBox.warning(self, "Name missing", "State name is required.")
            return
        if name in self.graph.state_names():
            QMessageBox.warning(self, "Duplicate name",
                                f"A state called '{name}' already exists.")
            return
        if not self.graph.add_state(name, anim, loop, scene_pos):
            QMessageBox.critical(self, "Failed", "Could not add state.")

    def _open_edit_dialog(self, state_name: str) -> None:
        # Pull current values out of the graph so the dialog shows them.
        data = self.graph.get_data()
        st = data.get("states", {}).get(state_name, {})
        dlg = _StateDialog(
            self, self._animation_names(),
            title=f"Edit state '{state_name}'",
            name=state_name,
            animation=st.get("animation", ""),
            loop=bool(st.get("loop", True)),
            lock_name=True,
        )
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        _, anim, loop = dlg.values()
        self.graph.update_state(state_name, anim, loop)

    def _open_add_transition_dialog(self, src_name: str) -> None:
        candidates = [n for n in self.graph.state_names() if n != src_name]
        if not candidates:
            QMessageBox.warning(
                self, "No targets",
                "Add at least one other state before defining transitions.",
            )
            return
        dlg = _TransitionDialog(
            self, src_name=src_name, target_choices=candidates,
            title=f"Add transition from '{src_name}'",
        )
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        target, trigger = dlg.values()
        if not trigger:
            QMessageBox.warning(self, "Trigger missing",
                                "A trigger name is required.")
            return
        if not self.graph.add_transition(src_name, target, trigger):
            QMessageBox.critical(
                self, "Couldn't add transition",
                f"State '{src_name}' already has a transition with trigger "
                f"'{trigger}'. Edit or delete the existing one first.",
            )

    def _open_edit_transition_dialog(self, edge) -> None:
        candidates = [n for n in self.graph.state_names() if n != edge.src.state_name]
        if not candidates:
            return
        dlg = _TransitionDialog(
            self, src_name=edge.src.state_name, target_choices=candidates,
            title="Edit transition",
            trigger=edge.trigger, target=edge.dst.state_name,
        )
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        target, trigger = dlg.values()
        if not trigger:
            QMessageBox.warning(self, "Trigger missing",
                                "A trigger name is required.")
            return
        if not self.graph.update_transition(edge, target, trigger):
            QMessageBox.critical(
                self, "Couldn't edit transition",
                f"Trigger '{trigger}' is already used by another transition "
                f"from this state.",
            )

    # ── Buttons ─────────────────────────────────────────────────────────────

    def _on_new(self) -> None:
        self.name_edit.setText("")
        self.graph.clear_graph()

    def _on_save(self) -> None:
        name = self.name_edit.text().strip()
        if not name:
            QMessageBox.warning(self, "Name missing", "Set name is required.")
            return

        data = self.graph.get_data()
        states = data["states"]
        if not states:
            QMessageBox.warning(self, "Empty graph",
                                "Add at least one state before saving.")
            return

        # Validation. All three checks mirror what the engine asserts on
        # load, surfaced earlier so authors hit them in the editor.
        default = data["default"]
        if default not in states:
            QMessageBox.critical(
                self, "Invalid default",
                f"Default state '{default}' is not in the graph.",
            )
            return
        anim_names = set(self._animation_names())
        for sname, st in states.items():
            anim = st["animation"]
            # Names containing '/' are cross-faction references resolved
            # by the bake step against the consolidated manifest. We
            # can't validate them locally — trust the author and let
            # bake catch dangling links at build time.
            if "/" in anim:
                pass
            elif anim not in anim_names:
                QMessageBox.critical(
                    self, "Missing animation",
                    f"State '{sname}' references animation "
                    f"'{anim}' which doesn't exist in this manifest. "
                    f"For cross-faction refs, qualify with a '/' "
                    f"(e.g. 'common/idle').",
                )
                return
            nxt = st.get("next")
            if nxt and nxt not in states:
                QMessageBox.critical(
                    self, "Bad transition",
                    f"State '{sname}' has next='{nxt}' but "
                    "that state isn't in the graph.",
                )
                return
            seen_triggers: set[str] = set()
            for tr in st.get("transitions", []) or []:
                trig = tr.get("trigger", "")
                target = tr.get("target", "")
                if not trig:
                    QMessageBox.critical(
                        self, "Bad transition",
                        f"State '{sname}' has a transition with no trigger name.",
                    )
                    return
                if trig in seen_triggers:
                    QMessageBox.critical(
                        self, "Duplicate trigger",
                        f"State '{sname}' has more than one transition for "
                        f"trigger '{trig}'.",
                    )
                    return
                seen_triggers.add(trig)
                if target not in states:
                    QMessageBox.critical(
                        self, "Bad transition",
                        f"State '{sname}' transition '{trig}' targets '{target}' "
                        "which isn't in the graph.",
                    )
                    return

        # Upsert into manifest.animation_sets, preserving anything else the
        # entry might have (none today, but future-proof).
        target: Optional[dict] = None
        for entry in self.manifest.animation_sets:
            if entry.get("name") == name:
                target = entry
                break
        if target is None:
            target = {"name": name}
            self.manifest.animation_sets.append(target)

        target["default"]  = default
        target["states"]   = states
        target["_layout"]  = data["_layout"]

        # Persist immediately so the user doesn't have to remember Ctrl+S.
        # The dirty flag is also set by mark_dirty(), but save() clears it
        # right away — net effect: clean, on-disk, and the window title
        # drops the '*' marker.
        self.manifest.mark_dirty()
        self.refresh_from_manifest()

        win = self.window()
        if hasattr(win, "save_manifest"):
            if win.save_manifest():
                win.statusBar().showMessage(
                    f"Saved set '{name}' ({len(states)} states) to disk.",
                    5000,
                )

    def _on_load_existing(self, item: QListWidgetItem) -> None:
        text = item.text()
        name = text.split()[0] if text else ""
        entry = next((s for s in self.manifest.animation_sets
                      if s.get("name") == name), None)
        if entry is None:
            return
        self.name_edit.setText(name)
        self.graph.set_data(entry)

    # ── External hooks ──────────────────────────────────────────────────────

    def on_manifest_reloaded(self) -> None:
        self._on_new()
        self.refresh_from_manifest()
