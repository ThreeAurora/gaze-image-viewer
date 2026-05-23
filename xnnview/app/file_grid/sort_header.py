"""排序头部"""
from PySide6.QtWidgets import QWidget, QHBoxLayout, QPushButton
from PySide6.QtCore import Signal

from app.constants import C_CONTENT

# 排序列常量
SORT_NAME, SORT_SIZE, SORT_TYPE, SORT_EXT, SORT_CTIME, SORT_MTIME, SORT_EXIF = range(7)
SORT_COLUMNS = [
    (SORT_NAME,  '文件名',    180),
    (SORT_SIZE,  '尺寸',      80),
    (SORT_TYPE,  '类型',      80),
    (SORT_EXT,   '扩展名',    70),
    (SORT_CTIME, '创建日期',  170),
    (SORT_MTIME, '修改日期',  170),
    (SORT_EXIF,  '拍摄日期',  170),
]


class SortHeader(QWidget):
    sort_changed = Signal(int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(26)
        self.setAcceptDrops(True)
        self.setStyleSheet(f'background: {C_CONTENT}; border-bottom: 1px solid #333;')
        layout = QHBoxLayout(self); layout.setContentsMargins(8, 0, 8, 0); layout.setSpacing(0)
        self._layout = layout
        self._col = -1; self._asc = True
        self._btns: dict[int, QPushButton] = {}
        self._col_order = [cid for cid, _, _ in SORT_COLUMNS]
        btn_style = (
            'QPushButton { background: transparent; color: #AAA; border: none;'
            'padding: 3px 8px; font-size: 11px; text-align: left; }'
            'QPushButton:hover { background: #2A2A2A; color: #EEE; }'
            'QPushButton:checked { color: #0078D7; font-weight: bold; }'
        )
        for col_id, label, width in SORT_COLUMNS:
            btn = DraggableButton(label, col_id, self)
            btn.setCheckable(True); btn.setFixedHeight(24); btn.setMinimumWidth(width)
            btn.setStyleSheet(btn_style)
            btn.clicked.connect(lambda checked, c=col_id, b=btn: self._on_click(c, b))
            layout.addWidget(btn)
            self._btns[col_id] = btn
        self._col = SORT_NAME
        self._btns[SORT_NAME].setChecked(True)
        self._btns[SORT_NAME].setText('文件名 ▲')
        layout.addStretch()

    def swap_columns(self, from_id: int, to_id: int):
        if from_id == to_id: return
        # 重排按钮
        idx_from = self._col_order.index(from_id)
        idx_to = self._col_order.index(to_id)
        self._col_order.pop(idx_from)
        self._col_order.insert(idx_to, from_id)
        # 重建布局
        for i in reversed(range(self._layout.count())):
            item = self._layout.itemAt(i)
            if item.widget() and isinstance(item.widget(), DraggableButton):
                self._layout.removeWidget(item.widget())
        for cid in self._col_order:
            self._layout.insertWidget(self._layout.count() - 1, self._btns[cid])


class DraggableButton(QPushButton):
    def __init__(self, text, col_id, header):
        super().__init__(text)
        self._col_id = col_id; self._header = header
        self._dragging = False; self._press_pos = None

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._press_pos = event.globalPosition().toPoint()
            self._dragging = False
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._press_pos is not None and not self._dragging:
            delta = (event.globalPosition().toPoint() - self._press_pos).manhattanLength()
            if delta > 15:
                self._dragging = True
                self._press_pos = None
                target = self._header.childAt(event.globalPosition().toPoint() -
                                              self._header.mapToGlobal(self._header.rect().topLeft()))
                if target and isinstance(target, DraggableButton) and target is not self:
                    self._header.swap_columns(self._col_id, target._col_id)
                return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._press_pos = None; self._dragging = False
        super().mouseReleaseEvent(event)

    def _on_click(self, col_id: int, btn: QPushButton):
        if self._col == col_id: self._asc = not self._asc
        else:
            for b in self._btns.values(): b.setChecked(False)
            self._col = col_id; self._asc = True
        btn.setChecked(True)
        arrow = ' ▲' if self._asc else ' ▼'
        for cid, (_, label, _) in enumerate(SORT_COLUMNS):
            b = self._btns.get(cid)
            if b: b.setText(label + (arrow if cid == self._col else ''))
        self.sort_changed.emit(self._col, self._asc)
