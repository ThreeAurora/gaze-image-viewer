"""后台缩略图加载器 —— 多线程 QThreadPool 并行生成（对标 XnView MP）"""
from PySide6.QtCore import QThreadPool, QRunnable, Signal, QObject, QMutex, QMutexLocker
from PySide6.QtGui import QPixmap
from app.thumbnail import generate as generate_thumb


class _ThumbTask(QRunnable):
    """单个缩略图生成任务"""
    def __init__(self, file_path: str, size: int, callback):
        super().__init__()
        self._path = file_path
        self._size = size
        self._cb = callback

    def run(self):
        pix = generate_thumb(self._path, self._size)
        if pix and not pix.isNull():
            self._cb(self._path, pix)


class ThumbLoader(QObject):
    """多线程缩略图加载器 —— QThreadPool + 去重队列"""
    loaded = Signal(str, QPixmap)

    def __init__(self):
        super().__init__()
        self._pool = QThreadPool()
        self._pool.setMaxThreadCount(4)  # 4 线程并行
        self._pending: set[str] = set()  # 去重
        self._mutex = QMutex()

    def enqueue(self, file_path: str, size: int, is_video: bool = False):
        key = f'{file_path}:{size}'
        with QMutexLocker(self._mutex):
            if key in self._pending:
                return
            self._pending.add(key)

        task = _ThumbTask(file_path, size, self._on_done)
        self._pool.start(task)

    def clear_queue(self):
        self._pool.clear()
        with QMutexLocker(self._mutex):
            self._pending.clear()

    def _on_done(self, file_path: str, pix: QPixmap):
        key = f'{file_path}:{pix.width()}'
        with QMutexLocker(self._mutex):
            self._pending.discard(key)
        self.loaded.emit(file_path, pix)

    def stop(self):
        self._pool.clear()
        self._pool.waitForDone(500)
        with QMutexLocker(self._mutex):
            self._pending.clear()
