"""
QGraphicsScene-based state-machine editor primitives.

Three classes here:
    StateNode  — a draggable rectangle representing one named state.
                 Renders the state name, the animation it plays, and a
                 'loop' marker. Highlighted when it's the set's default.
    EdgeItem   — directed line + arrowhead from one node's right edge to
                 another node's left edge. Tracks endpoints automatically
                 when nodes move.
    GraphView  — QGraphicsView with right-click menus for canvas
                 ("Add state…") and nodes ("Edit / Set default / Set
                 next… / Delete"). Owns the node + edge collections.

The data model the editor operates on is a single dict shaped exactly like
the JSON entry we'll persist:
    {
        "name":   <set name>,
        "default": <state name>,
        "states": { <name>: {"animation", "loop", "next"?} ... },
        "_layout": { <name>: [x, y] }
    }
The View exposes get_data() / set_data() for that dict.
"""

from __future__ import annotations

import math
from typing import Callable, Optional

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import (
    QAction, QBrush, QColor, QFont, QPainter, QPainterPath, QPen, QPolygonF,
)
from PySide6.QtWidgets import (
    QGraphicsItem, QGraphicsScene, QGraphicsView, QMenu,
)


# Visual constants. Tuned so labels don't crowd at 100% zoom.
_NODE_W = 180.0
_NODE_H = 70.0
_NODE_RADIUS = 8.0

_NODE_PEN          = QPen(QColor(80, 90, 110),  1.5)
_NODE_PEN_DEFAULT  = QPen(QColor(255, 200, 80), 2.5)
_NODE_BRUSH        = QBrush(QColor(45, 55, 75))
_NODE_BRUSH_HOVER  = QBrush(QColor(60, 75, 100))

# 'next' edges: solid line, no label. Used for one-shot auto-transition.
_EDGE_PEN_NEXT = QPen(QColor(180, 200, 220), 2)
_EDGE_PEN_NEXT.setCapStyle(Qt.PenCapStyle.RoundCap)
_EDGE_BRUSH_NEXT = QBrush(QColor(180, 200, 220))

# Trigger edges: dashed line, label sits at the midpoint. Visually
# distinct so you can tell at a glance which is auto vs trigger-driven.
_EDGE_PEN_TRIG = QPen(QColor(120, 220, 160), 2)
_EDGE_PEN_TRIG.setCapStyle(Qt.PenCapStyle.RoundCap)
_EDGE_PEN_TRIG.setStyle(Qt.PenStyle.DashLine)
_EDGE_BRUSH_TRIG = QBrush(QColor(120, 220, 160))


# Custom z-values so edges always sit under nodes.
_Z_EDGE = 0
_Z_NODE = 1


# ── StateNode ────────────────────────────────────────────────────────────────

