"""虚拟滚动文件网格"""
import os
from pathlib import Path

from PySide6.QtWidgets import QScrollArea, QWidget, QApplication, QMessageBox
from PySide6.QtCore import Qt, QTimer, Signal, QUrl
from PySide6.QtGui import QPixmap

from app.constants import C_CONTENT
from app.thumbnail import IMAGE_EXTS, VIDEO_EXTS as _TVIDEO_EXTS
from app.file_grid.entry import FileEntry, _fast_scandir, mime_type, read_exif_date, _img_dim
from app.file_grid.thumb_loader import ThumbLoader
from app.file_grid.card import FileCard
from app.file_grid.sort_header import SORT_NAME


class FileGrid(QScrollArea):
    file_count_changed = Signal()
    selection_changed = Signal(str)
    thumb_zoom = Signal(int)
    delete_requested = Signal(str)
    new_folder_requested = Signal(str)
    SPACING, MARGIN, BUF = 6, 8, 4

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWidgetResizable(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setStyleSheet(f'QScrollArea {{ background: {C_CONTENT}; border: none; }}')
        self._canvas = QWidget()
        self._canvas.setStyleSheet(f'background: {C_CONTENT};')
        self.setWidget(self._canvas)
        self._entries: list[FileEntry] = []
        self._card_size = 160; self._cols = 4
        self._vis_start_row = self._vis_end_row = -1
        self._pool: list[FileCard] = []
        self._active: dict[int, FileCard] = {}
        self._selected: set[int] = set()
        self._last_clicked: int = -1
        self._sort_col = SORT_NAME; self._sort_asc = True
        self._marked: set[str] = set()
        self._filter_marked = False
        self._all_entries: list[FileEntry] = []
        self._thumb_cache: dict[str, QPixmap] = {}
        self._thumb = ThumbLoader()
        self._thumb.loaded.connect(self._on_thumb)
        self.verticalScrollBar().valueChanged.connect(self._on_scroll)
        self.verticalScrollBar().setSingleStep(60)
        self.viewport().installEventFilter(self)
        self._resize_timer = QTimer(self)
        self._resize_timer.setSingleShot(True)
        self._resize_timer.setInterval(150)
        self._resize_timer.timeout.connect(self._do_resize_layout)
        self._thumb_timer = QTimer(self)
        self._thumb_timer.setSingleShot(True)
        self._thumb_timer.setInterval(50)
        self._thumb_timer.timeout.connect(self._do_load_thumbs)
        self._detect_timer = QTimer(self)
        self._detect_timer.setInterval(200)
        self._detect_timer.timeout.connect(self._detect_one)

    def sort(self, col: int, asc: bool):
        self._sort_col = col; self._sort_asc = asc
        exif_cache: dict[str, float] = {}
        def _key(entry: FileEntry):
            if entry.is_dir: return (0,)
            if col == 0: return (1, entry.name.lower())  # SORT_NAME
            elif col == 1: return (1, entry.size)
            elif col == 2: return (1, mime_type(entry.ext))
            elif col == 3: return (1, entry.ext)
            elif col == 4: return (1, entry.ctime)
            elif col == 5: return (1, entry.mtime)
            elif col == 6:
                if entry.path not in exif_cache:
                    exif_cache[entry.path] = read_exif_date(entry.path)
                return (1, exif_cache.get(entry.path, 0))
            elif col == -2: return (1, entry.path.lower())  # PATH
            elif col == -3: return (1, 1 if entry.path in self._marked else 0)  # MARKED
            elif col == -4: return (1, _img_dim(entry.path)[0])  # WIDTH
            elif col == -5: return (1, _img_dim(entry.path)[1])  # HEIGHT
            return (1, entry.name.lower())
        self._entries.sort(key=_key, reverse=not asc)
        self._clear_active(); self._update_layout()
        self.verticalScrollBar().setValue(0); self._on_scroll()

    def load_directory(self, dir_path: str):
        if getattr(self, '_loading', False): return
        self._loading = True
        try:
            self._all_entries = _fast_scandir(dir_path)
            self._marked.clear(); self._selected.clear(); self._last_clicked = -1
            self._apply_filter()
            self._canvas.setUpdatesEnabled(False)
            self._on_scroll(); self.file_count_changed.emit()
            self._canvas.setUpdatesEnabled(True)
        finally:
            self._loading = False

    def _apply_filter(self):
        if self._filter_marked:
            self._entries = [e for e in self._all_entries if e.path in self._marked]
        else:
            self._entries = list(self._all_entries)
        self.sort(self._sort_col, self._sort_asc)

    def toggle_filter(self):
        self._filter_marked = not self._filter_marked
        self._apply_filter()
        self._on_scroll(); self.file_count_changed.emit()

    def _update_layout(self):
        if not self._entries: self._canvas.setFixedSize(0, 0); return
        cw = self._card_size + self.SPACING
        vw = max(self.viewport().width(), 200)
        self._cols = max(1, (vw - self.MARGIN * 2) // cw)
        ch = self._card_size + 22 + self.SPACING
        rows = (len(self._entries) + self._cols - 1) // self._cols
        self._canvas.setFixedSize(vw, max(self.MARGIN * 2 + rows * ch, self.height()))
        self.verticalScrollBar().setSingleStep(ch)

    @property
    def _rows(self): return (len(self._entries) + self._cols - 1) // self._cols
    def _row_y(self, row: int): return self.MARGIN + row * (self._card_size + 22 + self.SPACING)

    def _on_scroll(self):
        if not self._entries: return
        ch = self._card_size + 22 + self.SPACING
        sy = self.verticalScrollBar().value(); vh = self.viewport().height()
        fr = max(0, (sy - self.MARGIN) // ch - self.BUF)
        lr = min(self._rows, (sy + vh - self.MARGIN) // ch + self.BUF)
        if fr == self._vis_start_row and lr == self._vis_end_row: return
        self._canvas.setUpdatesEnabled(False)
        for idx in list(self._active):
            if idx // self._cols < fr or idx // self._cols >= lr:
                card = self._active.pop(idx); card.deactivate(); card.hide(); self._pool.append(card)
        cw = self._card_size + self.SPACING
        for row in range(fr, lr):
            y = self._row_y(row)
            for col in range(self._cols):
                idx = row * self._cols + col
                if idx >= len(self._entries): break
                if idx in self._active: continue
                x = self.MARGIN + col * cw
                card = self._pool.pop() if self._pool else self._create_card()
                card.setParent(self._canvas)
                card.setup(self._entries[idx], self._card_size)
                card.set_selected(idx in self._selected, len(self._selected) > 1)
                card.set_marked(card.file_path in self._marked)
                if card.file_path in self._thumb_cache:
                    card.set_thumbnail(self._thumb_cache[card.file_path])
                card.move(x, y); card.show()
                self._active[idx] = card
        self._canvas.setUpdatesEnabled(True)
        self._vis_start_row, self._vis_end_row = fr, lr
        # 50ms 短防抖：合并连续滚动事件，但仍然快速加载
        self._thumb_timer.start()

    def _do_load_thumbs(self):
        # 只清除还未开始的任务，不中断进行中的
        self._thumb.clear_queue()
        # 按优先级排列：先加载中间的（当前焦点区），再加载上下缓冲区的
        items = []
        mid = (self._vis_start_row + self._vis_end_row) // 2
        for idx in sorted(self._active.keys(), key=lambda i: -abs(i // self._cols - mid)):
            entry = self._entries[idx]
            if not entry.is_dir and entry.ext in (IMAGE_EXTS | _TVIDEO_EXTS) and \
               entry.path not in self._thumb_cache:
                self._thumb.enqueue(entry.path, self._card_size,
                                    is_video=(entry.ext in _TVIDEO_EXTS))

    def _create_card(self) -> FileCard:
        card = FileCard(self._canvas)
        card.clicked.connect(self._on_card_clicked)
        card.double_clicked.connect(self._on_card_double_clicked)
        return card

    def _on_card_clicked(self, card: FileCard):
        idx = None
        for i, c in self._active.items():
            if c is card: idx = i; break
        if idx is None: return
        mods = QApplication.keyboardModifiers()
        ctrl = bool(mods & Qt.KeyboardModifier.ControlModifier)
        shift = bool(mods & Qt.KeyboardModifier.ShiftModifier)
        if ctrl:
            if idx in self._selected: self._selected.discard(idx)
            else: self._selected.add(idx)
            self._last_clicked = idx
        elif shift and self._last_clicked >= 0:
            lo, hi = min(idx, self._last_clicked), max(idx, self._last_clicked)
            self._selected.update(range(lo, hi + 1))
        else:
            self._selected = {idx}; self._last_clicked = idx
            if card.is_live: card.play_video()
        self._refresh_selection()
        sel_path = self._entries[idx].path if self._selected else ''
        self.selection_changed.emit(sel_path)

    def _on_card_double_clicked(self, card: FileCard):
        path = card.file_path
        if os.path.isdir(path):
            pw = self.parent()
            while pw and not hasattr(pw, '_navigate_to'): pw = pw.parent()
            if pw and hasattr(pw, '_navigate_to'): pw._navigate_to(path)
            return
        if not card.is_live and Path(path).suffix.lower() in {'.heic','.heif','.jpg','.jpeg','.png'}:
            card._detect_live()
        if card.is_live and card.live_info:
            video = card.live_info.get('video_path', card.file_path)
            if video: os.startfile(video)
        else:
            os.startfile(card.file_path)

    def _refresh_selection(self):
        multi = len(self._selected) > 1
        for idx, card in self._active.items():
            card.set_selected(idx in self._selected, multi)

    def _on_thumb(self, file_path: str, pixmap: QPixmap):
        self._thumb_cache[file_path] = pixmap
        for card in self._active.values():
            if card.file_path == file_path: card.set_thumbnail(pixmap); break

    def _clear_active(self):
        for card in self._active.values(): card.deactivate(); card.hide(); self._pool.append(card)
        self._active.clear(); self._vis_start_row = self._vis_end_row = -1

    def _detect_one(self):
        for card in self._active.values():
            if getattr(card, '_needs_detect', False):
                card._needs_detect = False
                card._detect_live()
                return  # 每次只测一张
        self._detect_timer.stop()  # 全测完了

    def _do_resize_layout(self):
        if self._entries: self._clear_active(); self._update_layout(); self._on_scroll()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self._entries: self._resize_timer.start()

    def closeEvent(self, event): self._thumb.stop(); super().closeEvent(event)

    def navigate_selection(self, delta: int):
        if not self._entries: return
        cur = self._last_clicked if self._last_clicked >= 0 else 0
        new_idx = max(0, min(len(self._entries) - 1, cur + delta))
        if new_idx == cur: return
        self._selected = {new_idx}; self._last_clicked = new_idx
        self._refresh_selection()
        row_h = self._card_size + 22 + self.SPACING
        new_row = new_idx // self._cols
        vh = self.viewport().height()
        cur_sy = self.verticalScrollBar().value()
        first_vis = max(0, (cur_sy - self.MARGIN) // row_h)
        last_vis = (cur_sy + vh - self.MARGIN) // row_h
        if delta > 0 and new_row > last_vis:
            sy = self._row_y(new_row) - vh + row_h + self.MARGIN
        elif delta < 0 and new_row < first_vis:
            sy = self._row_y(new_row) - self.MARGIN
        else:
            self._on_scroll()
            self.selection_changed.emit(self._entries[new_idx].path)
            return
        max_sy = self.verticalScrollBar().maximum()
        self.verticalScrollBar().setValue(max(0, min(sy, max_sy)))
        self._on_scroll()
        self.selection_changed.emit(self._entries[new_idx].path)

    # ── 键盘 ──
    def keyPressEvent(self, event):
        if not self._entries: return super().keyPressEvent(event)
        cur = self._last_clicked if self._last_clicked >= 0 else 0
        k = event.key()
        if k in (Qt.Key.Key_C, Qt.Key.Key_Left, Qt.Key.Key_Up):
            self.navigate_selection(-1); return
        elif k in (Qt.Key.Key_V, Qt.Key.Key_Right, Qt.Key.Key_Down):
            self.navigate_selection(1); return
        elif k == Qt.Key.Key_F: self._toggle_mark(cur); return
        elif k == Qt.Key.Key_D: self._clear_all_marks(); return
        elif k == Qt.Key.Key_S: self._delete_file(cur); return
        elif k == Qt.Key.Key_X: self._new_folder(); return
        super().keyPressEvent(event)

    def _toggle_mark(self, idx: int):
        if idx < 0 or idx >= len(self._entries): return
        path = self._entries[idx].path
        if path in self._marked: self._marked.discard(path)
        else: self._marked.add(path)
        self._refresh_card_mark(idx)

    def _clear_all_marks(self):
        self._marked.clear()
        if self._filter_marked:
            self._apply_filter()
        # 强制更新所有可见卡片的标记状态
        for card in self._active.values():
            card.set_marked(False)
        self._refresh_selection()

    def _delete_file(self, idx: int):
        if idx < 0 or idx >= len(self._entries): return
        self.delete_requested.emit(self._entries[idx].path)

    def _new_folder(self): self.new_folder_requested.emit(self._get_current_dir())

    def _get_current_dir(self) -> str:
        pw = self.parent()
        while pw and not hasattr(pw, '_navigate_to'): pw = pw.parent()
        if pw and hasattr(pw, '_addr_bar'): return pw._addr_bar.text()
        return ''

    def _refresh_card_mark(self, idx: int):
        if idx in self._active:
            self._active[idx].set_marked(self._active[idx].file_path in self._marked)

    # ── Ctrl+滚轮缩放 ──
    def eventFilter(self, obj, event):
        from PySide6.QtCore import QEvent
        if obj is self.viewport() and event.type() == QEvent.Type.Wheel:
            if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
                delta = event.angleDelta().y()
                self.thumb_zoom.emit(1 if delta > 0 else -1)
                return True
        return super().eventFilter(obj, event)

    def set_card_size(self, size: int): self._card_size = size

    @property
    def file_count(self): return len(self._entries)
    @property
    def selected_count(self): return len(self._selected)
    @property
    def selected_size(self): return sum(self._entries[i].size for i in self._selected if i < len(self._entries))
    @property
    def selected_paths(self): return [self._entries[i].path for i in self._selected if i < len(self._entries)]
