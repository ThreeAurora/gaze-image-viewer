"""文件夹树 + 自定义箭头"""
import os
from pathlib import Path

from PySide6.QtWidgets import (
    QTreeWidget, QTreeWidgetItem, QApplication, QStyle, QProxyStyle,
)
from PySide6.QtCore import Qt
from PySide6.QtCore import QPointF
from PySide6.QtGui import QIcon, QPixmap, QPainter, QColor, QBrush, QPolygonF, QPen

from app.constants import C_SIDEBAR, C_TREE_TEXT, C_TREE_HOVER, C_TREE_SELECT


def _has_subfolders(dir_path: str) -> bool:
    try:
        with os.scandir(dir_path) as it:
            for e in it:
                if e.is_dir() and not e.name.startswith('.'):
                    return True
    except: pass
    return False


class ArrowStyle(QProxyStyle):
    def drawPrimitive(self, element, option, painter, widget=None):
        from PySide6.QtWidgets import QStyle
        if element == QStyle.PrimitiveElement.PE_IndicatorBranch:
            painter.save()
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setPen(Qt.PenStyle.NoPen)
            cx = option.rect.center().x(); cy = option.rect.center().y(); s = 5
            if option.state & QStyle.StateFlag.State_Open:
                painter.setBrush(QBrush(QColor('#AAAAAA')))
                pts = [QPointF(cx - s, cy - s * 0.5), QPointF(cx + s, cy - s * 0.5), QPointF(cx, cy + s * 0.8)]
            elif option.state & QStyle.StateFlag.State_Children:
                painter.setBrush(QBrush(QColor('#888888')))
                pts = [QPointF(cx - s * 0.5, cy - s), QPointF(cx - s * 0.5, cy + s), QPointF(cx + s * 0.8, cy)]
            else:
                painter.restore(); return
            painter.drawPolygon(pts); painter.restore()
        else:
            super().drawPrimitive(element, option, painter, widget)