class StateNode(QGraphicsItem):
    """One state in the machine. Owns its name + animation + loop flag.

    Position is the *top-left* of the rectangle, so x/y in _layout match
    what the user sees, and we can compute connection points without
    fighting QGraphicsItem's coordinate origin.
    """

    def __init__(self, name: str, animation: str, loop: bool):
        super().__init__()
        self.state_name = name
        self.animation  = animation
        self.loop       = loop
        self.is_default = False

        # Edges that have us as either endpoint. Updated from EdgeItem so
        # we can tell each one to refresh when we move.
        self._edges: list[EdgeItem] = []

        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges, True)
        self.setAcceptHoverEvents(True)
        self.setZValue(_Z_NODE)
        self._hovered = False

    # ── Geometry ────────────────────────────────────────────────────────────

    def boundingRect(self) -> QRectF:
        return QRectF(0, 0, _NODE_W, _NODE_H)

    def paint(self, painter: QPainter, option, widget=None) -> None:
        pen = _NODE_PEN_DEFAULT if self.is_default else _NODE_PEN
        brush = _NODE_BRUSH_HOVER if self._hovered else _NODE_BRUSH
        painter.setPen(pen)
        painter.setBrush(brush)
        painter.drawRoundedRect(self.boundingRect(), _NODE_RADIUS, _NODE_RADIUS)

        # Title (state name).
        title_font = QFont(); title_font.setBold(True); title_font.setPointSize(11)
        painter.setFont(title_font)
        painter.setPen(QColor(230, 230, 230))
        painter.drawText(QRectF(8, 6, _NODE_W - 16, 22), Qt.AlignmentFlag.AlignLeft,
                         self.state_name)

        # Subtitle (animation + loop flag).
        sub_font = QFont(); sub_font.setPointSize(9)
        painter.setFont(sub_font)
        painter.setPen(QColor(170, 180, 195))
        loop_text = "loop" if self.loop else "one-shot"
        painter.drawText(QRectF(8, 28, _NODE_W - 16, 18), Qt.AlignmentFlag.AlignLeft,
                         f"anim: {self.animation or '?'}")
        painter.drawText(QRectF(8, 46, _NODE_W - 16, 18), Qt.AlignmentFlag.AlignLeft,
                         loop_text)

        if self.is_default:
            painter.setPen(QColor(255, 200, 80))
            painter.drawText(QRectF(_NODE_W - 60, 6, 56, 16),
                             Qt.AlignmentFlag.AlignRight, "default")

    def hoverEnterEvent(self, event):
        self._hovered = True; self.update()

    def hoverLeaveEvent(self, event):
        self._hovered = False; self.update()

    # Connection-point helpers used by edges.
    def left_anchor(self) -> QPointF:
        return self.scenePos() + QPointF(0, _NODE_H / 2)

    def right_anchor(self) -> QPointF:
        return self.scenePos() + QPointF(_NODE_W, _NODE_H / 2)

    # ── Edge tracking ───────────────────────────────────────────────────────

    def add_edge(self, edge: "EdgeItem") -> None:
        self._edges.append(edge)

    def remove_edge(self, edge: "EdgeItem") -> None:
        if edge in self._edges:
            self._edges.remove(edge)

    def itemChange(self, change, value):
        # As the node moves, refresh every edge that touches it. Without
        # this the edge graphics would lag behind drags.
        if change == QGraphicsItem.GraphicsItemChange.ItemPositionHasChanged:
            for e in list(self._edges):
                e.refresh()
        return super().itemChange(change, value)


# ── EdgeItem ─────────────────────────────────────────────────────────────────

