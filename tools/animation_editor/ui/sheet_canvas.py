"""
QGraphicsView-based canvas that:
  - Displays a PNG sprite sheet.
  - Draws translucent overlay rectangles for the current frame set.
  - In Manual mode: lets the user drag a new rectangle that becomes a frame.
  - Emits a signal when frames change so the side panel can refresh.

Frames are stored as plain dicts: {"x", "y", "w", "h"} in image-pixel coords.
The SheetSlicerTab owns the authoritative list — this widget just renders
and emits edits.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QMouseEvent, QPen, QPixmap
from PySide6.QtWidgets import (
    QGraphicsItem, QGraphicsPixmapItem, QGraphicsRectItem,
    QGraphicsScene, QGraphicsView,
)


# Visual style for frame overlays.
_FRAME_PEN   = QPen(QColor(80, 200, 255, 220), 1)
_FRAME_BRUSH = QBrush(QColor(80, 200, 255, 50))
_DRAFT_PEN   = QPen(QColor(255, 200, 80, 220), 1, Qt.PenStyle.DashLine)
_DRAFT_BRUSH = QBrush(QColor(255, 200, 80, 60))


class SheetCanvas(QGraphicsView):
    # Emitted whenever the user *adds* a new manual rectangle. The tab
    # appends to its frame list on receipt.
    manual_rect_drawn = Signal(int, int, int, int)   # x, y, w, h

    def __init__(self) -> None:
        super().__init__()
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)
        self.setBackgroundBrush(QColor(30, 30, 30))
        self.setRenderHints(self.renderHints())  # default; antialias not useful here

        # Layers, in z-order:
        #   pixmap_item (the sheet itself)
        #   _overlay_items (one QGraphicsRectItem per frame)
        #   _draft_item (the rect currently being dragged in manual mode)
        self._pixmap_item: Optional[QGraphicsPixmapItem] = None
        self._overlay_items: list[QGraphicsRectItem] = []
        self._draft_item: Optional[QGraphicsRectItem] = None

        # Manual-mode drag state.
        self._manual_mode = False
        self._drag_start: Optional[QPointF] = None

        # Most-recently loaded image dimensions, in pixels. Used by the tab's
        # auto-grid math; also for clamping manual draws inside the image.
        self._image_w = 0
        self._image_h = 0

    # ── Image management ────────────────────────────────────────────────────

    def load_image(self, path: str) -> bool:
        pix = QPixmap(path)
        if pix.isNull():
            return False
        self._scene.clear()
        self._overlay_items.clear()
        self._draft_item = None

        self._pixmap_item = self._scene.addPixmap(pix)
        self._pixmap_item.setZValue(0)
        self._image_w = pix.width()
        self._image_h = pix.height()

        self._scene.setSceneRect(QRectF(0, 0, pix.width(), pix.height()))
        self.fitInView(self._scene.sceneRect(), Qt.AspectRatioMode.KeepAspectRatio)
        return True

    def image_size(self) -> tuple[int, int]:
        return self._image_w, self._image_h

    def has_image(self) -> bool:
        return self._pixmap_item is not None

    # ── Frame overlays ──────────────────────────────────────────────────────

    def set_frames(self, frames: list[dict]) -> None:
        """Replace overlay rectangles with the given frame list."""
        for item in self._overlay_items:
            self._scene.removeItem(item)
        self._overlay_items.clear()

        for f in frames:
            rect = self._scene.addRect(
                QRectF(f["x"], f["y"], f["w"], f["h"]),
                _FRAME_PEN, _FRAME_BRUSH,
            )
            rect.setZValue(1)
            rect.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, False)
            self._overlay_items.append(rect)

    # ── Manual drawing ──────────────────────────────────────────────────────

    def set_manual_mode(self, enabled: bool) -> None:
        self._manual_mode = enabled
        self.setCursor(Qt.CursorShape.CrossCursor if enabled else Qt.CursorShape.ArrowCursor)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if not (self._manual_mode and self.has_image()
                and event.button() == Qt.MouseButton.LeftButton):
            return super().mousePressEvent(event)
        self._drag_start = self.mapToScene(event.pos())
        self._draft_item = self._scene.addRect(
            QRectF(self._drag_start, self._drag_start),
            _DRAFT_PEN, _DRAFT_BRUSH,
        )
        self._draft_item.setZValue(2)
        event.accept()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._draft_item is not None and self._drag_start is not None:
            current = self.mapToScene(event.pos())
            self._draft_item.setRect(QRectF(self._drag_start, current).normalized())
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if self._draft_item is None or self._drag_start is None:
            return super().mouseReleaseEvent(event)

        rect = self._draft_item.rect()
        # Get rid of the draft regardless — the tab will (re-)draw the new
        # frame as a permanent overlay through set_frames().
        self._scene.removeItem(self._draft_item)
        self._draft_item = None
        self._drag_start = None

        # Clamp to the image and ignore micro-clicks (≤ 2 px).
        x = max(0, int(rect.x()))
        y = max(0, int(rect.y()))
        w = int(rect.width())
        h = int(rect.height())
        if w < 2 or h < 2:
            event.accept()
            return
        x = min(x, self._image_w - 1)
        y = min(y, self._image_h - 1)
        w = min(w, self._image_w - x)
        h = min(h, self._image_h - y)
        self.manual_rect_drawn.emit(x, y, w, h)
        event.accept()

    # ── Resize handling ─────────────────────────────────────────────────────

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if self._pixmap_item is not None:
            self.fitInView(self._scene.sceneRect(), Qt.AspectRatioMode.KeepAspectRatio)
