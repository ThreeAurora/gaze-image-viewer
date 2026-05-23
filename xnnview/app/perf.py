"""性能计时工具"""
import time
from pathlib import Path
LOG_PATH = Path(__file__).parent.parent / 'perf_log.txt'

_last_hb = 0
_seq = 0


def log(msg: str):
    global _seq
    _seq += 1
    ts = time.time()
    try:
        with open(LOG_PATH, 'a', encoding='utf-8') as f:
            f.write(f'{ts:.3f} [{_seq}] {msg}\n')
    except:
        pass


_hb_timer = None


def start_hb():
    global _hb_timer, _last_hb
    _last_hb = time.time()
    try:
        from PySide6.QtCore import QTimer, QCoreApplication
        _hb_timer = QTimer()
        _hb_timer.timeout.connect(_hb_tick)
        _hb_timer.start(100)
    except:
        pass


def _hb_tick():
    global _last_hb
    now = time.time()
    gap = now - _last_hb
    if gap > 1.0:
        log(f'FREEZE {gap:.1f}s')
    _last_hb = now


def hook_method(obj, name):
    """给对象的方法加打点"""
    original = getattr(obj, name)
    def wrapped(*args, **kwargs):
        entry = f'{obj.__class__.__name__}.{name}'
        start = time.time()
        try:
            return original(*args, **kwargs)
        finally:
            elapsed = (time.time() - start) * 1000
            if elapsed > 50:
                log(f'SLOW {entry}: {elapsed:.0f}ms')
    setattr(obj, name, wrapped)