class EdgeItem(QGraphicsItem):
    """Directed arrow from `src` (right side) to `dst` (left side).

    Two flavours:
        kind="next"    — auto-transition after a one-shot completes.
                         Solid line, no label, only valid when src.loop is
                         False (the GraphView enforces that).
        kind="trigger" — fired by gameplay via AnimationSystem::trigger.
                         Dashed line, trigger name drawn at midpoint.
                         Valid for both looping and one-shot states.

    We don't subclass QGraphicsLineItem because we want a custom arrowhead
    polygon and an optional label as part of the same item — simpler
    painting / hit-testing.
    """

    ARROW_SIZE = 12.0
    KIND_NEXT    = "next"
    KIND_TRIGGER = "trigger"

    def __init__(self, src: StateNode, dst: StateNode,
                 kind: str = KIND_NEXT, trigger: str = ""):
        super().__init__()
        self.src     = src
        self.dst     = dst
        self.kind    = kind
        self.trigger = trigger
        self.setZValue(_Z_EDGE)
        src.add_edge(self)
        dst.add_edge(self)

    def detach(self) -> None:
        self.src.remove_edge(self)
        self.dst.remove_edge(self)

    # The "geometry changed" notifier — call after either endpoint moves.
    def refresh(self) -> None:
        self.prepareGeometryChange()
        self.update()

    # ── Geometry ────────────────────────────────────────────────────────────

    def _endpoints(self) -> tuple[QPointF, QPointF]:
        return self.src.right_anchor(), self.dst.left_anchor()

    def boundingRect(self) -> QRectF:
        a, b = self._endpoints()
        # Pad by arrow size + room for label text above the line.
        rect = QRectF(a, b).normalized()
        pad = max(self.ARROW_SIZE, 24.0)
        return rect.adjusted(-pad, -pad, pad, pad)

    def shape(self) -> QPainterPath:
        a, b = self._endpoints()
        path = QPainterPath(a)
        path.lineTo(b)
        # Stroke the path so right-click hit testing along the line works.
        stroker_pen = QPen(); stroker_pen.setWidth(8)
        from PySide6.QtGui import QPainterPathStroker
        stroker = QPainterPathStroker(stroker_pen)
        return stroker.createStroke(path)

    def paint(self, painter: QPainter, option, widget=None) -> None:
        a, b = self._endpoints()
        is_trig = (self.kind == self.KIND_TRIGGER)
        painter.setPen(_EDGE_PEN_TRIG if is_trig else _EDGE_PEN_NEXT)
        painter.drawLine(a, b)

        # Arrowhead — isoceles triangle at the dst end.
        angle = math.atan2(b.y() - a.y(), b.x() - a.x())
        size = self.ARROW_SIZE
        p1 = QPointF(b.x() - size * math.cos(angle - math.pi / 7),
                     b.y() - size * math.sin(angle - math.pi / 7))
        p2 = QPointF(b.x() - size * math.cos(angle + math.pi / 7),
                     b.y() - size * math.sin(angle + math.pi / 7))
        painter.setBrush(_EDGE_BRUSH_TRIG if is_trig else _EDGE_BRUSH_NEXT)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawPolygon(QPolygonF([b, p1, p2]))

        # Trigger label at midpoint, slightly above the line so it doesn't
        # overlap the arrow shaft.
        if is_trig and self.trigger:
            mid = QPointF((a.x() + b.x()) / 2, (a.y() + b.y()) / 2)
            label_font = QFont(); label_font.setPointSize(9); label_font.setBold(True)
            painter.setFont(label_font)
            painter.setPen(QColor(220, 240, 220))
            # Background rect for legibility against busy graphs.
            metrics = painter.fontMetrics()
            text_w = metrics.horizontalAdvance(self.trigger) + 8
            text_h = metrics.height() + 2
            bg = QRectF(mid.x() - text_w / 2, mid.y() - text_h - 4,
                        text_w, text_h)
            painter.setBrush(QColor(28, 32, 40, 220))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawRoundedRect(bg, 4, 4)
            painter.setPen(QColor(220, 240, 220))
            painter.drawText(bg, Qt.AlignmentFlag.AlignCenter, self.trigger)


# ── GraphView ────────────────────────────────────────────────────────────────

