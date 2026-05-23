"""主窗口 —— Xnnview"""
import os
from pathlib import Path

from PySide6.QtWidgets import (
    QMainWindow, QSplitter, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QSlider, QToolBar,
    QMessageBox, QApplication, QStatusBar, QMenuBar,
)
from PySide6.QtCore import Qt, QTimer, QSize
from PySide6.QtGui import QIcon, QPixmap, QPainter, QPen, QColor, QBrush, QFont, QFontMetrics, QAction, QShortcut

from app.constants import C_TOOLBAR_BG, C_STATUSBAR_BG, C_STATUSBAR_TXT, C_SEPARATOR
from app.folder_tree import FolderTree
from app.file_grid import FileGrid, SortHeader, format_size, format_date
from app.preview_panel import PreviewPanel


def create_app_icon() -> QIcon:
    """绘制应用图标 —— 多彩 X 字母"""
    pix = QPixmap(64, 64); pix.fill(Qt.GlobalColor.transparent)
    p = QPainter(pix); p.setRenderHint(QPainter.RenderHint.Antialiasing)
    # 圆角方形背景
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QBrush(QColor('#1A1A1A')))
    p.drawRoundedRect(2, 2, 60, 60, 12, 12)
    # 多彩 X
    pen = QPen(QColor('#F9C74F'), 6); pen.setCapStyle(Qt.PenCapStyle.RoundCap)
    p.setPen(pen); p.drawLine(18, 18, 46, 46)
    pen.setColor(QColor('#0078D7')); p.setPen(pen)
    p.drawLine(46, 18, 18, 46)
    p.end()
    return QIcon(pix)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('Xnnview')
        self.setMinimumSize(1000, 650); self.resize(1500, 900)
        self.setWindowIcon(create_app_icon())
        self.setStyleSheet(f'background: {C_TOOLBAR_BG};')

        central = QWidget(); self.setCentralWidget(central)
        ml = QVBoxLayout(central); ml.setContentsMargins(0, 0, 0, 0); ml.setSpacing(0)

        self._create_menubar()
        self._create_toolbar(ml)

        self._splitter = QSplitter(Qt.Orientation.Horizontal)
        self._splitter.setStyleSheet(f'QSplitter::handle {{ background: {C_SEPARATOR}; width: 1px; }}')
        ml.addWidget(self._splitter, 1)

        self.folder_tree = FolderTree(); self.folder_tree.setMinimumWidth(160)
        self._splitter.addWidget(self.folder_tree)

        center_panel = QWidget(); center_panel.setMinimumWidth(200)
        cl = QVBoxLayout(center_panel); cl.setContentsMargins(0, 0, 0, 0); cl.setSpacing(0)
        self.sort_header = SortHeader(); cl.addWidget(self.sort_header)
        self.file_grid = FileGrid(); cl.addWidget(self.file_grid, 1)
        self._filter_btn.clicked.connect(self.file_grid.toggle_filter)
        self.file_grid.file_count_changed.connect(self._update_status)
        self.file_grid.selection_changed.connect(self._on_selection_changed)
        self.file_grid.thumb_zoom.connect(self._on_thumb_zoom)
        self.file_grid.delete_requested.connect(self._on_delete_file)
        self.file_grid.new_folder_requested.connect(self._on_new_folder)
        self.sort_header.sort_changed.connect(self.file_grid.sort)
        self._splitter.addWidget(center_panel)

        self.preview = PreviewPanel(); self.preview.setMinimumWidth(200)
        self.preview.nav_file.connect(self.file_grid.navigate_selection)
        self._splitter.addWidget(self.preview)
        self._splitter.setSizes([270, 570, 660])

        self._status: QLabel | None = None
        self._create_statusbar()

        self.folder_tree.load_drives()
        # 默认选中并打开"桌面"
        desktop = self.folder_tree.topLevelItem(0)
        if desktop:
            desktop_path = desktop.data(0, Qt.ItemDataRole.UserRole)
            self.folder_tree.setCurrentItem(desktop)
            self._navigate_to(desktop_path)
        else:
            self._navigate_to('C:\\')

        # 全局事件过滤器：空格键 / Ctrl+PgUp/PgDn
        QApplication.instance().installEventFilter(self)

        # 悬停浮动信息面板
        self._tooltip = QLabel(None, Qt.WindowType.ToolTip)
        self._tooltip.setStyleSheet(
            'background: rgba(20,20,20,240); color: #CCC; font-size: 11px;'
            'padding: 8px 12px; border-radius: 6px; border: 1px solid #444;')
        self._tooltip.setFixedWidth(300)
        self._tooltip.hide()
        self._tooltip.raise_()

    def _show_tooltip(self, card):
        if getattr(self, '_fullscreen_mode', False): return
        fp = card.file_path
        p = Path(fp)
        # 文件名折行（最多5行）
        max_w = 320
        name = p.name
        self._tooltip.setWordWrap(True)
        self._tooltip.setMaximumWidth(max_w)
        self._tooltip.setFont(QFont('Microsoft YaHei', 12))
        fm = self._tooltip.fontMetrics()
        # 用fontMetrics估算折行
        if fm.horizontalAdvance(name) > max_w:
            wrapped = ''
            line = ''
            for ch in name:
                test = line + ch
                if fm.horizontalAdvance(test) > max_w - 20:
                    wrapped += test + '\n'
                    line = ''
                else:
                    line = test
            if line: wrapped += line
            name_html = wrapped.replace('\n', '<br>')
        else:
            name_html = name
        # 限制最多5行
        lines = name_html.split('<br>')
        if len(lines) > 5:
            name_html = '<br>'.join(lines[:4]) + '<br>' + lines[4][:30] + '...'

        is_file = p.is_file()
        size = format_size(p.stat().st_size) if is_file else '--'
        tip = (
            f'<b style="color:#FFF;font-size:12px">{name_html}</b><br>'
            f'<span style="color:#AAA;font-size:11px">'
            f'路径: {p.parent}<br>'
            f'大小: {size}<br>'
            f'扩展名: {p.suffix or "文件夹"}'
        )
        try:
            st = p.stat()
            from datetime import datetime
            tip += (f'<br>创建时间: {datetime.fromtimestamp(st.st_ctime).strftime("%Y/%m/%d %H:%M")}'
                    f'<br>修改时间: {datetime.fromtimestamp(st.st_mtime).strftime("%Y/%m/%d %H:%M")}')
        except: pass
        tip += '</span>'
        self._tooltip.setText(tip)
        self._tooltip.setFont(QFont('Microsoft YaHei', 10))
        self._tooltip.adjustSize()
        gp = card.mapToGlobal(card.rect().topRight())
        x = min(gp.x() + 8, self.geometry().right() - self._tooltip.width() - 10)
        y = max(self.geometry().top(), min(gp.y(), self.geometry().bottom() - self._tooltip.height()))
        self._tooltip.move(x, y)
        self._tooltip.show()
        self._tooltip.raise_()

    def _hide_tooltip(self):
        self._tooltip.hide()

    def hide_tooltip_now(self):
        """公开接口：全屏切换时强制隐藏提示框"""
        self._tooltip.hide()

        # 全局快捷键
        # Ctrl+PgUp / Ctrl+PgDn：视频快退/快进3秒
        seek_back = QAction(self)
        seek_back.setShortcut(Qt.Modifier.CTRL | Qt.Key.Key_PageUp)
        seek_back.triggered.connect(lambda: self.preview.seek_delta(-3))
        self.addAction(seek_back)
        seek_fwd = QAction(self)
        seek_fwd.setShortcut(Qt.Modifier.CTRL | Qt.Key.Key_PageDown)
        seek_fwd.triggered.connect(lambda: self.preview.seek_delta(3))
        self.addAction(seek_fwd)

    def _create_menubar(self):
        mb = self.menuBar()
        mb.setStyleSheet(
            'QMenuBar { background: #1A1A1A; color: #CCC; font-size: 12px; padding: 2px;'
            'border-bottom: 1px solid #333; }'
            'QMenuBar::item { padding: 4px 10px; }'
            'QMenuBar::item:selected { background: #0078D7; color: #FFF; }'
            'QMenu { background: #252525; color: #CCC; border: 1px solid #444; padding: 4px; }'
            'QMenu::item { padding: 5px 30px; }'
            'QMenu::item:selected { background: #0078D7; color: #FFF; }'
            'QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }')
        f = mb.addMenu('文件(&F)')
        f.addAction('打开(&O)...', lambda: None); f.addSeparator()
        f.addAction('退出(&X)', self.close)
        v = mb.addMenu('查看(&V)')
        v.addAction('缩略图视图', lambda: None); v.addAction('详细列表', lambda: None)
        t = mb.addMenu('工具(&T)')
        t.addAction('格式转换...', lambda: None); t.addAction('批量重命名...', lambda: None)
        h = mb.addMenu('帮助(&H)')
        h.addAction('快捷键帮助(&K)', lambda: self._show_shortcuts_dialog())
        h.addAction('关于(&A)', lambda: self._show_about_dialog())

    def _create_toolbar(self, layout):
        bar = QWidget(); bar.setFixedHeight(36)
        bar.setStyleSheet(f'background: {C_TOOLBAR_BG}; border-bottom: 1px solid {C_SEPARATOR};')
        hl = QHBoxLayout(bar); hl.setContentsMargins(8, 4, 8, 4); hl.setSpacing(6)
        btn = ('QPushButton { background: transparent; color: #CCC; border: 1px solid #444;'
               'padding: 4px 8px; border-radius: 3px; font-size: 12px; }'
               'QPushButton:hover { background: #333; border-color: #0078D7; }')
        for text, tip in [('←','后退'), ('→','前进'), ('↑','上级目录')]:
            b = QPushButton(text); b.setToolTip(tip); b.setStyleSheet(btn); b.setFixedSize(30, 26)
            if text == '↑': b.clicked.connect(lambda: self._navigate_to('..'))
            hl.addWidget(b)
        # ☆ 筛选
        self._filter_btn = QPushButton('已标记')
        self._filter_btn.setCheckable(True); self._filter_btn.setToolTip('仅显示已标记')
        self._filter_btn.setStyleSheet(
            'QPushButton{background:transparent;color:#888;border:1px solid #444;'
            'padding:2px 8px;border-radius:3px;font-size:11px;}'
            'QPushButton:hover{color:#FF8800;border-color:#FF8800;}'
            'QPushButton:checked{color:#FF8800;border-color:#FF8800;}')
        self._filter_btn.setFixedHeight(26); hl.addWidget(self._filter_btn)
        # 排序按钮
        self._sort_btn = QPushButton('排序 ▼')
        self._sort_btn.setStyleSheet(btn); self._sort_btn.setFixedHeight(26)
        self._sort_btn.clicked.connect(self._show_sort_menu); hl.addWidget(self._sort_btn)
        self._addr_bar = QLineEdit(); self._addr_bar.setFixedHeight(26)
        self._addr_bar.setStyleSheet(
            'QLineEdit { background: #111; color: #CCC; border: 1px solid #444;'
            'border-radius: 3px; padding: 2px 8px; font-size: 12px; }'
            'QLineEdit:focus { border-color: #0078D7; }')
        self._addr_bar.returnPressed.connect(lambda: self._navigate_to(self._addr_bar.text()))
        hl.addWidget(self._addr_bar, 1)
        lbl_style = 'color: #AAA; font-size: 11px; background: transparent;'
        hl.addWidget(QLabel(' 缩略图: '))
        self._size_slider = QSlider(Qt.Orientation.Horizontal)
        self._size_slider.setRange(80, 300); self._size_slider.setValue(160); self._size_slider.setFixedWidth(100)
        self._size_slider.setStyleSheet(
            'QSlider::groove:horizontal { height: 4px; background: #444; border-radius: 2px; }'
            'QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;'
            '  background: #0078D7; border-radius: 7px; }')
        self._size_slider.valueChanged.connect(self._on_size); hl.addWidget(self._size_slider)
        self._size_label = QLabel('160 px'); self._size_label.setFixedWidth(45)
        self._size_label.setStyleSheet(lbl_style); hl.addWidget(self._size_label)
        layout.addWidget(bar)

    def _create_statusbar(self):
        sb = QStatusBar()
        sb.setStyleSheet(f'QStatusBar {{ background: {C_STATUSBAR_BG}; border-top: 1px solid {C_SEPARATOR};'
                         f'  color: {C_STATUSBAR_TXT}; font-size: 11px; padding: 2px 8px; }}')
        sb.setFixedHeight(40)
        self._status = QLabel('')
        self._status.setStyleSheet(f'color: {C_STATUSBAR_TXT}; background: transparent;')
        sb.addWidget(self._status, 1); self.setStatusBar(sb)

    def _navigate_to(self, path: str):
        path = path.strip()
        if path == '..':
            cur = self._addr_bar.text()
            parent = str(Path(cur).parent) if cur else 'C:\\'
            path = parent if os.path.isdir(parent) else cur
        if not os.path.isdir(path): return
        self._addr_bar.setText(path)
        self.file_grid.load_directory(path)
        self.preview.clear()
        self.setWindowTitle(f'{path} - Xnnview')
        # 文件树触发的导航跳过路径展开，只选中当前项
        if getattr(self.folder_tree, '_from_tree', False):
            item = self.folder_tree.currentItem()
            if item: self.folder_tree.scrollToItem(item)
        else:
            self.folder_tree.focus_path(path)

    def _update_status(self):
        fc = self.file_grid.file_count; sc = self.file_grid.selected_count
        ss = self.file_grid.selected_size
        line1 = f'{fc}个 / 选中了{sc}个 [{format_size(ss)}]'
        line2 = ''
        if sc == 1 and self.file_grid.selected_paths:
            fp = self.file_grid.selected_paths[0]
            try:
                pix = self.preview._orig_pix or QPixmap(fp)
                if not pix.isNull():
                    w, h, d = pix.width(), pix.height(), pix.depth()
                    ratio = w / h if h else 0
                    size = format_size(Path(fp).stat().st_size)
                    mtime = format_date(Path(fp).stat().st_mtime)
                    name = Path(fp).name
                    if len(name) > 35:
                        name = name[:17] + '...' + name[-15:]
                    pw, ph = w / 72, h / 72
                    line2 = (f'{name} {w}x{h}x{d} ({ratio:.2f}) '
                             f'{pw:.2f}x{ph:.2f}英寸 {size} {mtime}')
            except: pass
        if self.preview._orig_pix:
            pct = f' {self.preview.scale_percent}%'
            self._status.setText(f'{line1}{line2}{pct}')
        else:
            self._status.setText(f'{line1}  {line2}'.rstrip())

    def _on_selection_changed(self, path: str):
        self.preview.load_file(path); self._update_status()
        if path:
            self.setWindowTitle(f'{path} - Xnnview')
        else:
            self.setWindowTitle(f'{self._addr_bar.text()} - Xnnview')

    def eventFilter(self, obj, event):
        from PySide6.QtCore import QEvent
        t = event.type()
        if t == QEvent.Type.KeyPress:
            k = event.key()
            if k == Qt.Key.Key_Space and not isinstance(obj, QLineEdit):
                self.preview.toggle_play_pause()
                return True
            if k in (Qt.Key.Key_PageUp, Qt.Key.Key_PageDown):
                if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
                    self.preview.seek_delta(-3 if k == Qt.Key.Key_PageUp else 3)
                    return True
        elif t == QEvent.Type.Wheel:
            right = bool(QApplication.mouseButtons() & Qt.MouseButton.RightButton)
            if right:
                delta = event.angleDelta().y()
                self.preview.seek_all_modes(delta)
                return True
        return super().eventFilter(obj, event)

    def _on_thumb_zoom(self, delta: int):
        """Ctrl+滚轮缩放缩略图"""
        val = self._size_slider.value() + delta * 10
        val = max(80, min(300, val))
        self._size_slider.setValue(val)

    def _on_delete_file(self, path: str):
        from send2trash import send2trash
        from PySide6.QtWidgets import QMessageBox
        reply = QMessageBox.question(self, '确认删除', f'确定要删除到回收站？\n{Path(path).name}',
                                      QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            send2trash(path)
            self._navigate_to(self._addr_bar.text())  # 刷新

    def _on_new_folder(self, parent_dir: str):
        from PySide6.QtWidgets import QInputDialog
        name, ok = QInputDialog.getText(self, '新建文件夹', '文件夹名称：')
        if ok and name.strip():
            new_path = str(Path(parent_dir) / name.strip())
            os.makedirs(new_path, exist_ok=True)
            self._navigate_to(parent_dir)
            self.folder_tree.load_children(self.folder_tree.currentItem() or self.folder_tree.invisibleRootItem())

    def _show_sort_menu(self):
        from PySide6.QtWidgets import QMenu
        from PySide6.QtCore import QTimer
        # 刚关闭的菜单不要重开
        if getattr(self, '_menu_closing', False): return
        if hasattr(self, '_sort_menu') and self._sort_menu and \
           self._sort_menu.isVisible():
            self._sort_menu.close(); return
        self._sort_menu = QMenu(self)
        menu = self._sort_menu
        menu.aboutToHide.connect(lambda: self._on_menu_closing())
        menu.setStyleSheet(
            'QMenu{background:#252525;color:#CCC;border:1px solid #444;padding:4px;}'
            'QMenu::item{padding:5px 24px;}'
            'QMenu::item:selected{background:#0078D7;color:#FFF;}'
            'QMenu::separator{height:1px;background:#444;margin:4px 8px;}')
        # 所有排序字段
        columns = [
            ('文件名', 0), ('扩展名', 3), ('类型', 2),
            ('文件大小', 1), ('修改日期', 5), ('创建日期', 4),
            ('EXIF 拍摄日期', 6), ('路径', -2), ('☆ 已标记', -3),
            ('图像宽度', -4), ('图像高度', -5),
        ]
        for name, col in columns:
            menu.addAction(name, lambda c=col: self.file_grid.sort(c, True))
        menu.addSeparator()
        cur_asc = self.file_grid._sort_asc
        asc = menu.addAction('升序 ▲'); asc.setCheckable(True); asc.setChecked(cur_asc)
        desc = menu.addAction('降序 ▼'); desc.setCheckable(True); desc.setChecked(not cur_asc)
        asc.triggered.connect(lambda: self.file_grid.sort(self.file_grid._sort_col, True))
        desc.triggered.connect(lambda: self.file_grid.sort(self.file_grid._sort_col, False))
        menu.addSeparator()
        menu.addAction('显示列标题', lambda: self.sort_header.setVisible(not self.sort_header.isVisible()))
        gp = self._sort_btn.mapToGlobal(self._sort_btn.rect().bottomLeft())
        menu.popup(gp)

    def _on_menu_closing(self):
        self._menu_closing = True
        from PySide6.QtCore import QTimer
        QTimer.singleShot(200, lambda: setattr(self, '_menu_closing', False))

    def _show_shortcuts_dialog(self):
        from PySide6.QtWidgets import QDialog, QVBoxLayout, QLabel, QPushButton, QGridLayout, QHBoxLayout
        d = QDialog(self)
        d.setWindowTitle('快捷键帮助')
        d.setFixedSize(650, 360)
        d.setStyleSheet(
            'QDialog{background:#1A1A1A;border:1px solid #333;}'
            'QLabel{color:#CCC;background:transparent;}'
            'QPushButton{background:#333;color:#CCC;border:none;padding:6px 24px;border-radius:3px;font-size:12px;}'
            'QPushButton:hover{background:#444;}')
        layout = QVBoxLayout(d); layout.setContentsMargins(24, 20, 24, 16); layout.setSpacing(12)

        title = QLabel('<b style="font-size:15px;color:#FFF">快捷键帮助</b>'); layout.addWidget(title)

        # 三列布局
        cols = QHBoxLayout(); cols.setSpacing(30)
        sections = [
            ('文件导航', [
                '<b>C / ← / ↑</b> — 上一个',
                '<b>V / → / ↓</b> — 下一个',
                '<b>滚轮</b> — 切换文件',
                '<b>Ctrl+滚轮</b> — 缩放缩略图',
                '<b>双击文件夹</b> — 进入',
            ]),
            ('标记 & 操作', [
                '<b>F</b> — 标记/取消标记',
                '<b>D</b> — 清除所有标记',
                '<b>S</b> — 删除到回收站',
                '<b>X</b> — 新建文件夹',
                '<b>☆ 按钮</b> — 筛选已标记',
            ]),
            ('播放 & 预览', [
                '<b>空格</b> — 播放/暂停',
                '<b>Ctrl+PgUp/PgDn</b> — 快退/快进 3 秒',
                '<b>右键+滚轮上/下</b> — 快退/快进 3 秒',
                '<b>Ctrl+滚轮上/下</b> — 快退/快进 10 秒',
                '<b>双击预览区</b> — 全屏',
                '<b>Esc</b> — 退出全屏',
            ]),
        ]
        for title_text, items in sections:
            col = QVBoxLayout(); col.setSpacing(4)
            col.addWidget(QLabel(f'<b style="color:#0078D7;font-size:12px">{title_text}</b>'))
            for item in items:
                col.addWidget(QLabel(f'<span style="color:#AAA;font-size:11px">{item}</span>'))
            cols.addLayout(col)
        layout.addLayout(cols)

        btn_row = QHBoxLayout(); btn_row.addStretch()
        ok = QPushButton('关闭'); ok.clicked.connect(d.accept); btn_row.addWidget(ok)
        layout.addLayout(btn_row)
        d.exec()

    def _show_about_dialog(self):
        from PySide6.QtWidgets import QDialog, QVBoxLayout, QLabel, QPushButton, QHBoxLayout
        d = QDialog(self)
        d.setWindowTitle('关于 Xnnview')
        d.setFixedSize(380, 200)
        d.setStyleSheet(
            'QDialog{background:#1A1A1A;border:1px solid #333;}'
            'QLabel{color:#CCC;background:transparent;}'
            'QPushButton{background:#333;color:#CCC;border:none;padding:6px 24px;border-radius:3px;font-size:12px;}'
            'QPushButton:hover{background:#444;}')
        layout = QVBoxLayout(d); layout.setContentsMargins(24, 20, 24, 16); layout.setSpacing(8)
        layout.addWidget(QLabel('<b style="font-size:18px;color:#FFF">Xnnview</b>'))
        layout.addWidget(QLabel('<span style="color:#AAA;font-size:11px">通用文件资源管理器<br>支持图片/视频/音频预览 — Live Photo 动态照片识别</span>'))
        layout.addWidget(QLabel('<span style="color:#666;font-size:10px">版本 1.0 — Python + PySide6</span>'))
        layout.addStretch()
        btn = QHBoxLayout(); btn.addStretch()
        ok = QPushButton('关闭'); ok.clicked.connect(d.accept); btn.addWidget(ok)
        layout.addLayout(btn)
        d.exec()

    def _on_size(self, value: int):
        self._size_label.setText(f'{value} px')
        self.file_grid.set_card_size(value)
        cur = self._addr_bar.text()
        if cur and os.path.isdir(cur): self.file_grid.load_directory(cur)
