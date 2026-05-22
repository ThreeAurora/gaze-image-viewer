"""视频帧提取 —— 使用 ffmpeg"""
import os
import subprocess
from pathlib import Path


def extract_frames(video_path: str, output_dir: str) -> int:
    """
    提取视频所有帧到指定目录
    返回提取的帧数
    """
    os.makedirs(output_dir, exist_ok=True)
    output_pattern = os.path.join(output_dir, 'frame_%05d.png')

    try:
        result = subprocess.run(
            [
                'ffmpeg',
                '-i', video_path,
                '-vsync', '0',        # 不丢帧
                '-q:v', '2',           # 高质量 PNG
                '-y',                   # 覆盖已有文件
                output_pattern,
            ],
            capture_output=True,
            timeout=120,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0,
        )
    except subprocess.TimeoutExpired:
        pass
    except FileNotFoundError:
        raise RuntimeError('未找到 ffmpeg，请先安装 ffmpeg 并添加到 PATH')

    # 统计生成的帧数
    frame_files = list(Path(output_dir).glob('frame_*.png'))
    return len(frame_files)