class GraphView(QGraphicsView):
    """Canvas + interaction. Holds the authoritative collection of nodes
    and edges; SetsTab queries it via get_data() / set_data().
    """

    # Emitted when the graph topology changes (node/edge add/remove or
    # default change). SetsTab uses it to enable Save.
    graph_changed = Signal()

    def __init__(self):
        super().__init__()
        self._scene = QGraphicsScene(self)
        self._scene.setSceneRect(QRectF(-2000, -2000, 4000, 4000))
        self.setScene(self._scene)
        self.setBackgroundBrush(QColor(28, 32, 40))
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setDragMode(QGraphicsView.DragMode.RubberBandDrag)

        # name → StateNode. Authoritative — saving walks this dict.
        self._nodes: dict[str, StateNode] = {}
        self._edges: list[EdgeItem] = []
        self._default_name: Optional[str] = None

        # Callbacks installed by the tab so we don't pull dialogs into this
        # file. None means "feature unavailable from menus".
        self.add_state_callback:       Optional[Callable[[QPointF], None]]   = None
        self.edit_state_callback:      Optional[Callable[[str], None]]       = None
        self.add_transition_callback:  Optional[Callable[[str], None]]       = None
        self.edit_transition_callback: Optional[Callable[["EdgeItem"], None]] = None

    # ── Public API: data round-trip ─────────────────────────────────────────

    def get_data(self) -> dict:
        """Snapshot the graph as the dict shape we save into the manifest."""
        states: dict[str, dict] = {}
        layout: dict[str, list[float]] = {}

        # Split edges by kind: at most one 'next' per src; any number of
        # triggers per src.
        next_by_src: dict[str, str] = {}
        triggers_by_src: dict[str, list[dict]] = {}
        for e in self._edges:
            if e.kind == EdgeItem.KIND_NEXT:
                next_by_src[e.src.state_name] = e.dst.state_name
            elif e.kind == EdgeItem.KIND_TRIGGER:
                triggers_by_src.setdefault(e.src.state_name, []).append({
                    "trigger": e.trigger,
                    "target":  e.dst.state_name,
                })

        for name, node in self._nodes.items():
            entry: dict = {"animation": node.animation, "loop": node.loop}
            if not node.loop and name in next_by_src:
                entry["next"] = next_by_src[name]
            if name in triggers_by_src:
                entry["transitions"] = triggers_by_src[name]
            states[name] = entry
            pos = node.scenePos()
            layout[name] = [round(pos.x(), 1), round(pos.y(), 1)]

        return {
            "states":   states,
            "default":  self._default_name or "",
            "_layout":  layout,
        }

    def set_data(self, data: dict) -> None:
        """Rebuild the graph from the given dict (inverse of get_data)."""
        self.clear_graph()

        states = data.get("states", {}) or {}
        layout = data.get("_layout", {}) or {}
        for i, (name, st) in enumerate(states.items()):
            node = self._add_node_internal(
                name=name,
                animation=st.get("animation", ""),
                loop=bool(st.get("loop", True)),
                pos=self._layout_or_default(layout, name, i),
            )
            (void := node)  # keep linter quiet about unused

        # 'next' edges (auto-transition after one-shot completes).
        for name, st in states.items():
            nxt = st.get("next")
            if nxt and nxt in self._nodes and name in self._nodes:
                self._add_edge_internal(
                    self._nodes[name], self._nodes[nxt],
                    kind=EdgeItem.KIND_NEXT,
                )

        # Trigger edges (gameplay-fired transitions).
        for name, st in states.items():
            for tr in st.get("transitions", []) or []:
                target = tr.get("target", "")
                trigger = tr.get("trigger", "")
                if (name in self._nodes and target in self._nodes
                        and target != name and trigger):
                    self._add_edge_internal(
                        self._nodes[name], self._nodes[target],
                        kind=EdgeItem.KIND_TRIGGER, trigger=trigger,
                    )

        # Default flag.
        default = data.get("default", "")
        if default in self._nodes:
            self._set_default(default)

        self.graph_changed.emit()

    def clear_graph(self) -> None:
        # Detach edges first so node removal doesn't leave dangling backrefs.
        for e in list(self._edges):
            e.detach()
            self._scene.removeItem(e)
        self._edges.clear()
        for n in list(self._nodes.values()):
            self._scene.removeItem(n)
        self._nodes.clear()
        self._default_name = None

    def state_names(self) -> list[str]:
        return list(self._nodes.keys())

    # ── Mutation helpers used by dialogs ────────────────────────────────────

    def add_state(self, name: str, animation: str, loop: bool, pos: QPointF) -> bool:
        if not name or name in self._nodes:
            return False
        self._add_node_internal(name, animation, loop, pos)
        # First state added — make it default automatically so saves are
        # never invalid for trivial graphs.
        if len(self._nodes) == 1:
            self._set_default(name)
        self.graph_changed.emit()
        return True

    def update_state(self, name: str, animation: str, loop: bool) -> None:
        node = self._nodes.get(name)
        if node is None:
            return
        node.animation = animation
        node.loop      = loop
        node.update()
        # If the state stopped being a one-shot, drop any outgoing 'next'
        # edge — it wouldn't fire any more anyway. Trigger edges are kept
        # because they're independent of the loop flag.
        if loop:
            for e in list(self._edges):
                if e.src is node and e.kind == EdgeItem.KIND_NEXT:
                    self._remove_edge(e)
        self.graph_changed.emit()

    def delete_state(self, name: str) -> None:
        node = self._nodes.pop(name, None)
        if node is None:
            return
        for e in list(self._edges):
            if e.src is node or e.dst is node:
                self._remove_edge(e)
        self._scene.removeItem(node)
        if self._default_name == name:
            # Promote any other state to default so saves stay valid.
            self._default_name = next(iter(self._nodes), None)
            if self._default_name is not None:
                self._nodes[self._default_name].is_default = True
                self._nodes[self._default_name].update()
        self.graph_changed.emit()

    def set_next(self, src_name: str, dst_name: Optional[str]) -> None:
        """Replace the 'next' edge of `src_name`. dst_name=None removes it.
        Doesn't touch trigger edges from the same source."""
        src = self._nodes.get(src_name)
        if src is None:
            return
        for e in list(self._edges):
            if e.src is src and e.kind == EdgeItem.KIND_NEXT:
                self._remove_edge(e)
        if dst_name is None:
            self.graph_changed.emit()
            return
        dst = self._nodes.get(dst_name)
        if dst is None or dst is src:
            return
        self._add_edge_internal(src, dst, kind=EdgeItem.KIND_NEXT)
        self.graph_changed.emit()

    def add_transition(self, src_name: str, dst_name: str, trigger: str) -> bool:
        """Add a trigger-driven edge. Refuses duplicates of (src, trigger)
        because the runtime would only fire the first one anyway."""
        src = self._nodes.get(src_name)
        dst = self._nodes.get(dst_name)
        if src is None or dst is None or src is dst or not trigger:
            return False
        for e in self._edges:
            if (e.src is src and e.kind == EdgeItem.KIND_TRIGGER
                    and e.trigger == trigger):
                return False
        self._add_edge_internal(src, dst, kind=EdgeItem.KIND_TRIGGER,
                                trigger=trigger)
        self.graph_changed.emit()
        return True

    def update_transition(self, edge: "EdgeItem",
                          dst_name: str, trigger: str) -> bool:
        """Edit an existing trigger edge in place (keeps src constant)."""
        if edge not in self._edges or edge.kind != EdgeItem.KIND_TRIGGER:
            return False
        dst = self._nodes.get(dst_name)
        if dst is None or dst is edge.src or not trigger:
            return False
        # Reject if the rename would collide with another trigger from
        # the same source.
        for e in self._edges:
            if (e is not edge and e.src is edge.src
                    and e.kind == EdgeItem.KIND_TRIGGER
                    and e.trigger == trigger):
                return False
        # Re-attach to a different destination by replacing the edge —
        # cleanest way to refresh node back-pointers.
        if dst is not edge.dst:
            self._remove_edge(edge)
            self._add_edge_internal(edge.src, dst,
                                    kind=EdgeItem.KIND_TRIGGER, trigger=trigger)
        else:
            edge.trigger = trigger
            edge.refresh()
        self.graph_changed.emit()
        return True

    def remove_edge(self, edge: "EdgeItem") -> None:
        if edge in self._edges:
            self._remove_edge(edge)
            self.graph_changed.emit()

    def set_default(self, name: str) -> None:
        if name not in self._nodes:
            return
        self._set_default(name)
        self.graph_changed.emit()

    # ── Internal builders ───────────────────────────────────────────────────

    def _add_node_internal(self, name: str, animation: str,
                           loop: bool, pos: QPointF) -> StateNode:
        node = StateNode(name, animation, loop)
        node.setPos(pos)
        self._scene.addItem(node)
        self._nodes[name] = node
        return node

    def _add_edge_internal(self, src: StateNode, dst: StateNode,
                           kind: str = EdgeItem.KIND_NEXT,
                           trigger: str = "") -> EdgeItem:
        edge = EdgeItem(src, dst, kind=kind, trigger=trigger)
        self._scene.addItem(edge)
        self._edges.append(edge)
        return edge

    def _remove_edge(self, edge: EdgeItem) -> None:
        edge.detach()
        self._scene.removeItem(edge)
        if edge in self._edges:
            self._edges.remove(edge)

    def _set_default(self, name: str) -> None:
        if self._default_name and self._default_name in self._nodes:
            old = self._nodes[self._default_name]
            old.is_default = False
            old.update()
        self._default_name = name
        new = self._nodes[name]
        new.is_default = True
        new.update()

    def _layout_or_default(self, layout: dict, name: str, idx: int) -> QPointF:
        if name in layout:
            xy = layout[name]
            return QPointF(float(xy[0]), float(xy[1]))
        # Auto-grid: 3 columns, _NODE_W+40 / _NODE_H+40 spacing, starting at
        # the visible origin so users always see new nodes immediately.
        col = idx % 3
        row = idx // 3
        return QPointF(40 + col * (_NODE_W + 40), 40 + row * (_NODE_H + 40))

    # ── Right-click menus ───────────────────────────────────────────────────

    def contextMenuEvent(self, event) -> None:
        item = self.itemAt(event.pos())
        scene_pos = self.mapToScene(event.pos())

        if isinstance(item, StateNode):
            self._node_menu(item, event.globalPos())
            return
        if isinstance(item, EdgeItem):
            self._edge_menu(item, event.globalPos())
            return

        # Canvas.
        menu = QMenu(self)
        act_add = QAction("Add state…", self)
        act_add.triggered.connect(
            lambda: self.add_state_callback(scene_pos) if self.add_state_callback else None
        )
        menu.addAction(act_add)
        menu.exec(event.globalPos())

    def _node_menu(self, node: StateNode, global_pos) -> None:
        menu = QMenu(self)
        act_edit    = QAction("Edit…", self)
        act_default = QAction("Set as default", self)
        act_default.setEnabled(not node.is_default)

        # 'Set next…' submenu populated with sibling states. Disabled for
        # looping states because 'next' has no effect for them.
        next_menu = menu.addMenu("Set next →")
        next_menu.setEnabled(not node.loop)
        for sibling_name in self._nodes:
            if sibling_name == node.state_name:
                continue
            a = QAction(sibling_name, self)
            a.triggered.connect(
                lambda checked=False, n=sibling_name: self.set_next(node.state_name, n)
            )
            next_menu.addAction(a)
        clear_action = QAction("(clear)", self)
        clear_action.triggered.connect(lambda: self.set_next(node.state_name, None))
        next_menu.addSeparator()
        next_menu.addAction(clear_action)

        act_add_trans = QAction("Add transition…", self)
        act_add_trans.triggered.connect(
            lambda: self.add_transition_callback(node.state_name)
                    if self.add_transition_callback else None
        )

        act_delete  = QAction("Delete", self)

        act_edit.triggered.connect(
            lambda: self.edit_state_callback(node.state_name)
                    if self.edit_state_callback else None
        )
        act_default.triggered.connect(lambda: self.set_default(node.state_name))
        act_delete.triggered.connect(lambda: self.delete_state(node.state_name))

        menu.addAction(act_edit)
        menu.addAction(act_default)
        menu.addAction(act_add_trans)
        menu.addSeparator()
        menu.addAction(act_delete)
        menu.exec(global_pos)

    def _edge_menu(self, edge: "EdgeItem", global_pos) -> None:
        menu = QMenu(self)
        if edge.kind == EdgeItem.KIND_TRIGGER:
            act_edit = QAction(f"Edit transition '{edge.trigger}'…", self)
            act_edit.triggered.connect(
                lambda: self.edit_transition_callback(edge)
                        if self.edit_transition_callback else None
            )
            menu.addAction(act_edit)
        act_delete = QAction("Delete edge", self)
        act_delete.triggered.connect(lambda: self.remove_edge(edge))
        menu.addAction(act_delete)
        menu.exec(global_pos)
