"""预览面板 —— 图片异步加载 / 视频(QMediaPlayer) / 音频 三合一"""
import os
from pathlib import Path

from PySide6.QtWidgets import (
    QWidget, QHBoxLayout, QLabel, QPushButton, QSlider, QApplication,
)
from PySide6.QtCore import Qt, QTimer, Signal, QUrl, QEvent, QSize, QThread
from PySide6.QtGui import QPixmap, QMovie, QWheelEvent

from app.constants import C_PREVIEW_BG
from app.thumbnail import IMAGE_EXTS

VIDEO_EXTS = {'.mp4', '.mov', '.avi', '.mkv', '.webm', '.wmv', '.flv', '.m4v', '.mpg', '.mpeg', '.3gp'}
AUDIO_EXTS = {'.mp3', '.wav', '.flac', '.aac', '.ogg', '.wma', '.m4a', '.opus'}


class _ImgLoader(QThread):
    loaded = Signal(str, QPixmap)

    def __init__(self):
        super().__init__()
        self._path = ''; self._alive = True

    def load(self, path: str):
        self._path = path
        if not self.isRunning(): self.start()

    def run(self):
        while self._alive:
            p = self._path
            if p:
                self._path = ''
                pix = QPixmap(p)
                if not pix.isNull():
                    self.loaded.emit(p, pix)
            else:
                self.msleep(20)

    def stop(self):
        self._alive = False; self._path = ''
        if self.isRunning():
            self.quit()
            self.wait(2000)


class _SeekSlider(QSlider):
    clicked = Signal(object); hover_move = Signal(object); hover_leave = Signal()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton: self.clicked.emit(event)
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event): self.hover_move.emit(event); super().mouseMoveEvent(event)
    def leaveEvent(self, event): self.hover_leave.emit(); super().leaveEvent(event)


