#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""拉取 Material Design Icons(Apache 2.0)SVG 并按功能组着色,供 Qt 渲染成 48px 图标。

来源:https://github.com/Templarian/MaterialDesign (Apache License 2.0,可商用可分发,
无需署名)。本地已带该仓库的图标译名映射表(Gaze 功能名 → MDI 名),脚本只做三件事:
  1) 按映射表逐个下载 SVG 到 cache/tmp/mdi_svg/
  2) 把图标统一着色(功能组配色,仿原 XnView 彩色风格)
  3) 输出着色后的 SVG 到 cache/tmp/mdi_svg_colored/ 供渲染工具批量转 PNG

用法: python tools/fetch_mdi_icons.py [--refresh]
"""
import io, os, re, sys, urllib.request

BASE = "https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SVG_DIR = os.path.normpath(os.path.join(ROOT, "cache", "tmp", "mdi_svg"))
COLORED_DIR = os.path.normpath(os.path.join(ROOT, "cache", "tmp", "mdi_svg_colored"))

# Gaze 图标名 → MDI 文件名(主名,备选名...)。主名 404 时依次尝试备选。
MAP = {
    "up":                    ["arrow-up", "chevron-up"],
    "cmd_open":              ["folder-open"],
    "cmd_browse":            ["folder-clock-outline", "history"],
    "cmd_copy":              ["content-copy"],
    "cmd_copyPath":          ["link-variant", "content-copy"],
    "cmd_copyTo":            ["folder-copy", "content-copy"],
    "cmd_crop":              ["crop"],
    "cmd_cut":               ["content-cut"],
    "cmd_delete":            ["trash-can-outline", "delete"],
    "cmd_editMetadata":      ["file-document-edit-outline", "pencil-outline"],
    "cmd_fileNext":          ["chevron-right"],
    "cmd_filePrevious":      ["chevron-left"],
    "cmd_filter":            ["filter-variant", "filter"],
    "cmd_fullscreen":        ["fullscreen"],
    "cmd_horizontalFlip":    ["flip-horizontal"],
    "cmd_newFolder":         ["folder-plus"],
    "cmd_openProperties":    ["information-outline", "file-document-outline"],
    "cmd_openWith":          ["open-in-app", "application"],
    "cmd_options":           ["cog"],
    "cmd_paneIcons":         ["view-grid"],
    "cmd_paneThumbs":        ["image-multiple-outline", "view-grid-outline"],
    "cmd_paste":             ["content-paste"],
    "cmd_print":             ["printer"],
    "cmd_refresh":           ["refresh"],
    "cmd_rename":            ["square-edit-outline", "pencil-outline"],
    "cmd_rotate":            ["rotate-right"],
    "cmd_rotate90":          ["rotate-90-degrees-ccw", "format-rotate-90"],
    "cmd_rotate270":         ["rotate-90-degrees-cw", "rotate-left"],
    "cmd_search":            ["magnify"],
    "cmd_selectAllFile":     ["select-all", "checkbox-multiple-marked-outline"],
    "cmd_showFilesInFolder": ["folder-search-outline", "folder-multiple-outline"],
    "cmd_showRed":           ["contrast-circle", "eye-outline"],
    "cmd_verticalFlip":      ["flip-vertical"],
    "label_item":            ["tag"],
    "min_moveTo":            ["folder-move"],
    "sort":                  ["sort", "sort-variant"],
    "viewas":                ["view-list-outline", "view-column-outline"],
}

# 功能组配色(Material 500 系,明亮醒目,深/浅主题通用)
COLORS = {
    "folder": "#F5A623",   # 打开/浏览/进入文件夹:琥珀金(同原 XnView 文件夹色)
    "nav":    "#78909C",   # 后退/前进/向上:蓝灰
    "copy":   "#1976D2",   # 复制/剪切/粘贴/复制路径/移动到:蓝
    "edit":   "#5E35B1",   # 重命名/属性/元数据编辑:紫
    "danger": "#E53935",   # 删除:亮红(原 #D32F2F 偏暗,48px 下发灰)
    "create": "#43A047",   # 新建文件夹:绿
    "view":   "#3949AB",   # 视图模式/面板/缩略图/全屏:靛蓝
    "image":  "#7CB342",   # 旋转/翻转/裁剪:浅绿
    "tool":   "#00897B",   # 选项/搜索/过滤/排序/刷新:青
    "print":  "#607D8B",   # 打印:蓝灰
    "label":  "#F9A825",   # 颜色标记/标签:黄
    "red":    "#E53935",   # 红通道示波:红
}

NAME_COLOR = {  # 个别图标单独指定(默认按组)
    "cmd_rotate": "image", "cmd_rotate90": "image", "cmd_rotate270": "image",
    "cmd_horizontalFlip": "image", "cmd_verticalFlip": "image", "cmd_crop": "image",
    "cmd_open": "folder", "cmd_browse": "folder", "cmd_openWith": "folder",
    "cmd_showFilesInFolder": "folder", "cmd_newFolder": "create",
    "cmd_delete": "danger", "label_item": "label", "cmd_showRed": "red",
    "cmd_print": "print", "up": "nav", "cmd_fileNext": "nav", "cmd_filePrevious": "nav",
    "cmd_copy": "copy", "cmd_copyPath": "copy", "cmd_copyTo": "copy",
    "cmd_cut": "copy", "cmd_paste": "copy", "min_moveTo": "copy",
    "cmd_rename": "edit", "cmd_openProperties": "edit", "cmd_editMetadata": "edit",
    "cmd_fullscreen": "view", "cmd_paneIcons": "view", "cmd_paneThumbs": "view",
    "viewas": "view", "cmd_options": "tool", "cmd_search": "tool",
    "cmd_filter": "tool", "sort": "tool", "cmd_refresh": "tool",
    "cmd_selectAllFile": "view",
}

def group(name):
    return NAME_COLOR.get(name, "tool")

def color_for(name):
    from collections import OrderedDict
    return COLORS[group(name)]

def fetch_svg(mdi_name):
    url = BASE + mdi_name + ".svg"
    req = urllib.request.Request(url, headers={"User-Agent": "Gaze-icon-fetch"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read().decode("utf-8")

def colorize(svg, color):
    # MDI 官方图标 path 用 fill="currentColor" 或默认黑色:统一替换为目标色
    svg = svg.replace("currentColor", color)
    # 无显式 fill 的 path(极少数)补上
    svg = re.sub(r"<path(?![^>]*\bfill=)", "<path fill='%s'" % color, svg)
    return svg

def main():
    refresh = "--refresh" in sys.argv
    os.makedirs(SVG_DIR, exist_ok=True)
    os.makedirs(COLORED_DIR, exist_ok=True)
    ok, miss = 0, []
    for gaze, candidates in MAP.items():
        src_path = os.path.join(SVG_DIR, gaze + ".svg")
        if not refresh and os.path.exists(src_path):
            ok += 1
            print("  %-20s 已缓存" % gaze)
            continue
        text = None
        for cand in candidates:
            try:
                text = fetch_svg(cand)
                print("  %-20s <- %s" % (gaze, cand))
                break
            except Exception:
                continue
        if text is None:
            miss.append(gaze)
            print("  %-20s !! 404,无可用替代" % gaze)
            continue
        with open(src_path, "w", encoding="utf-8") as f:
            f.write(text)
        with open(os.path.join(COLORED_DIR, gaze + ".svg"), "w", encoding="utf-8") as f:
            f.write(colorize(text, color_for(gaze)))
        ok += 1
    print("完成:%d 个成功, %d 个缺失%s" % (ok, len(miss), ("(" + ",".join(miss) + ")") if miss else ""))

if __name__ == "__main__":
    main()