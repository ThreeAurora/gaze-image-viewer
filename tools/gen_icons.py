# -*- coding: utf-8 -*-
"""
Gaze 应用图标生成器(自绘)
================================
用法:  python tools/gen_icons.py
输出:  src/assets/icons/*.png  (48x48,由 192x192 4 倍超采样缩小)

背景:原 icons-48 取自 XnView MP(个人自用、不得分发)。本脚本用
Pillow 从零绘制全部所需图标,几何原语直接可读——每个图标一个函数,
颜色/线宽全局统一,是图标本体的唯一来源,改样式只需改这里再跑一遍。

风格:扁平双色。主色蓝 #4E8EF7、文件夹琥珀 #F0A93B、警示红 #E5534B、
中性灰蓝 #8E9BA8。线条 12px(超采样坐标系),圆头。
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

S = 192          # 超采样画布
OUT = 48         # 输出尺寸
W = 12           # 主线宽
BLUE = (78, 142, 247, 255)
DEEP = (43, 108, 176, 255)
AMBER = (240, 169, 59, 255)
AMBER_D = (204, 137, 34, 255)
RED = (229, 83, 75, 255)
GREEN = (67, 163, 95, 255)
SLATE = (142, 155, 168, 255)
DARK = (91, 103, 112, 255)
WHITE = (255, 255, 255, 255)
FONT = "C:/Windows/Fonts/seguisb.ttf"


def canvas():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def rline(d, p1, p2, w=W, fill=SLATE):
    """圆头线段。"""
    d.line([p1, p2], fill=fill, width=w)
    r = w / 2
    for (x, y) in (p1, p2):
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def dashed(d, p1, p2, dash=14, gap=12, w=W, fill=SLATE):
    """虚线段。"""
    x1, y1 = p1
    x2, y2 = p2
    total = math.hypot(x2 - x1, y2 - y1)
    if total == 0:
        return
    ux, uy = (x2 - x1) / total, (y2 - y1) / total
    t = 0.0
    while t < total:
        e = min(t + dash, total)
        rline(d, (x1 + ux * t, y1 + uy * t), (x1 + ux * e, y1 + uy * e), w, fill)
        t = e + gap


def rrect(d, box, r=16, fill=None, outline=None, width=W):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def folder(d, x0=28, y0=64, x1=164, y1=150, body=AMBER, lip=AMBER_D):
    """文件夹:左侧凸舌整块多边形(经典平涂形)。返回底部 y。"""
    d.polygon([(x0, y0 + 24), (x0 + 52, y0 + 24), (x0 + 68, y0),
               (x1, y0), (x1, y1), (x0, y1)], fill=lip)
    d.rounded_rectangle([x0, y0 + 24, x1, y1], radius=12, fill=body)
    return y1


def doc(d, box, outline=SLATE, fill=WHITE, r=12):
    rrect(d, box, r=r, fill=fill, outline=outline)


def chevron(d, cx, cy, size, right=True, color=BLUE, w=W + 2):
    s = size
    if right:
        rline(d, (cx - s * 0.5, cy - s), (cx + s * 0.5, cy), w, color)
        rline(d, (cx + s * 0.5, cy), (cx - s * 0.5, cy + s), w, color)
    else:
        rline(d, (cx + s * 0.5, cy - s), (cx - s * 0.5, cy), w, color)
        rline(d, (cx - s * 0.5, cy), (cx + s * 0.5, cy + s), w, color)


def magnifier(d, cx, cy, r, color=BLUE, handle=SLATE):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=W)
    hx, hy = cx + r * 0.74, cy + r * 0.74
    rline(d, (hx, hy), (hx + 34, hy + 34), W + 2, handle)


def text(d, xy, s, size, fill=WHITE, font_path=FONT, anchor="mm"):
    f = ImageFont.truetype(font_path, size)
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


# ─────────────────────────── 图标定义 ───────────────────────────

def i_up(d):
    folder(d)
    ax = 96
    rline(d, (ax, 24), (ax, 52), W, BLUE)
    rline(d, (ax - 16, 40), (ax, 24), W, BLUE)
    rline(d, (ax, 24), (ax + 16, 40), W, BLUE)


def i_cmd_open(d):
    folder(d, y0=72, y1=152)
    d.polygon([(96, 18), (150, 60), (118, 60), (118, 86), (74, 86), (74, 60), (42, 60)], fill=BLUE)


def i_cmd_browse(d):
    doc(d, (40, 22, 152, 160), outline=SLATE)
    for i, ln in enumerate((64, 90, 116)):
        rline(d, (58, ln), (134, ln), 9, SLATE if i else BLUE)
    d.ellipse([92, 92, 176, 176], fill=AMBER, outline=WHITE, width=8)
    rline(d, (134, 134), (134, 158), 9, DARK)
    rline(d, (134, 134), (152, 134), 9, DARK)


def i_cmd_copy(d):
    rrect(d, (36, 36, 124, 148), r=12, outline=SLATE)
    rrect(d, (70, 60, 158, 172), r=12, fill=WHITE, outline=BLUE)


def i_cmd_copyPath(d):
    # 文档带路径行 + 右下角复制双卡:复制路径
    doc(d, (28, 24, 128, 148), outline=SLATE)
    rline(d, (48, 60), (108, 60), 9, BLUE)
    rline(d, (48, 88), (108, 88), 9, SLATE)
    rline(d, (48, 116), (84, 116), 9, SLATE)
    rrect(d, (84, 96, 148, 160), r=10, outline=BLUE, width=10)
    rrect(d, (116, 128, 180, 192), r=10, fill=WHITE, outline=BLUE, width=10)


def i_cmd_copyTo(d):
    folder(d, y0=84, y1=160)
    rline(d, (96, 20), (96, 58), W, BLUE)
    rline(d, (96, 58), (76, 38), W, BLUE)
    rline(d, (96, 58), (116, 38), W, BLUE)


def i_cmd_crop(d):
    rline(d, (24, 138), (138, 138), W, SLATE)
    rline(d, (138, 54), (138, 168), W, SLATE)
    rline(d, (54, 24), (168, 24), W, BLUE)
    rline(d, (24, 24), (24, 54), W, BLUE)
    rline(d, (78, 108), (132, 54), W, BLUE)


def i_cmd_cut(d):
    d.ellipse([24, 96, 76, 148], outline=BLUE, width=W)
    d.ellipse([116, 96, 168, 148], outline=BLUE, width=W)
    rline(d, (62, 116), (140, 30), W, SLATE)
    rline(d, (130, 116), (52, 30), W, SLATE)


def i_cmd_delete(d):
    rrect(d, (48, 62, 144, 170), r=14, fill=RED)
    rrect(d, (32, 38, 160, 62), r=9, fill=RED)
    rrect(d, (74, 18, 118, 44), r=9, fill=RED)
    for x in (76, 116):
        rline(d, (x, 84), (x, 148), 10, WHITE)


def i_cmd_editMetadata(d):
    doc(d, (36, 24, 148, 168), outline=SLATE)
    rline(d, (58, 62), (126, 62), 9, SLATE)
    rline(d, (58, 92), (108, 92), 9, SLATE)
    d.polygon([(100, 156), (146, 110), (168, 132), (122, 178)], fill=AMBER)
    d.polygon([(92, 164), (100, 156), (122, 178), (114, 186)], fill=DARK)


def i_cmd_fileNext(d):
    chevron(d, 96, 96, 52, right=True, color=BLUE)


def i_cmd_filePrevious(d):
    chevron(d, 96, 96, 52, right=False, color=BLUE)


def i_cmd_filter(d):
    d.polygon([(24, 30), (168, 30), (114, 106), (114, 162), (78, 162), (78, 106)], fill=BLUE)


def i_cmd_fullscreen(d):
    for (x, y, dx, dy) in ((30, 30, 1, 1), (162, 30, -1, 1), (30, 162, 1, -1), (162, 162, -1, -1)):
        rline(d, (x, y), (x + dx * 48, y), W, BLUE)
        rline(d, (x, y), (x, y + dy * 48), W, BLUE)


def i_cmd_horizontalFlip(d):
    dashed(d, (96, 16), (96, 176), fill=SLATE)
    d.polygon([(76, 96), (28, 56), (28, 136)], fill=SLATE)
    d.polygon([(116, 96), (164, 56), (164, 136)], fill=BLUE)


def i_cmd_verticalFlip(d):
    dashed(d, (16, 96), (176, 96), fill=SLATE)
    d.polygon([(96, 76), (56, 28), (136, 28)], fill=SLATE)
    d.polygon([(96, 116), (56, 164), (136, 164)], fill=BLUE)


def i_cmd_newFolder(d):
    folder(d)
    rrect(d, (108, 92, 172, 156), r=18, fill=GREEN)
    rline(d, (140, 110), (140, 138), 11, WHITE)
    rline(d, (126, 124), (154, 124), 11, WHITE)


def i_cmd_openProperties(d):
    # 属性对话框:窗口 + 标题栏 + 内容列表三行
    rrect(d, (24, 40, 168, 160), r=14, outline=SLATE)
    rrect(d, (24, 40, 168, 74), r=14, fill=BLUE)
    for i, w in enumerate((88, 116, 64)):
        rline(d, (46, 96 + i * 26), (46 + w, 96 + i * 26), 9, SLATE if i else BLUE)


def i_cmd_openWith(d):
    rrect(d, (24, 30, 146, 148), r=14, outline=SLATE)
    rrect(d, (24, 30, 146, 62), r=14, fill=BLUE)
    d.polygon([(96, 92), (168, 116), (134, 130), (152, 164), (138, 172), (120, 138), (96, 152)], fill=DARK)


def i_cmd_options(d):
    for i, y in enumerate((56, 96, 136)):
        rline(d, (28, y), (164, y), 10, SLATE)
        x = (56, 116, 84)[i]
        d.ellipse([x - 17, y - 17, x + 17, y + 17], fill=BLUE)


def i_cmd_paneIcons(d):
    for (x, y, c) in ((30, 30, BLUE), (106, 30, SLATE), (30, 106, SLATE), (106, 106, BLUE)):
        rrect(d, (x, y, x + 56, y + 56), r=12, fill=c)


def i_cmd_paneThumbs(d):
    rrect(d, (26, 26, 98, 98), r=10, outline=SLATE)
    d.polygon([(36, 92), (58, 58), (76, 82), (90, 64), (92, 92)], fill=AMBER)
    d.ellipse([40, 40, 54, 54], fill=BLUE)
    rrect(d, (106, 26, 166, 98), r=10, outline=BLUE)
    rrect(d, (26, 106, 98, 166), r=10, outline=BLUE)
    rrect(d, (106, 106, 166, 166), r=10, fill=SLATE)


def i_cmd_paste(d):
    rrect(d, (34, 30, 158, 170), r=14, outline=BLUE)
    rrect(d, (66, 14, 126, 46), r=10, fill=BLUE)
    rrect(d, (58, 58, 134, 150), r=8, fill=WHITE, outline=SLATE, width=8)
    for y in (86, 108, 130):
        rline(d, (74, y), (118, y), 8, SLATE)


def i_cmd_print(d):
    rrect(d, (58, 20, 134, 66), r=8, outline=SLATE)
    rrect(d, (28, 66, 164, 128), r=14, fill=SLATE)
    d.ellipse([140, 82, 156, 98], fill=WHITE)
    rrect(d, (58, 128, 134, 172), r=8, fill=WHITE, outline=BLUE)


def i_cmd_refresh(d):
    d.arc([30, 30, 162, 162], start=300, end=230, fill=BLUE, width=W + 4)
    d.polygon([(162, 24), (162, 84), (120, 56)], fill=BLUE)


def i_cmd_rename(d):
    text(d, (76, 84), "A", 96, fill=BLUE)
    d.polygon([(108, 156), (154, 110), (176, 132), (130, 178)], fill=AMBER)
    d.polygon([(100, 164), (108, 156), (130, 178), (122, 186)], fill=DARK)


def i_cmd_rotate(d):
    rrect(d, (56, 52, 136, 140), r=10, outline=BLUE)
    d.arc([24, 24, 168, 168], start=-40, end=200, fill=GREEN, width=W + 4)
    d.polygon([(168, 40), (168, 96), (128, 64)], fill=GREEN)


def i_cmd_rotate90(d):
    rrect(d, (56, 52, 136, 140), r=10, outline=BLUE)
    d.arc([24, 24, 168, 168], start=320, end=170, fill=GREEN, width=W + 4)
    d.polygon([(24, 40), (24, 96), (64, 64)], fill=GREEN)


def i_cmd_rotate270(d):
    rrect(d, (56, 52, 136, 140), r=10, outline=BLUE)
    d.arc([24, 24, 168, 168], start=140, end=-10, fill=GREEN, width=W + 4)
    d.polygon([(168, 40), (168, 96), (128, 64)], fill=GREEN)


def i_cmd_search(d):
    magnifier(d, 88, 88, 52)


def i_cmd_selectAllFile(d):
    for (x, y) in ((40, 40), (96, 40), (40, 96), (96, 96)):
        rrect(d, (x, y, x + 44, y + 44), r=8, outline=SLATE, width=8)
    # 选中态:左上一格蓝底 + 白勾
    rrect(d, (40, 40, 84, 84), r=8, fill=BLUE)
    rline(d, (52, 64), (60, 72), 8, WHITE)
    rline(d, (60, 72), (74, 52), 8, WHITE)


def i_cmd_showFilesInFolder(d):
    folder(d, y0=88, y1=164)
    doc(d, (56, 20, 140, 104), outline=BLUE)
    rline(d, (72, 44), (124, 44), 8, SLATE)
    rline(d, (72, 66), (124, 66), 8, SLATE)
    rline(d, (72, 88), (104, 88), 8, SLATE)


def i_cmd_showRed(d):
    rrect(d, (28, 28, 164, 164), r=20, fill=RED)
    text(d, (96, 96), "R", 84, fill=WHITE)


def i_label_item(d):
    # 标签:45° 斜置矩形 + 近端穿孔
    cx, cy = 96, 96
    hw, hh = 62, 30
    a = math.radians(-45)
    ca, sa = math.cos(a), math.sin(a)
    pts = [(cx + px * ca - py * sa, cy + px * sa + py * ca)
           for (px, py) in ((-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh))]
    d.polygon(pts, fill=AMBER)
    hx, hy = cx - 44 * ca, cy - 44 * sa      # 孔在标签长轴近端
    d.ellipse([hx - 11, hy - 11, hx + 11, hy + 11], fill=WHITE)


def i_min_moveTo(d):
    folder(d, x0=66, y0=56, x1=170, y1=148)
    rline(d, (24, 96), (58, 96), W, BLUE)
    d.polygon([(84, 96), (48, 72), (48, 120)], fill=BLUE)


def i_sort(d):
    for i, w in enumerate((112, 76, 40)):
        rline(d, (28, 52 + i * 44), (28 + w, 52 + i * 44), 11, SLATE)
    rline(d, (150, 56), (150, 140), W, BLUE)
    rline(d, (150, 56), (132, 76), W, BLUE)
    rline(d, (150, 56), (168, 76), W, BLUE)
    rline(d, (150, 140), (132, 120), W, BLUE)
    rline(d, (150, 140), (168, 120), W, BLUE)


def i_viewas(d):
    # 眼睛:横椭圆眼眶 + 蓝瞳 + 高光
    d.ellipse([16, 52, 176, 140], outline=SLATE, width=W)
    d.ellipse([64, 66, 128, 130], fill=BLUE)
    d.ellipse([82, 84, 102, 104], fill=WHITE)


ICONS = {
    "up": i_up,
    "cmd_open": i_cmd_open,
    "cmd_browse": i_cmd_browse,
    "cmd_copy": i_cmd_copy,
    "cmd_copyPath": i_cmd_copyPath,
    "cmd_copyTo": i_cmd_copyTo,
    "cmd_crop": i_cmd_crop,
    "cmd_cut": i_cmd_cut,
    "cmd_delete": i_cmd_delete,
    "cmd_editMetadata": i_cmd_editMetadata,
    "cmd_fileNext": i_cmd_fileNext,
    "cmd_filePrevious": i_cmd_filePrevious,
    "cmd_filter": i_cmd_filter,
    "cmd_fullscreen": i_cmd_fullscreen,
    "cmd_horizontalFlip": i_cmd_horizontalFlip,
    "cmd_verticalFlip": i_cmd_verticalFlip,
    "cmd_newFolder": i_cmd_newFolder,
    "cmd_openProperties": i_cmd_openProperties,
    "cmd_openWith": i_cmd_openWith,
    "cmd_options": i_cmd_options,
    "cmd_paneIcons": i_cmd_paneIcons,
    "cmd_paneThumbs": i_cmd_paneThumbs,
    "cmd_paste": i_cmd_paste,
    "cmd_print": i_cmd_print,
    "cmd_refresh": i_cmd_refresh,
    "cmd_rename": i_cmd_rename,
    "cmd_rotate": i_cmd_rotate,
    "cmd_rotate90": i_cmd_rotate90,
    "cmd_rotate270": i_cmd_rotate270,
    "cmd_search": i_cmd_search,
    "cmd_selectAllFile": i_cmd_selectAllFile,
    "cmd_showFilesInFolder": i_cmd_showFilesInFolder,
    "cmd_showRed": i_cmd_showRed,
    "label_item": i_label_item,
    "min_moveTo": i_min_moveTo,
    "sort": i_sort,
    "viewas": i_viewas,
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.normpath(os.path.join(here, "..", "src", "assets", "icons"))
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in ICONS.items():
        img, d = canvas()
        fn(d)
        img = img.resize((OUT, OUT), Image.LANCZOS)
        img.save(os.path.join(out_dir, f"{name}.png"))
        print(f"  {name}.png")
    print(f"共 {len(ICONS)} 个图标 → {out_dir}")


if __name__ == "__main__":
    main()
