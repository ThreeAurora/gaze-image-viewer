#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""拉取 Lucide 图标(MIT)SVG,统一为白色单色,供 iconrender 渲染成 96px PNG。
Lucide 是现代线性图标集(圆润描边,无版权可商用),语义直观看图即懂。
来源:https://unpkg.com/lucide-static@latest/icons/<name>.svg (MIT)
产物:cache/tmp/lucide_svg_white/  → iconrender → cache/tmp/lucide_png_white/
用法: python tools/fetch_lucide_icons.py
"""
import os, re, sys, urllib.request
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
BASE = "https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/"
OUT = os.path.normpath(os.path.join(ROOT, "cache", "tmp", "lucide_svg_white"))

MAP = {
    "up":                    "chevron-up",
    "cmd_open":              "folder-open",
    "cmd_browse":            "history",
    "cmd_copy":              "copy",
    "cmd_copyPath":          "link",
    "cmd_copyTo":            "files",
    "cmd_crop":              "crop",
    "cmd_cut":               "scissors",
    "cmd_delete":            "trash-2",
    "cmd_editMetadata":      "pencil",
    "cmd_fileNext":          "chevron-right",
    "cmd_filePrevious":      "chevron-left",
    "cmd_filter":            "filter",
    "cmd_fullscreen":        "maximize-2",
    "cmd_horizontalFlip":    "flip-horizontal-2",
    "cmd_newFolder":         "folder-plus",
    "cmd_openProperties":    "info",
    "cmd_openWith":          "external-link",
    "cmd_options":           "settings",
    "cmd_paneIcons":         "layout-grid",
    "cmd_paneThumbs":        "images",
    "cmd_paste":             "clipboard",
    "cmd_print":             "printer",
    "cmd_refresh":           "refresh-cw",
    "cmd_rename":            "square-pen",
    "cmd_rotate":            "rotate-cw",
    "cmd_rotate90":          "rotate-cw",
    "cmd_rotate270":         "rotate-ccw",
    "cmd_search":            "search",
    "cmd_selectAllFile":     "check-square",
    "cmd_showFilesInFolder": "folder-search",
    "cmd_showRed":           "eye",
    "cmd_verticalFlip":      "flip-vertical-2",
    "label_item":            "tag",
    "min_moveTo":            "folder-input",
    "sort":                  "arrow-up-down",
    "viewas":                "list",
    "spin_up":               "chevron-up",
    "spin_down":             "chevron-down",
}


def main():
    os.makedirs(OUT, exist_ok=True)
    ok, fail = 0, []

    def work(item):
        gaze, name = item
        dst = os.path.join(OUT, gaze + ".svg")
        if os.path.exists(dst):
            return gaze, "已缓存"
        url = BASE + name + ".svg"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Gaze-icon-fetch"})
            t = urllib.request.urlopen(req, timeout=20).read().decode("utf-8")
            if "<svg" not in t or "404" in t[:64]:
                raise RuntimeError("bad payload")
            # Lucide:stroke=currentColor + fill=none → 把 currentColor 换白色,fill 去掉
            t = t.replace("currentColor", "#FFFFFF")
            # 确保整体描边白色(个别 svg 用 stroke= 直接带色)
            t = re.sub(r'stroke="#[0-9a-fA-F]{3,8}"', 'stroke="#FFFFFF"', t)
            with open(dst, "w", encoding="utf-8") as f:
                f.write(t)
            return gaze, "ok"
        except Exception as e:
            return gaze, "!! %s" % e

    with ThreadPoolExecutor(max_workers=10) as ex:
        for gaze, msg in ex.map(work, MAP.items()):
            print("  %-22s %s" % (gaze, msg), flush=True)
            if msg == "ok":
                ok += 1
            elif msg != "已缓存":
                fail.append(gaze)
    print("完成:%d 个新下载, %d 个失败%s" % (ok, len(fail), (":" + ",".join(fail)) if fail else ""))


if __name__ == "__main__":
    main()