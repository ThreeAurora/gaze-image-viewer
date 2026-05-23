"""右键菜单 —— Live Photo 专用操作"""
import os
from pathlib import Path

from PySide6.QtWidgets import QMenu, QMessageBox, QApplication
from PySide6.QtCore import Qt

from app.live_photo import get_video_path
from app.frame_extract import extract_frames


class FileContextMenu(QMenu):
    """文件右键菜单"""

    def __init__(self, parent_card):
        super().__init__(parent_card)
        self.card = parent_card
        self.file_path = parent_card.file_path
        self.is_live = parent_card.is_live
        self.live_info = parent_card.live_info

        self.setStyleSheet(
            'QMenu { background: #252525; color: #CCC; border: 1px solid #444; '
            'border-radius: 4px; padding: 4px; font-size: 12px; }'
            'QMenu::item { padding: 6px 24px; border-radius: 2px; }'
            'QMenu::item:hover { background: #0078D7; color: #FFF; }'
            'QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }'
        )
        self._build()

    def _build(self):
        # 打开
        act_open = self.addAction('打开')
        act_open.triggered.connect(lambda: os.startfile(self.file_path))

        self.addSeparator()

        # Live Photo 专用操作
        if self.is_live and self.live_info:
            video_path = self.live_info.get('video_path', self.file_path)

            # Apple: 直接播放 .mov
            if self.live_info.get('embedded') == False:
                act_play = self.addAction('播放实况视频')
                act_play.triggered.connect(lambda: os.startfile(video_path))

            self.addSeparator()

            # 拆帧
            act_frames = self.addAction('拆帧保存...')
            act_frames.triggered.connect(self._extract_frames)

    def _extract_frames(self):
        """提取视频帧"""
        video_path = get_video_path(self.file_path)
        if not video_path:
            QMessageBox.warning(self, '错误', '找不到对应的视频文件')
            return

        # 默认输出目录：图片所在目录下的 frames 子目录
        default_dir = str(Path(self.file_path).parent / f'{Path(self.file_path).stem}_frames')

        reply = QMessageBox.question(
            self.parent(), '拆帧保存',
            f'将从视频提取所有帧，保存到：\n{default_dir}\n\n继续？',
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        try:
            count = extract_frames(video_path, default_dir)
            QMessageBox.information(
                self.parent(), '完成',
                f'共提取 {count} 帧，已保存到：\n{default_dir}',
            )
        except RuntimeError as e:
            QMessageBox.critical(self.parent(), '错误', str(e))
        except Exception as e:
            QMessageBox.critical(self.parent(), '错误', f'提取失败：{e}')