class PreviewPanel(QWidget):
    nav_file = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setStyleSheet(f'background: {C_PREVIEW_BG};')
        self._mode = 'none'; self._orig_pix = None; self._scale = 1.0
        self._file_path = ''; self._show_remaining = False

        self._img_label = QLabel(self); self._img_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._img_label.setStyleSheet('background: transparent;'); self._img_label.hide()
        self._audio_label = QLabel(self); self._audio_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._audio_label.setStyleSheet('color: #CCC; font-size: 16px; background: transparent;')
        self._audio_label.hide()

        self._img_loader = _ImgLoader()
        self._img_loader.loaded.connect(self._on_img_loaded)

        self._setup_player()
        self._build_control_bar()
        self._pos_timer = QTimer(self); self._pos_timer.setInterval(200)
        self._pos_timer.timeout.connect(self._update_position)
        self._seeking = False; self._dragging = False
        self._drag_start = self._drag_label_pos = None; self._splitter_parent = None
        self._live_info = None; self._orig_pix_saved = None

    def _setup_player(self):
        try:
            from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput
            from PySide6.QtMultimediaWidgets import QVideoWidget
            self._video_widget = QVideoWidget(self)
            self._video_widget.setStyleSheet('background: black;'); self._video_widget.hide()
            self._video_overlay = QWidget(self)
            self._video_overlay.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, False)
            self._video_overlay.setStyleSheet('background: transparent;'); self._video_overlay.hide()
            self._video_overlay.installEventFilter(self)
            self._player = QMediaPlayer(self); self._audio_output = QAudioOutput(self._player)
            self._player.setAudioOutput(self._audio_output)
            self._player.setVideoOutput(self._video_widget)
        except ImportError:
            self._player = None

    def _build_control_bar(self):
        self._control_bar = QWidget(self); self._control_bar.setFixedHeight(48)
        self._control_bar.setStyleSheet('background: rgba(0,0,0,200);'); self._control_bar.hide()
        cb = QHBoxLayout(self._control_bar); cb.setContentsMargins(10, 0, 10, 0); cb.setSpacing(8)

        play_s = ('QPushButton{background:#333;color:#CCC;border:none;'
                  'font-size:16px;border-radius:4px;padding:0;}'
                  'QPushButton:hover{background:#0078D7;color:#FFF;}')
        self._btn_play = QPushButton('▶'); self._btn_play.setFixedSize(38, 28)
        self._btn_play.setStyleSheet(play_s); self._btn_play.clicked.connect(self._toggle_play)
        cb.addWidget(self._btn_play)

        vol_s = ('QPushButton{background:transparent;color:#AAA;border:none;font-size:13px;border-radius:3px;}'
                 'QPushButton:hover{background:rgba(255,255,255,0.1);color:#FFF;}')
        self._btn_volume = QPushButton('♪'); self._btn_volume.setFixedSize(28, 28)
        self._btn_volume.setStyleSheet(vol_s)
        self._btn_volume.clicked.connect(self._toggle_volume_popup)
        cb.addWidget(self._btn_volume)
        self._vol_popup = QWidget(None, Qt.WindowType.Popup)
        self._vol_popup.setFixedSize(28, 100)
        self._vol_popup.setStyleSheet('background:#252525;border:1px solid #444;border-radius:4px;')
        vol_layout = QHBoxLayout(self._vol_popup); vol_layout.setContentsMargins(4, 4, 4, 4)
        self._vol_slider = QSlider(Qt.Orientation.Vertical)
        self._vol_slider.setRange(0, 100); self._vol_slider.setValue(80)
        self._vol_slider.setFixedSize(28, 92)
        self._vol_slider.setStyleSheet(
            'QSlider::groove:vertical{width:4px;background:#444;border-radius:2px;}'
            'QSlider::handle:vertical{height:12px;width:14px;margin:0 -5px;background:#FFF;border-radius:7px;}')
        vol_layout.addWidget(self._vol_slider)
        self._vol_popup.hide()
        self._vol_slider.valueChanged.connect(self._on_volume)

        self._progress = _SeekSlider(Qt.Orientation.Horizontal); self._progress.setRange(0, 1000)
        self._progress.setStyleSheet(
            'QSlider::groove:horizontal{height:6px;background:#333;border-radius:3px;}'
            'QSlider::sub-page:horizontal{background:#CCC;border-radius:3px;}'
            'QSlider::handle:horizontal{width:14px;height:14px;margin:-4px 0;background:#FFF;border-radius:7px;}')
        self._progress.sliderMoved.connect(self._on_seek)
        self._progress.sliderPressed.connect(self._on_seek_press)
        self._progress.sliderReleased.connect(self._on_seek_release)
        self._progress.clicked.connect(self._on_progress_click)
        self._progress.hover_move.connect(self._on_progress_hover)
        self._progress.hover_leave.connect(self._on_progress_leave)
        cb.addWidget(self._progress, 1)

        self._time_label = QPushButton('00:00 / 00:00'); self._time_label.setFixedWidth(120)
        self._time_label.setStyleSheet(
            'QPushButton{background:transparent;color:#AAA;border:none;font-size:11px;padding:2px 4px;}'
            'QPushButton:hover{color:#FFF;}')
        self._time_label.clicked.connect(self._toggle_time_display); cb.addWidget(self._time_label)

        self._hover_tip = QLabel(self)
        self._hover_tip.setStyleSheet('background:rgba(0,0,0,220);color:#FFF;font-size:12px;padding:4px 8px;border-radius:4px;')
        self._hover_tip.hide()
        self._frame_thumb = QLabel(self); self._frame_thumb.setFixedSize(160, 90)
        self._frame_thumb.setStyleSheet('background:#111;border:1px solid #444;border-radius:4px;')
        self._frame_thumb.hide()
        self._frame_debounce = QTimer(self); self._frame_debounce.setSingleShot(True)
        self._frame_debounce.setInterval(200)
        self._frame_debounce.timeout.connect(self._extract_frame_thumb)

    # ── 加载 ──
    def load_file(self, file_path: str):
        if not file_path or not Path(file_path).is_file(): self.clear(); return
        self._vol_popup.hide()
        self._file_path = file_path; ext = Path(file_path).suffix.lower()
        if ext in IMAGE_EXTS: self._show_image(file_path)
        elif ext in VIDEO_EXTS: self._show_video(file_path)
        elif ext in AUDIO_EXTS: self._show_audio(file_path)
        else: self.clear()

    def _show_image(self, path: str):
        self._stop_player(); self._control_bar.hide()
        self._audio_label.hide()
        if self._video_widget: self._video_widget.hide()
        if Path(path).suffix.lower() == '.gif':
            self._orig_pix = None; self._mode = 'image'
            self._img_label.setGeometry(10, 10, self.width() - 20, self.height() - 20)
            # 先停止旧 movie 防止内存泄漏
            old = self._img_label.movie()
            if old: old.stop(); old.deleteLater()
            self._img_label.setMovie(None)
            self._img_label.clear()
            movie = QMovie(path)
            movie.setScaledSize(self._img_label.size())
            self._img_label.setMovie(movie)
            self._img_label.setScaledContents(False)
            movie.start()
        else:
            self._live_info = None; self._orig_pix = None; self._mode = 'image'
            # 轻量检测：仅检查配对 .mov 文件（不打开图片文件，无 I/O 风险）
            try:
                from app.live_photo import detect_companion
                self._live_info = detect_companion(path)
            except: pass
            self._img_label.show()
            self._img_loader.load(path)

    def _on_img_loaded(self, path: str, pix: QPixmap):
        if self._mode != 'image' or self._file_path != path: return
        self._orig_pix = pix; self._fit_auto()

    def _show_video(self, path: str):
        self._img_label.clear(); self._img_label.hide(); self._audio_label.hide()
        self._orig_pix = None; self._mode = 'video'
        if not self._player: return
        bar_h = self._control_bar.height()
        self._video_widget.setGeometry(0, 0, self.width(), self.height() - bar_h)
        self._video_widget.show()
        if self._video_overlay:
            self._video_overlay.setGeometry(0, 0, self.width(), self.height() - bar_h)
            self._video_overlay.show()
            self._video_overlay.raise_()
        self._control_bar.setGeometry(0, self.height() - bar_h, self.width(), bar_h)
        self._control_bar.show()
        self._btn_play.setText('⏸'); self._pos_timer.start()
        # 彻底停止并重建播放器，消除音频泄漏
        old = self._player
        old.stop()
        old.setVideoOutput(None)
        old.setSource(QUrl())
        from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput
        self._player = QMediaPlayer(self)
        self._audio_output = QAudioOutput(self._player)
        self._player.setAudioOutput(self._audio_output)
        self._player.setVideoOutput(self._video_widget)
        self._player.setSource(QUrl.fromLocalFile(path))
        self._player.play()
        old.deleteLater()

    def _show_audio(self, path: str):
        self._img_label.clear(); self._img_label.hide()
        if self._video_widget: self._video_widget.hide()
        if self._video_overlay: self._video_overlay.hide()
        self._orig_pix = None; self._mode = 'audio'
        bar_h = self._control_bar.height()
        self._control_bar.setGeometry(0, self.height() - bar_h, self.width(), bar_h)
        self._control_bar.show()
        self._btn_play.setText('⏸'); self._pos_timer.start()
        name = Path(path).name
        self._audio_label.setText(f'♪ {name}')
        self._audio_label.setGeometry(0, 0, self.width(), self.height() - bar_h)
        self._audio_label.show()
        self._player.stop()
        self._player.setVideoOutput(None)
        self._player.setSource(QUrl.fromLocalFile(path))
        self._player.play()

    # ── 控制 ──
    def _toggle_play(self):
        if not self._player: return
        from PySide6.QtMultimedia import QMediaPlayer
        if self._player.playbackState() == QMediaPlayer.PlayingState:
            self._player.pause(); self._btn_play.setText('▶'); self._pos_timer.stop()
        else:
            self._player.play(); self._btn_play.setText('⏸'); self._pos_timer.start()

    def toggle_play_pause(self):
        if self._mode in ('video', 'audio'): self._toggle_play()

    def seek_delta(self, seconds: int):
        if self._mode in ('video', 'audio') and self._player:
            dur = self._player.duration(); cur = self._player.position()
            self._player.setPosition(max(0, min(dur, cur + seconds * 1000)))

    def seek_all_modes(self, wheel_delta: int):
        if self._mode in ('video', 'audio') and self._player:
            self._seek_video_by_delta(wheel_delta, 3)

    def _seek_video_by_delta(self, delta: int, seconds: int):
        if not self._player: return
        cur = self._player.position(); dur = self._player.duration()
        ms = seconds * 1000
        self._player.setPosition(int(max(0, min(dur, cur + (ms if delta > 0 else -ms)))))

    def _on_volume(self, val: int):
        if self._audio_output:
            self._audio_output.setVolume(val / 100.0)
            v = val / 100.0
            self._btn_volume.setText('M' if v < 0.01 else '♪' if v < 0.5 else '♪♪')

    def _toggle_volume_popup(self):
        if self._vol_popup.isVisible():
            self._vol_popup.hide()
        else:
            gp = self._btn_volume.mapToGlobal(self._btn_volume.rect().bottomLeft())
            self._vol_popup.move(gp.x(), gp.y() - self._vol_popup.height() - 4)
            self._vol_popup.show()

    def _on_volume_toggle_mute(self):
        if self._audio_output and self._audio_output.volume() > 0.01:
            self._mute_vol = self._audio_output.volume(); self._audio_output.setVolume(0)
            self._vol_slider.setValue(0); self._btn_volume.setText('M')
        elif self._audio_output:
            v = getattr(self, '_mute_vol', 0.8); self._audio_output.setVolume(v)
            self._vol_slider.setValue(int(v * 100))
            self._btn_volume.setText('♪♪' if v > 0.5 else '♪')

    def _on_seek_press(self): self._seeking = True
    def _on_seek_release(self):
        self._seeking = False
        if self._player and self._player.duration() > 0:
            pos = self._progress.value() * self._player.duration() // 1000
            self._player.setPosition(pos)

    def _on_seek(self, val: int):
        if self._player:
            dur = self._player.duration()
            pos = int(val * dur / 1000) if dur > 0 else 0
            self._update_time_label(pos, dur)
            self._player.setPosition(pos)

    def _on_progress_click(self, event):
        if not self._player or self._player.duration() <= 0: return
        w = self._progress.width(); x = event.position().x()
        ratio = max(0, min(1.0, x / w)); val = int(ratio * 1000)
        self._progress.setValue(val)
        self._player.setPosition(int(val * self._player.duration() / 1000))

    def _on_progress_hover(self, event):
        if not self._player or self._player.duration() <= 0: return
        w = self._progress.width(); x = event.position().x()
        ratio = max(0, min(1.0, x / w))
        pos_ms = int(ratio * self._player.duration()); s = pos_ms // 1000
        tip_text = f'{s // 60}:{s % 60:02d}'
        gx = self._progress.mapTo(self, event.position().toPoint()).x(); gy = self._control_bar.y() - 28
        self._hover_tip.setText(tip_text); self._hover_tip.adjustSize()
        tx = max(0, min(self.width() - self._hover_tip.width(), gx - self._hover_tip.width() // 2))
        self._hover_tip.move(tx, gy); self._hover_tip.show(); self._hover_tip.raise_()
        fty = gy - self._frame_thumb.height() - 6
        ftx = max(0, min(self.width() - self._frame_thumb.width(), gx - self._frame_thumb.width() // 2))
        self._frame_thumb.move(ftx, fty); self._frame_debounce.start()

    def _on_progress_leave(self):
        self._hover_tip.hide(); self._frame_debounce.stop(); self._frame_thumb.hide()

    def _extract_frame_thumb(self):
        if not self._player or not self._file_path: return
        dur = self._player.duration(); pos = self._player.position()
        if dur <= 0: return
        try:
            import subprocess, tempfile
            tmp = tempfile.NamedTemporaryFile(suffix='.png', delete=False); tmp.close()
            subprocess.run(['ffmpeg', '-ss', str(pos), '-i', self._file_path, '-vframes', '1',
                            '-vf','scale=160:90:force_original_aspect_ratio=decrease,pad=160:90:(ow-iw)/2:(oh-ih)/2:color=#111',
                            '-q:v','5','-y',tmp.name],
                           capture_output=True, timeout=4,
                           creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            pix = QPixmap(tmp.name); os.unlink(tmp.name)
            if not pix.isNull(): self._frame_thumb.setPixmap(pix); self._frame_thumb.show(); self._frame_thumb.raise_()
        except Exception: pass

    def _update_position(self):
        if not self._player or self._seeking: return
        dur = self._player.duration(); pos = self._player.position()
        if dur > 0:
            self._progress.setValue(int(pos * 1000 / dur))
            if getattr(self, '_live_info', None) and pos >= dur - 500:
                self._restore_live_cover(); return
        self._update_time_label(pos, dur)

    def _restore_live_cover(self):
        self._stop_player(); self._control_bar.hide()
        if self._video_widget: self._video_widget.hide()
        if self._video_overlay: self._video_overlay.hide()
        self._orig_pix = getattr(self, '_orig_pix_saved', None)
        if self._orig_pix:
            self._mode = 'image'; self._img_label.show(); self._fit_auto()

    def _update_time_label(self, pos_ms: int, dur_ms: int):
        def _fmt(ms): s = max(0, ms // 1000); return f'{s // 60}:{s % 60:02d}'
        if self._show_remaining:
            remain = dur_ms - pos_ms
            self._time_label.setText(f'-{_fmt(remain)} / {_fmt(dur_ms)}' if dur_ms > 0 else '--:-- / --:--')
        else:
            self._time_label.setText(f'{_fmt(pos_ms)} / {_fmt(dur_ms)}' if dur_ms > 0 else '--:-- / --:--')

    def _toggle_time_display(self):
        self._show_remaining = not self._show_remaining
        if self._player: self._update_time_label(self._player.position(), self._player.duration())

    def _stop_player(self):
        if self._player:
            self._player.stop()
            self._player.setSource(QUrl())
        self._pos_timer.stop(); self._btn_play.setText('▶')

    def clear(self):
        self._stop_player(); self._control_bar.hide()
        # 停止 GIF movie
        old = self._img_label.movie()
        if old: old.stop(); old.deleteLater()
        self._img_label.setMovie(None)
        self._img_label.clear(); self._img_label.hide()
        if self._video_widget: self._video_widget.hide(); self._audio_label.hide()
        self._orig_pix = None; self._mode = 'none'

    # ── 图片缩放/拖动 ──
    def _fit_auto(self):
        if not self._orig_pix: return
        pw, ph = self._orig_pix.width(), self._orig_pix.height()
        vw, vh = self.width() - 20, self.height() - 20
        if pw <= 0 or ph <= 0 or vw <= 0 or vh <= 0: return
        self._scale = min(vw / pw, vh / ph, 1.0); self._render()

    def _render(self):
        if not self._orig_pix: return
        pw, ph = self._orig_pix.width(), self._orig_pix.height()
        self._img_label.resize(int(pw * self._scale), int(ph * self._scale))
        self._img_label.setPixmap(self._orig_pix.scaled(
            self._img_label.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
        self._img_label.move((self.width() - self._img_label.width()) // 2,
                             (self.height() - self._img_label.height()) // 2)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        bar_h = self._control_bar.height() if self._mode in ('video','audio') and self._control_bar.isVisible() else 0
        if self._mode == 'image':
            if self._orig_pix: self._fit_auto()
            else: self._img_label.setGeometry(10, 10, self.width() - 20, self.height() - 20)
        elif self._mode == 'video' and self._video_widget:
            self._video_widget.setGeometry(0, 0, self.width(), self.height() - bar_h)
            if self._video_overlay: self._video_overlay.setGeometry(0, 0, self.width(), self.height() - bar_h)
            self._control_bar.setGeometry(0, self.height() - bar_h, self.width(), bar_h)
        elif self._mode == 'audio':
            self._audio_label.setGeometry(0, 0, self.width(), self.height() - bar_h)
            self._control_bar.setGeometry(0, self.height() - bar_h, self.width(), bar_h)

    def mousePressEvent(self, event):
        if self._mode == 'image' and event.button() == Qt.MouseButton.LeftButton:
            # 轻量检测没找到 → 点击时做一次完整检测（用户主动触发，可接受）
            if not self._live_info:
                from app.live_photo import detect as detect_live
                self._live_info = detect_live(self._file_path)
            if self._live_info:
                self._play_live_photo()
                if self._mode == 'video': return
            if self._orig_pix and self._scale > 0:
                lw = self._img_label.width(); lh = self._img_label.height()
                if lw > self.width() or lh > self.height():
                    self._dragging = True; self._drag_start = event.position()
                    self._drag_label_pos = self._img_label.pos()
                    self.setCursor(Qt.CursorShape.ClosedHandCursor); return
        super().mousePressEvent(event)

    def _play_live_photo(self):
        # 使用已缓存的检测结果（_show_image 时已做轻量检测）
        info = self._live_info
        if not info: return
        if info.get('embedded'):
            from app.live_photo import extract_embedded_video
            video = extract_embedded_video(self._file_path, info)
        else:
            video = info.get('video_path', self._file_path)
        if not video or not Path(video).exists(): return
        if not self._player: return
        self._orig_pix_saved = self._orig_pix
        self._show_video(video)

    def mouseMoveEvent(self, event):
        if self._dragging:
            delta = event.position() - self._drag_start
            nx = self._drag_label_pos.x() + int(delta.x())
            ny = self._drag_label_pos.y() + int(delta.y())
            lw, lh = self._img_label.width(), self._img_label.height()
            nx = max(self.width() - lw, min(0, nx)); ny = max(self.height() - lh, min(0, ny))
            self._img_label.move(nx, ny); return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self._dragging: self._dragging = False; self.setCursor(Qt.CursorShape.ArrowCursor); return
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton: self._toggle_fullscreen()

    def _find_main_window(self):
        pw = self._splitter_parent or self.parent()
        while pw and not hasattr(pw, 'hide_tooltip_now'): pw = pw.parent()
        return pw

    def _toggle_fullscreen(self):
        mw = self._find_main_window()
        if mw and hasattr(mw, 'hide_tooltip_now'):
            mw.hide_tooltip_now()
            mw._fullscreen_mode = not self.isFullScreen()
        if self.isFullScreen():
            self.setWindowState(Qt.WindowState.WindowNoState)
            if self._splitter_parent and self._splitter_parent.layout():
                self.setParent(self._splitter_parent)
                self._splitter_parent.layout().addWidget(self)
                self.show()
                QTimer.singleShot(50, lambda: mw._splitter.setSizes(self._splitter_sizes) if mw and hasattr(mw, '_splitter') and self._splitter_sizes else None)
                if mw and hasattr(mw, 'file_grid'):
                    QTimer.singleShot(100, lambda: mw.file_grid.resizeEvent(None))
        else:
            mw = self._find_main_window()
            self._splitter_sizes = list(mw._splitter.sizes()) if mw and hasattr(mw, '_splitter') else [270, 570, 660]
            self._splitter_parent = self.parent()
            self.setParent(None, Qt.WindowType.Window)
            self.setWindowState(Qt.WindowState.WindowFullScreen)
            self.show()
        self.setFocus()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape and self.isFullScreen():
            self._toggle_fullscreen(); return
        super().keyPressEvent(event)

    def contextMenuEvent(self, event): pass

    def wheelEvent(self, event: QWheelEvent):
        ctrl = bool(event.modifiers() & Qt.KeyboardModifier.ControlModifier)
        right = bool(QApplication.mouseButtons() & Qt.MouseButton.RightButton)
        if self._mode == 'image':
            if ctrl: self._zoom_at_cursor(event)
            else: self._nav(event)
        elif self._mode == 'video':
            if right: self._seek_video(event, 3)
            elif ctrl: self._seek_video(event, 10)
            else: self._nav(event)
        elif self._mode == 'audio':
            if right or ctrl:
                delta = event.angleDelta().y()
                if self._player:
                    vol = max(0, min(1.0, self._player.volume + (0.1 if delta > 0 else -0.1)))
                    self._player.volume = vol
            else: self._nav(event)
        else: self._nav(event)

    def _nav(self, event: QWheelEvent):
        delta = event.angleDelta().y(); self.nav_file.emit(1 if delta < 0 else -1); event.accept()

    def _seek_video(self, event: QWheelEvent, seconds: int):
        if not self._player: return
        delta = event.angleDelta().y()
        dur = self._player.duration(); cur = self._player.position(); ms = seconds * 1000
        self._player.setPosition(int(max(0, min(dur, cur + (-ms if delta > 0 else ms))))); event.accept()

    def _zoom_at_cursor(self, event: QWheelEvent):
        if not self._orig_pix: return
        old_scale = self._scale; delta = event.angleDelta().y()
        factor = 1.25 if delta > 0 else 0.8
        self._scale = max(0.01, min(self._scale * factor, 10.0))
        cursor_pos = event.position()
        img_x = cursor_pos.x() - self._img_label.x(); img_y = cursor_pos.y() - self._img_label.y()
        self._render(); scale_ratio = self._scale / old_scale
        self._img_label.move(int(cursor_pos.x() - img_x * scale_ratio),
                             int(cursor_pos.y() - img_y * scale_ratio))

    def eventFilter(self, obj, event):
        if obj is self._video_overlay:
            t = event.type()
            if t == QEvent.Type.MouseButtonPress and event.button() == Qt.MouseButton.LeftButton:
                self._toggle_play(); return True
            elif t == QEvent.Type.Wheel: self.wheelEvent(event); return True
        return super().eventFilter(obj, event)

    def closeEvent(self, event):
        self._img_loader.stop()
        super().closeEvent(event)

    @property
    def scale_percent(self) -> int: return int(self._scale * 100)
