"""文件条目 + 扫描 + 图标 + 格式化工具"""
import os
from pathlib import Path
from datetime import datetime

from PySide6.QtWidgets import QApplication, QStyle
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon, QPixmap, QPainter, QBrush, QColor

from app.thumbnail import IMAGE_EXTS, VIDEO_EXTS as _TVIDEO_EXTS

# ── FileEntry ──
class FileEntry:
    __slots__ = ('name','path','ext','is_dir','size','mtime','ctime')

def _fast_scandir(dir_path: str) -> list[FileEntry]:
    entries = []
    try:
        with os.scandir(dir_path) as it:
            for entry in it:
                if entry.name.startswith('.'): continue
                try: stat = entry.stat(follow_symlinks=False)
                except OSError: stat = None
                fe = FileEntry.__new__(FileEntry)
                fe.name, fe.path, fe.ext = entry.name, entry.path, Path(entry.name).suffix.lower()
                fe.is_dir = entry.is_dir(follow_symlinks=False)
                fe.size = stat.st_size if stat else 0
                fe.mtime = stat.st_mtime if stat else 0
                fe.ctime = stat.st_ctime if stat else 0
                entries.append(fe)
    except PermissionError: pass
    return entries

# ── 格式化 ──
def format_size(num: float) -> str:
    for unit in ('B','KB','MB','GB','TB'):
        if abs(num) < 1024: return f'{num:.2f} {unit}'
        num /= 1024
    return f'{num:.2f} PB'

def format_date(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime('%Y/%m/%d - %H:%M:%S') if ts else ''

def mime_type(ext: str) -> str:
    types = {
        '.jpg':'JPEG 图片','.jpeg':'JPEG 图片','.png':'PNG 图片','.gif':'GIF 图片',
        '.bmp':'BMP 图片','.webp':'WebP 图片','.heic':'HEIC 图片','.heif':'HEIF 图片',
        '.tiff':'TIFF 图片','.tif':'TIFF 图片','.svg':'SVG 图片',
        '.mp4':'MP4 视频','.mov':'MOV 视频','.avi':'AVI 视频','.mkv':'MKV 视频',
        '.webm':'WebM 视频','.wmv':'WMV 视频','.flv':'FLV 视频',
        '.mp3':'MP3 音频','.wav':'WAV 音频','.flac':'FLAC 音频','.aac':'AAC 音频',
        '.ogg':'OGG 音频','.m4a':'M4A 音频',
        '.zip':'ZIP 压缩','.rar':'RAR 压缩','.7z':'7Z 压缩',
        '.txt':'文本文档','.md':'Markdown','.py':'Python','.js':'JavaScript',
        '.pdf':'PDF 文档','.doc':'Word 文档','.docx':'Word 文档','.xlsx':'Excel 表格',
        '.exe':'应用程序','.dll':'动态链接库',
    }
    return types.get(ext, f'{ext.upper()[1:]} 文件')

_DIM_CACHE: dict[str, tuple[int, int]] = {}
def _img_dim(file_path: str) -> tuple[int, int]:
    if file_path not in _DIM_CACHE:
        try:
            from PySide6.QtGui import QImageReader
            r = QImageReader(file_path)
            s = r.size()
            _DIM_CACHE[file_path] = (s.width(), s.height())
        except Exception:
            _DIM_CACHE[file_path] = (0, 0)
    return _DIM_CACHE[file_path]

def read_exif_date(file_path: str) -> float:
    try:
        img = __import__('PIL.Image', fromlist=['open']).open(file_path)
        exif = img.getexif()
        if exif:
            for tag, value in exif.items():
                from PIL.ExifTags import Base as ExifBase
                if ExifBase(tag).name == 'DateTimeOriginal':
                    return datetime.strptime(value, '%Y:%m:%d %H:%M:%S').timestamp()
    except: pass
    return 0

# ── 图标 ──
_ICONS: dict[str, QIcon] = {}
def _type_icon(ext: str) -> QIcon:
    if ext not in _ICONS:
        style = QApplication.style()
        if ext in (IMAGE_EXTS | _TVIDEO_EXTS):
            _ICONS[ext] = style.standardIcon(QStyle.StandardPixmap.SP_FileDialogContentsView)
        else:
            _ICONS[ext] = style.standardIcon(QStyle.StandardPixmap.SP_FileIcon)
    return _ICONS[ext]

_FOLDER_ICONS: dict[int, QIcon] = {}
def _folder_icon(size: int) -> QIcon:
    if size not in _FOLDER_ICONS:
        pix = QPixmap(size, size); pix.fill(Qt.GlobalColor.transparent)
        p = QPainter(pix); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        c = QColor('#E8B830')
        s = size * 0.72; x = (size - s) / 2; y = size * 0.18
        # 扁平文件夹主体
        p.setBrush(QBrush(c))
        p.drawRoundedRect(x, y + s * 0.1, s, s * 0.78, 2, 2)
        # 文件夹标签
        p.drawRoundedRect(x, y, s * 0.50, s * 0.18, 2, 2)
        p.end(); _FOLDER_ICONS[size] = QIcon(pix)
    return _FOLDER_ICONS[size]

_FILE_ICON_PROVIDER = None
def _file_system_icon(file_path: str) -> QIcon:
    try:
        global _FILE_ICON_PROVIDER
        if _FILE_ICON_PROVIDER is None:
            from PySide6.QtWidgets import QFileIconProvider
            _FILE_ICON_PROVIDER = QFileIconProvider()
        from PySide6.QtCore import QFileInfo
        return _FILE_ICON_PROVIDER.icon(QFileInfo(file_path))
    except:
        return _type_icon(Path(file_path).suffix.lower())
