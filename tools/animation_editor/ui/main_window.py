"""
Top-level QMainWindow with a tab per editor area. Step 1 only wires the
Sprites tab — the other three are placeholders until we get there.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QCloseEvent
from PySide6.QtWidgets import (
    QFileDialog, QLabel, QMainWindow, QMessageBox, QStatusBar,
    QTabWidget, QWidget,
)

from manifest import Manifest

from .sheet_slicer    import SheetSlicerTab
from .animations_tab  import AnimationsTab
from .sets_tab        import SetsTab
from .preview_tab     import PreviewTab


class MainWindow(QMainWindow):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root

        # Title is rebuilt from manifest.path + dirty flag every refresh, so
        # File→Open can swap the path without touching anything else.
        self._refresh_title()
        manifest.add_dirty_listener(lambda _: self._refresh_title())

        # ── Tabs ────────────────────────────────────────────────────────────
        tabs = QTabWidget()
        self.sprites_tab    = SheetSlicerTab(manifest, project_root)
        self.animations_tab = AnimationsTab (manifest, project_root)
        self.sets_tab       = SetsTab       (manifest, project_root)
        self.preview_tab    = PreviewTab    (manifest, project_root)
        tabs.addTab(self.sprites_tab,    "Sprites")
        tabs.addTab(self.animations_tab, "Animations")
        tabs.addTab(self.sets_tab,       "Sets")
        tabs.addTab(self.preview_tab,    "Preview")
        # Refresh whichever tab the user just landed on. Tabs cache their
        # views (sprite picker, set list, ...) for performance, so without
        # this they'd show stale data after another tab mutated the manifest.
        # Sprites tab is excluded — it owns transient slicing state that
        # shouldn't be wiped just because the user toggled tabs.
        tabs.currentChanged.connect(self._on_tab_changed)
        self._tabs = tabs
        self.setCentralWidget(tabs)

        # ── Menu / status ───────────────────────────────────────────────────
        self._build_menu()
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("Ready")

    # ── UI building ─────────────────────────────────────────────────────────

    def _placeholder(self, message: str) -> QWidget:
        w = QLabel(message)
        w.setStyleSheet("color: #888; padding: 24px;")
        return w

    def _build_menu(self) -> None:
        file_menu = self.menuBar().addMenu("&File")

        open_action = QAction("&Open manifest…", self)
        open_action.setShortcut("Ctrl+O")
        open_action.triggered.connect(self._open_manifest)
        file_menu.addAction(open_action)

        save_action = QAction("&Save manifest", self)
        save_action.setShortcut("Ctrl+S")
        save_action.triggered.connect(self._save_manifest)
        file_menu.addAction(save_action)

        reload_action = QAction("&Reload manifest", self)
        reload_action.setShortcut("Ctrl+R")
        reload_action.triggered.connect(self._reload_manifest)
        file_menu.addAction(reload_action)

        file_menu.addSeparator()

        quit_action = QAction("&Quit", self)
        quit_action.setShortcut("Ctrl+Q")
        quit_action.triggered.connect(self.close)
        file_menu.addAction(quit_action)

    # ── Tab switching ───────────────────────────────────────────────────────

    def _on_tab_changed(self, index: int) -> None:
        widget = self._tabs.widget(index)
        # Only the data-driven tabs need a refresh; Sprites tab keeps its
        # in-flight slicing state instead. We dispatch via refresh_from_manifest
        # which every relevant tab implements.
        if widget is self.sprites_tab:
            return
        if hasattr(widget, "refresh_from_manifest"):
            widget.refresh_from_manifest()

    # ── Actions ─────────────────────────────────────────────────────────────

    def _refresh_title(self) -> None:
        # Leading '*' is the standard "unsaved changes" affordance — Qt
        # Creator, VS Code, JetBrains all use the same convention.
        marker = "* " if self.manifest.dirty else ""
        self.setWindowTitle(
            f"{marker}fast-photon animation editor — {self.manifest.path}"
        )

    def save_manifest(self) -> bool:
        """Public so tabs can write to disk after their own mutations.
        Returns True on success."""
        try:
            self.manifest.save()
            self.statusBar().showMessage(f"Saved {self.manifest.path}", 3000)
            return True
        except OSError as exc:
            QMessageBox.critical(self, "Save failed", str(exc))
            return False

    def _save_manifest(self) -> None:
        self.save_manifest()

    def _reload_manifest(self) -> None:
        # Reloading throws away unsaved in-memory changes, so confirm if
        # there's anything to lose. Same Save/Discard/Cancel pattern as
        # closeEvent so the experience matches.
        if not self._confirm_discard_if_dirty("Reload manifest"):
            return
        try:
            self.manifest.load()
        except OSError as exc:
            QMessageBox.critical(self, "Reload failed", str(exc))
            return
        self._notify_tabs_manifest_reloaded()
        self.statusBar().showMessage("Reloaded from disk", 3000)
        self._refresh_title()

    def _open_manifest(self) -> None:
        # Same dirty guard as Reload — switching to a different file
        # discards in-memory changes the same way.
        if not self._confirm_discard_if_dirty("Open another manifest"):
            return

        # Default the dialog to the assets/ folder so the typical case
        # (picking common or a faction file) is one click away.
        start_dir = self.project_root / "assets"
        if not start_dir.exists():
            start_dir = self.project_root

        path_str, _ = QFileDialog.getOpenFileName(
            self, "Open assets manifest",
            str(start_dir),
            "Asset manifest (*.json);;All files (*)",
        )
        if not path_str:
            return

        new_path = Path(path_str)
        try:
            self.manifest.load(new_path)
        except (OSError, ValueError) as exc:
            QMessageBox.critical(self, "Open failed", str(exc))
            return

        self._notify_tabs_manifest_reloaded()
        self._refresh_title()
        self.statusBar().showMessage(f"Opened {new_path}", 3000)

    def _confirm_discard_if_dirty(self, action_label: str) -> bool:
        """If the manifest is dirty, prompt the user and act on the answer.
        Returns True when the caller may proceed with the destructive op."""
        if not self.manifest.dirty:
            return True
        choice = self._prompt_unsaved(action_label)
        if choice == "cancel":
            return False
        if choice == "save" and not self.save_manifest():
            return False
        return True

    def _notify_tabs_manifest_reloaded(self) -> None:
        self.sprites_tab.on_manifest_reloaded()
        self.animations_tab.on_manifest_reloaded()
        self.sets_tab.on_manifest_reloaded()
        self.preview_tab.on_manifest_reloaded()

    # ── Close handling ──────────────────────────────────────────────────────

    def closeEvent(self, event: QCloseEvent) -> None:
        if not self.manifest.dirty:
            event.accept()
            return
        choice = self._prompt_unsaved("Quit")
        if choice == "cancel":
            event.ignore()
            return
        if choice == "save" and not self.save_manifest():
            event.ignore()
            return
        event.accept()

    def _prompt_unsaved(self, action: str) -> str:
        """Returns one of 'save' / 'discard' / 'cancel'."""
        box = QMessageBox(self)
        box.setWindowTitle(action)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setText(f"You have unsaved changes. {action} anyway?")
        save = box.addButton(f"Save and {action.lower()}", QMessageBox.ButtonRole.AcceptRole)
        discard = box.addButton(f"Discard and {action.lower()}", QMessageBox.ButtonRole.DestructiveRole)
        cancel = box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(save)
        box.exec()
        clicked = box.clickedButton()
        if clicked is save:    return "save"
        if clicked is discard: return "discard"
        return "cancel"
