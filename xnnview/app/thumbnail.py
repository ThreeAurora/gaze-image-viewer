"""缩略图生成器 — WebP 缓存 + 多线程（对标 XnView MP）

策略:
1. 统一用 480px 标准尺寸生成 WebP 缩略图（~1KB/张）
2. 显示时按需缩放到目标尺寸
3. SQLite WAL + 内存 LRU
4. QThreadPool 4 线程并行
"""

import hashlib, os, sqlite3, subprocess, tempfile, time
from pathlib import Path

IMAGE_EXTS = {'.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp',
              '.heic', '.heif', '.tiff', '.tif', '.ico', '.svg'}
VIDEO_EXTS = {'.mp4', '.mov', '.avi', '.mkv', '.webm', '.wmv', '.flv', '.m4v', '.mpg', '.mpeg', '.3gp'}

STD_SIZE = 480   # 标准缩略图尺寸（对标 XnView MP 465x365）
_cache_db: str | None = None
MAX_DB_SIZE_MB = 1024
_mem_cache: dict[str, 'QPixmap'] = {}
_mem_cache_mtime: dict[str, float] = {}
_MEM_CACHE_MAX = 500


def _db_path() -> str:
    global _cache_db
    if _cache_db is None:
        _cache_db = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'thumbnails.db')
    return _cache_db


def _get_db() -> sqlite3.Connection:
    db = sqlite3.connect(_db_path())
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('PRAGMA synchronous=NORMAL')
    db.execute('PRAGMA cache_size=-65536')
    db.execute('CREATE TABLE IF NOT EXISTS thumbs (key TEXT PRIMARY KEY, png BLOB, mtime REAL, atime REAL DEFAULT 0)')
    db.execute('CREATE INDEX IF NOT EXISTS idx_atime ON thumbs(atime)')
    db.commit()
    return db


def _cache_key(file_path: str, size: int) -> str:
    # 不分尺寸，每文件只存一张标准缩略图（对标 XnView MP）
    return hashlib.md5(file_path.encode()).hexdigest()


def _scale_pix(pix: 'QPixmap', size: int) -> 'QPixmap':
    from PySide6.QtCore import Qt
    if pix.isNull() or max(pix.width(), pix.height()) <= size: return pix
    return pix.scaled(size, size, Qt.AspectRatioMode.KeepAspectRatio,
                      Qt.TransformationMode.SmoothTransformation)


def generate(file_path: str, size: int = 160) -> 'QPixmap | None':
    from PySide6.QtGui import QPixmap

    ext = Path(file_path).suffix.lower()
    key = _cache_key(file_path, size)

    # 1. 内存
    try:
        mtime = os.path.getmtime(file_path)
        if key in _mem_cache and _mem_cache_mtime.get(key) == mtime:
            return _scale_pix(_mem_cache[key], size)
    except Exception:
        mtime = 0

    # 2. SQLite (WebP)
    db = None
    try:
        db = _get_db()
        row = db.execute('SELECT png, mtime FROM thumbs WHERE key=?', (key,)).fetchone()
        if row and row[1] == mtime:
            pix = QPixmap(); pix.loadFromData(row[0], 'WEBP')
            if not pix.isNull():
                db.execute('UPDATE thumbs SET atime=? WHERE key=?', (time.time(), key)); db.commit()
                _mem_cache[key] = pix; _mem_cache_mtime[key] = mtime
                return _scale_pix(pix, size)
    except Exception:
        db = None

    # 3. 统一以标准尺寸生成
    if ext in IMAGE_EXTS:
        pix = _image_thumb(file_path, STD_SIZE)
    elif ext in VIDEO_EXTS:
        pix = _video_thumb(file_path, STD_SIZE)
    else:
        return None

    # 4. WebP 缓存
    if pix:
        try:
            if len(_mem_cache) > _MEM_CACHE_MAX:
                oldest = min(_mem_cache.keys(), key=lambda k: _mem_cache_mtime.get(k, 0))
                _mem_cache.pop(oldest, None); _mem_cache_mtime.pop(oldest, None)
            _mem_cache[key] = pix; _mem_cache_mtime[key] = mtime
            if db is None: db = _get_db()
            webp = _pixmap_to_webp(pix)
            db.execute('INSERT OR REPLACE INTO thumbs VALUES (?,?,?,?)', (key, webp, mtime, time.time()))
            db.commit()
        except Exception: pass
        pix = _scale_pix(pix, size)
    return pix


def _image_thumb(file_path: str, size: int) -> 'QPixmap | None':
    from PySide6.QtGui import QImageReader, QPixmap
    from PySide6.QtCore import Qt

    r = QImageReader(file_path); r.setAutoTransform(True)
    img = r.read()
    if img.isNull(): return None
    scaled = img.scaled(size, size, Qt.AspectRatioMode.KeepAspectRatio,
                        Qt.TransformationMode.SmoothTransformation)
    return QPixmap.fromImage(scaled)


def _video_thumb(file_path: str, size: int) -> 'QPixmap | None':
    from PySide6.QtGui import QPixmap
    try:
        tmp = tempfile.NamedTemporaryFile(suffix='.png', delete=False); tmp.close()
        subprocess.run(['ffmpeg', '-ss', '1', '-i', file_path, '-vframes', '1',
            '-vf', f'scale={size}:{size}:force_original_aspect_ratio=decrease,pad={size}:{size}:(ow-iw)/2:(oh-ih)/2:color=#0A0A0A',
            '-q:v', '5', '-y', tmp.name],
            capture_output=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        if os.path.exists(tmp.name) and os.path.getsize(tmp.name) > 0:
            pix = QPixmap(tmp.name); os.unlink(tmp.name)
            return pix if not pix.isNull() else None
    except Exception: pass
    return None


def _pixmap_to_webp(pixmap: 'QPixmap') -> bytes:
    from PySide6.QtCore import QByteArray, QBuffer, QIODevice
    ba = QByteArray(); buf = QBuffer(ba); buf.open(QIODevice.OpenModeFlag.WriteOnly)
    pixmap.save(buf, 'WEBP', 80)  # 质量 80，对标 XnView MP
    buf.close()
    return ba.data()
