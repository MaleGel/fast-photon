"""
Top-level QMainWindow. One central editor widget; menu bar handles
File → Open / Save / Reload, dirty title indicator, close-prompt.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QAction, QCloseEvent
from PySide6.QtWidgets import (
    QFileDialog, QMainWindow, QMessageBox, QStatusBar,
)

from manifest import Manifest

from .editor_tab import EditorTab


class MainWindow(QMainWindow):
    def __init__(self, manifest: Manifest, project_root: Path):
        super().__init__()
        self.manifest = manifest
        self.project_root = project_root

        self._refresh_title()
        manifest.add_dirty_listener(lambda _: self._refresh_title())

        self.editor = EditorTab(manifest, project_root)
        self.setCentralWidget(self.editor)

        self._build_menu()
        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("Ready")

    # ── Menu ────────────────────────────────────────────────────────────────

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

    # ── Title ───────────────────────────────────────────────────────────────

    def _refresh_title(self) -> None:
        marker = "* " if self.manifest.dirty else ""
        self.setWindowTitle(
            f"{marker}fast-photon particle editor — {self.manifest.path}"
        )

    # ── Actions ─────────────────────────────────────────────────────────────

    def save_manifest(self) -> bool:
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
        if not self._confirm_discard_if_dirty("Reload manifest"):
            return
        try:
            self.manifest.load()
        except OSError as exc:
            QMessageBox.critical(self, "Reload failed", str(exc))
            return
        self.editor.on_manifest_reloaded()
        self.statusBar().showMessage("Reloaded from disk", 3000)
        self._refresh_title()

    def _open_manifest(self) -> None:
        if not self._confirm_discard_if_dirty("Open another manifest"):
            return
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
        self.editor.on_manifest_reloaded()
        self._refresh_title()
        self.statusBar().showMessage(f"Opened {new_path}", 3000)

    # ── Dirty-prompt + close handling ──────────────────────────────────────

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

    def _confirm_discard_if_dirty(self, action_label: str) -> bool:
        if not self.manifest.dirty:
            return True
        choice = self._prompt_unsaved(action_label)
        if choice == "cancel":
            return False
        if choice == "save" and not self.save_manifest():
            return False
        return True

    def _prompt_unsaved(self, action: str) -> str:
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
