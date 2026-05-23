"""Live Photo / 动态照片识别 —— 支持 Apple / 全安卓（JPEG EOI 定位嵌入视频）"""
import os
import re
import tempfile
from pathlib import Path

MOTION_IMAGE_EXT = {'.heic', '.heif', '.jpg', '.jpeg', '.png'}
COMPANION_VIDEO_EXT = {'.mov', '.mp4', '.MP4', '.MOV'}

# JPEG 结尾标记
JPEG_EOI = b'\xff\xd9'
MP4_FTYP = b'ftyp'
# 常见 MP4 ftyp 品牌
MP4_BRANDS = {b'isom', b'iso2', b'avc1', b'mp41', b'mp42', b'MSNV', b'mmp4', b'3gp5', b'3gp4', b'3gp6',
              b'qt  ', b'MSNV', b'M4V ', b'M4A ', b'F4V ', b'F4P ', b'F4A ', b'F4B '}


def detect_companion(image_path: str) -> dict | None:
    """检测外部配对视频（Apple Live Photo + 部分安卓）"""
    p = Path(image_path)
    if p.suffix.lower() not in MOTION_IMAGE_EXT:
        return None
    bases = {p.stem}
    if p.stem.startswith('MV'):
        bases.add(p.stem[2:])
    for b in bases:
        for ve in COMPANION_VIDEO_EXT:
            video_path = p.with_name(b + ve)
            if video_path.exists():
                return {'type': 'companion', 'video_path': str(video_path), 'embedded': False}
    return None


def detect_embedded(image_path: str) -> dict | None:
    """检测 JPEG 内嵌 MP4 视频（通过 JPEG EOI + ftyp 定位，不限大小）"""
    p = Path(image_path)
    if p.suffix.lower() not in MOTION_IMAGE_EXT:
        return None
    try:
        file_size = p.stat().st_size
        if file_size < 100 * 1024:
            return None

        with open(image_path, 'rb') as f:
            # 1. 搜索 JPEG EOI (FF D9) 最后出现的位置 → JPEG 图片终点
            chunk = 65536
            f.seek(max(0, file_size - chunk))
            tail = f.read(chunk)
            eoi_pos = tail.rfind(JPEG_EOI)
            if eoi_pos == -1:
                # 扩大范围
                f.seek(max(0, file_size - 5 * 1024 * 1024))
                tail = f.read()
                eoi_pos = tail.rfind(JPEG_EOI)

            if eoi_pos >= 0:
                eoi_offset = file_size - len(tail) + eoi_pos + 2
                # 2. EOI 之后如果是 ftyp → 嵌入 MP4
                if eoi_offset + 8 < file_size:
                    f.seek(eoi_offset)
                    marker = f.read(12)
                    if marker[4:8] == MP4_FTYP and marker[8:12] in MP4_BRANDS:
                        return {
                            'type': 'motion',
                            'video_path': image_path,
                            'video_offset': eoi_offset,
                            'video_length': file_size - eoi_offset,
                            'embedded': True,
                        }

            # 3. 回退：XMP 元数据确认
            f.seek(0)
            head = f.read(131072)
            head_str = head.decode('utf-8', errors='ignore')
            has_xmp = bool(re.search(
                r'MotionPhoto[^<]*>\s*1\s*<|MicroVideo[^<]*>\s*1\s*<|'
                r'http://ns\.google\.com/photos/1\.0/camera/',
                head_str
            ))
            if has_xmp:
                # 有 XMP 但没找到 EOI+ftyp → 扫描整个文件找任意 ftyp
                f.seek(0)
                data = f.read()
                pos = -1
                while True:
                    pos = data.find(MP4_FTYP, pos + 1)
                    if pos < 1024: continue  # 跳过图片内部的假ftyp
                    if pos == -1: break
                    if pos + 8 < len(data) and data[pos+4:pos+8] in MP4_BRANDS:
                        break  # 找到真正的MP4
                    pos = -1
                if pos > 1024:
                    return {
                        'type': 'motion',
                        'video_path': image_path,
                        'video_offset': pos - 4,
                        'video_length': file_size - (pos - 4),
                        'embedded': True,
                    }
                return {'type': 'motion', 'video_path': image_path, 'embedded': True}

            return None
    except Exception:
        return None


def detect(image_path: str) -> dict | None:
    p = Path(image_path)
    if not p.is_file() or p.suffix.lower() not in MOTION_IMAGE_EXT:
        return None
    result = detect_companion(image_path)
    if result:
        return result
    result = detect_embedded(image_path)
    if result and 'video_offset' in result:
        return result
    if result:
        return result  # XMP only, no offset
    return None


def get_video_path(image_path: str) -> str | None:
    info = detect(image_path)
    if info:
        return info['video_path']
    return None


def extract_embedded_video(image_path: str, info: dict) -> str | None:
    if not info.get('embedded') or 'video_offset' not in info:
        return None
    try:
        with open(image_path, 'rb') as f:
            f.seek(info['video_offset'])
            data = f.read(info.get('video_length', 100 * 1024 * 1024))
        # 写到临时文件
        raw = tempfile.NamedTemporaryFile(suffix='.mp4', delete=False)
        raw.write(data); raw.close()
        # ffmpeg 修复 MOOV atom（确保时长正确）
        import subprocess
        fixed = tempfile.NamedTemporaryFile(suffix='.mp4', delete=False)
        fixed.close()
        subprocess.run([
            'ffmpeg', '-i', raw.name, '-c', 'copy', '-movflags', 'faststart',
            '-y', fixed.name,
        ], capture_output=True, timeout=10,
           creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        if os.path.exists(fixed.name) and os.path.getsize(fixed.name) > 0:
            os.unlink(raw.name)
            return fixed.name
        # ffmpeg 失败，返回原始文件
        return raw.name
    except Exception:
        return None
