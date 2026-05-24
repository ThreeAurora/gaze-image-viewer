"""文件卡片 Widget"""
import os
from pathlib import Path

from PySide6.QtWidgets import QLabel, QFrame, QApplication
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import QPixmap, QColor, QPainter, QPen

from app.constants import C_CARD_BG, C_CARD_BORDER, C_SELECT_BLUE, C_SELECT_YELLOW, C_VIDEO_BG, C_GIF_BG, C_OTHER_BG, VIDEO_EXTS, GIF_EXT
from app.thumbnail import IMAGE_EXTS, VIDEO_EXTS as _TVIDEO_EXTS
from app.live_photo import detect
from app.file_grid.entry import _type_icon, _folder_icon, _file_system_icon


class FileCard(QFrame):
    clicked = Signal(object)
    double_clicked = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._card_size = 160
        self.file_path = ''
        self.is_live = False
        self.live_info: dict | None = None
        self._active = False
        self._selected = False
        self._sel_color = C_SELECT_BLUE
        self._name_bg = C_OTHER_BG

        self.thumb_label = QLabel(self)
        self.thumb_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.name_label = QLabel(self)
        self.name_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.live_badge = QLabel('LIVE', self); self.live_badge.hide()
        self.star_label = QLabel('★', self); self.star_label.hide()
        self.star_label.setStyleSheet('color: #FF8800; font-size: 14px; background: transparent;')
        self.video_widget = None
        self._marked = False

    def setup(self, entry, card_size: int):
        self._card_size = card_size; self.file_path = entry.path
        self._active = True; self._selected = False
        self._sel_color = C_SELECT_BLUE
        self.is_live = False; self.live_info = None; self.live_badge.hide()

        if entry.ext == GIF_EXT:
            self._name_bg = C_GIF_BG
        elif entry.ext in VIDEO_EXTS:
            self._name_bg = C_VIDEO_BG
        else:
            self._name_bg = C_OTHER_BG

        ts = card_size - 8
        self.setFixedSize(card_size, card_size + 22)
        self.thumb_label.setGeometry(4, 4, ts, ts)
        self.name_label.setGeometry(2, card_size + 2, card_size - 4, 18)
        metrics = self.name_label.fontMetrics()
        elided = metrics.elidedText(entry.name, Qt.TextElideMode.ElideMiddle, card_size - 8)
        self.name_label.setText(elided)

        if entry.is_dir:
            icon = _folder_icon(ts)
        elif entry.ext in (IMAGE_EXTS | _TVIDEO_EXTS):
            icon = _type_icon(entry.ext)
        else:
            icon = _file_system_icon(entry.path)
        self.thumb_label.setPixmap(icon.pixmap(int(ts * 0.35), int(ts * 0.35)))
        self.live_badge.setGeometry(6, 6, 36, 16)
        self.star_label.setGeometry(card_size - 24, 4, 20, 20)
        self._marked = False; self.star_label.hide()
        self.setToolTip(entry.name)
        self._last_label_bg = None  # 强制刷新标签底色
        self._apply_base_style()
        # Live Photo 检测仅通过预览面板点击触发，不在浏览时执行

    _LABEL_SS_CACHE: dict[str, str] = {}

    _BASE_SS = f'FileCard{{background:{C_CARD_BG};border:none;border-radius:4px;}}'

    def _apply_base_style(self):
        if not getattr(self, '_styled', False):
            self._styled = True
            self.setStyleSheet(self._BASE_SS)
            self.live_badge.setStyleSheet(
                'background:#0078D4;color:#FFF;font-size:9px;font-weight:bold;'
                'padding:1px 4px;border-radius:2px;')
        self._last_label_bg = None
        self._apply_label_bg()

    def _apply_label_bg(self):
        bg = self._sel_color if self._selected else self._name_bg
        if bg != getattr(self, '_last_label_bg', None):
            self._last_label_bg = bg
            pal = self.name_label.palette()
            pal.setColor(self.name_label.backgroundRole(), QColor(bg))
            self.name_label.setPalette(pal)
            self.name_label.setAutoFillBackground(True)

    def set_selected(self, sel: bool, multi: bool = False):
        if self._selected == sel and self._sel_color == (C_SELECT_YELLOW if multi else C_SELECT_BLUE):
            return
        self._selected = sel; self._sel_color = C_SELECT_YELLOW if multi else C_SELECT_BLUE
        pal = self.name_label.palette()
        pal.setColor(self.name_label.backgroundRole(), QColor(self._sel_color if sel else self._name_bg))
        self.name_label.setPalette(pal)
        self.update()

    def paintEvent(self, event):
        super().paintEvent(event)
        if not getattr(self, '_active', False): return
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(QColor(self._sel_color if self._selected else C_CARD_BORDER), 1))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRoundedRect(0, 0, self.width() - 1, self.height() - 1, 4, 4)
        p.end()

    def set_marked(self, marked: bool):
        self._marked = marked
        self.star_label.setVisible(marked)

    def set_thumbnail(self, pixmap: QPixmap):
        if not self._active: return
        ts = self._card_size - 14
        scaled = pixmap.scaled(ts, ts, Qt.AspectRatioMode.KeepAspectRatio,
                               Qt.TransformationMode.SmoothTransformation)
        self.thumb_label.setPixmap(scaled)
        self.thumb_label.setScaledContents(False)
        # 让 label 适应 pixmap 的实际尺寸，居中显示
        self.thumb_label.setGeometry(
            (self._card_size - scaled.width()) // 2,
            (self._card_size - scaled.height()) // 2,
            scaled.width(), scaled.height())

    def deactivate(self):
        self._active = False; self._stop_video()

    def _detect_live(self):
        if not self._active: return
        # 只用轻量检测（检查同名 .mov/.mp4），不扫描文件内部（极快）
        from app.live_photo import detect_companion
        info = detect_companion(self.file_path)
        if info and self._active:
            self.is_live = True; self.live_info = info; self.live_badge.show()

    # ── 鼠标事件 ──
    def mouseReleaseEvent(self, event):
        self._stop_video()
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit(self)
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.double_clicked.emit(self)
        super().mouseDoubleClickEvent(event)

    def enterEvent(self, event):
        self.thumb_label.setStyleSheet('background:#1A3050;border-radius:2px;')
        pw = self.window()
        if hasattr(pw, '_show_tooltip'): pw._show_tooltip(self)
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.thumb_label.setStyleSheet('background:transparent;border-radius:2px;')
        pw = self.window()
        if hasattr(pw, '_hide_tooltip'): pw._hide_tooltip()
        super().leaveEvent(event)

    def contextMenuEvent(self, event):
        from app.context_menu import FileContextMenu
        FileContextMenu(self).exec(event.globalPos())

    # ── Live Photo 播放 ──
    def play_video(self):
        if not self.is_live or not self.live_info: return
        if self.live_info.get('embedded'):
            from app.live_photo import extract_embedded_video
            video = extract_embedded_video(self.file_path, self.live_info)
            if not video: return
        else:
            video = self.live_info.get('video_path', self.file_path)
        if not video or not Path(video).exists(): return
        os.startfile(video)

    def _stop_video(self):
        if self.video_widget and self.video_widget.isVisible():
            self.video_widget.hide()
            try: self._player.stop()
            except: pass
            if self.is_live: self.live_badge.show()