class FolderTree(QTreeWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setHeaderHidden(True); self.setIndentation(16)
        self.setAnimated(False)
        self._icons = self._make_icons()
        self.setStyleSheet(f"""
            QTreeWidget {{ background: {C_SIDEBAR}; color: {C_TREE_TEXT}; border: none;
                          outline: none; font-size: 12px; }}
            QTreeWidget::item {{ padding: 3px 6px; border-radius: 2px; }}
            QTreeWidget::item:hover {{ background: {C_TREE_HOVER}; }}
            QTreeWidget::item:selected {{ background: {C_TREE_SELECT}; }}
        """)
        self.setStyle(ArrowStyle())
        self.itemClicked.connect(self._on_clicked)
        self.itemExpanded.connect(lambda item: self.load_children(item))

    def _make_icons(self):
        icons = {}
        def _draw_folder(color: QColor, w=20, h=17) -> QIcon:
            pix = QPixmap(w, h); pix.fill(Qt.GlobalColor.transparent)
            p = QPainter(pix); p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QBrush(color))
            p.drawRoundedRect(0, 3, w, h - 3, 2, 2)
            p.setBrush(QBrush(color.lighter(130)))
            p.drawRoundedRect(0, 0, int(w * 0.55), int(h * 0.35), 1, 1)
            p.end(); return QIcon(pix)
        def _draw_drive(color: QColor, w=24, h=18) -> QIcon:
            pix = QPixmap(w, h); pix.fill(Qt.GlobalColor.transparent)
            p = QPainter(pix); p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setPen(Qt.PenStyle.NoPen)
            # 扁平斜视角硬盘——多边形模拟3D
            skew = 3  # 倾斜量
            body = [
                QPointF(skew, 1), QPointF(w - 2, 1),
                QPointF(w - 2 - skew, h - 3), QPointF(0, h - 3)
            ]
            p.setBrush(QBrush(color))
            p.drawPolygon(body)
            # 顶面金属盖
            top_skew = skew * 0.6
            top = [
                QPointF(skew + 1, 2), QPointF(w - 4, 2),
                QPointF(w - 4 - top_skew, h - 5), QPointF(top_skew + 1, h - 5)
            ]
            p.setBrush(QBrush(color.lighter(115)))
            p.drawPolygon(top)
            # 标签贴纸
            label = [
                QPointF(skew + 3, 4), QPointF(w - 8, 4),
                QPointF(w - 8 - top_skew, h - 8), QPointF(top_skew + 3, h - 8)
            ]
            p.setBrush(QBrush(QColor('#EEE')))
            p.drawPolygon(label)
            p.end(); return QIcon(pix)
        def _draw_desktop(color: QColor, w=20, h=18) -> QIcon:
            pix = QPixmap(w, h); pix.fill(Qt.GlobalColor.transparent)
            p = QPainter(pix); p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setPen(Qt.PenStyle.NoPen)
            # 屏幕外框
            p.setBrush(QBrush(QColor('#333')))
            p.drawRoundedRect(1, 0, w - 2, int(h * 0.72), 3, 3)
            # 屏幕显示区
            p.setBrush(QBrush(color))
            p.drawRoundedRect(3, 2, w - 6, int(h * 0.6), 2, 2)
            # 底座
            p.setBrush(QBrush(QColor('#555')))
            p.drawRoundedRect(int(w * 0.25), int(h * 0.75), int(w * 0.5), int(h * 0.1), 1, 1)
            # 支架
            p.setBrush(QBrush(QColor('#444')))
            p.drawRect(int(w * 0.42), int(h * 0.72), int(w * 0.16), int(h * 0.08))
            p.end(); return QIcon(pix)
        icons['folder']  = _draw_folder(QColor('#F9C74F'))
        icons['drive']   = _draw_drive(QColor('#7B9AAA'))
        icons['desktop'] = _draw_desktop(QColor('#3B82F6'))
        return icons

    def _on_clicked(self, item: QTreeWidgetItem, col: int):
        """单击：加载子项 + 直接导航"""
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if not path: return
        self.load_children(item)
        if os.path.isdir(path):
            self._nav(path)

    def _nav(self, path: str):
        pw = self.parent()
        while pw and not hasattr(pw, '_navigate_to'): pw = pw.parent()
        if pw and hasattr(pw, '_navigate_to'):
            # 从文件树触发时标记，跳过 focus_path 避免额外 I/O
            self._from_tree = True
            pw._navigate_to(path)
            self._from_tree = False

    def focus_path(self, dir_path: str):
        root = self.invisibleRootItem()
        # 精确匹配顶级项（桌面等非盘符路径），盘符路径走正常树展开
        is_drive_path = len(dir_path) >= 3 and dir_path[1] == ':'
        if not is_drive_path:
            for i in range(root.childCount()):
                child = root.child(i)
                if child.data(0, Qt.ItemDataRole.UserRole) == dir_path:
                    self.setCurrentItem(child); self.scrollToItem(child)
                    return
        # 正常路径拆分展开
        parts = Path(dir_path).parts
        if not parts: return
        current = ''
        parent = root
        for part in parts:
            current = str(Path(current) / part)
            if current.endswith(':'): current += '\\'
            self.load_children(parent)
            parent.setExpanded(True)
            found = None
            for i in range(parent.childCount()):
                child = parent.child(i)
                if child.data(0, Qt.ItemDataRole.UserRole) == current:
                    found = child; break
            if found: parent = found
            else: break
        if parent is not self.invisibleRootItem():
            self.setCurrentItem(parent); self.scrollToItem(parent)

    def load_drives(self):
        self.clear()
        # 桌面（Windows API 获取真实路径）
        import ctypes
        buf = ctypes.create_unicode_buffer(260)
        ctypes.windll.shell32.SHGetFolderPathW(0, 0, 0, 0, buf)
        desktop = buf.value
        if desktop and os.path.isdir(desktop):
            item = QTreeWidgetItem(['桌面'])
            item.setData(0, Qt.ItemDataRole.UserRole, desktop)
            item.setData(0, Qt.ItemDataRole.UserRole + 1, False)
            item.setIcon(0, self._icons['desktop'])
            item.setChildIndicatorPolicy(QTreeWidgetItem.ChildIndicatorPolicy.ShowIndicator)
            self.addTopLevelItem(item)
        for letter in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
            root = f'{letter}:\\'
            if os.path.exists(root):
                item = QTreeWidgetItem([f'本地磁盘 ({letter}:)'])
                item.setData(0, Qt.ItemDataRole.UserRole, root)
                item.setData(0, Qt.ItemDataRole.UserRole + 1, False)
                item.setIcon(0, self._icons['drive'])
                item.setChildIndicatorPolicy(QTreeWidgetItem.ChildIndicatorPolicy.ShowIndicator)
                self.addTopLevelItem(item)

    def load_children(self, item: QTreeWidgetItem):
        path = item.data(0, Qt.ItemDataRole.UserRole)
        if not path: return
        if item.data(0, Qt.ItemDataRole.UserRole + 1): return
        item.setData(0, Qt.ItemDataRole.UserRole + 1, True)
        try:
            for e in sorted(Path(path).iterdir(), key=lambda e: e.name.lower()):
                if e.is_dir() and not e.name.startswith('.'):
                    child = QTreeWidgetItem([e.name])
                    child.setData(0, Qt.ItemDataRole.UserRole, str(e))
                    child.setData(0, Qt.ItemDataRole.UserRole + 1, False)
                    child.setIcon(0, self._icons['folder'])
                    item.addChild(child)
                    if _has_subfolders(str(e)):
                        child.setChildIndicatorPolicy(QTreeWidgetItem.ChildIndicatorPolicy.ShowIndicator)
                    else:
                        child.setChildIndicatorPolicy(QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
        except PermissionError: pass
        if item.childCount() == 0:
            item.setChildIndicatorPolicy(QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
