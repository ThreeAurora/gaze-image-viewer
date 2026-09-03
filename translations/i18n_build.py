#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gaze 国际化构建脚本(2026-09-03)
================================
1. 扫描 src/**/*.{h,cpp} 里所有 gazeTr("...") 调用,提取源串(默认中文)。
2. 合并翻译字典(优先级:common.json 压过 batches/*.json,批内先文件序先赢)。
3. 生成 translations/gaze_en.ts(单一 context "Gaze")。
4. 调 lrelease 编译 gaze_en.qm,落到调用方给定的目标目录(默认 = exe 目录)。

用法:
    python i18n_build.py [目标目录]
常见目标:build_qt68(即 exe 目录) —— 由 CMake post-build 调用,
或直接 `python i18n_build.py` 观察统计与缺失清单。

注意:不识别非字面量参数 gazeTr(var);lupdate 也识别不了本包装函数,
译文全由本脚本的字典合并而来。
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # 仓库根
TRANS = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
TS_PATH = os.path.join(TRANS, "gaze_en.ts")


def find_code_regions(text):
    """返回 bytearray:1 = 代码区(不在字符串字面量///注释/*注释*/ 里)。"""
    n = len(text)
    code = bytearray(n)
    in_str = False
    in_line = False
    in_block = False
    i = 0
    while i < n:
        c = text[i]
        if in_line:
            if c == "\n":
                in_line = False
            i += 1
            continue
        if in_block:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        code[i] = 1
        if c == "/":
            if i + 1 < n and text[i + 1] == "/":
                in_line = True
                i += 2
                continue
            if i + 1 < n and text[i + 1] == "*":
                in_block = True
                i += 2
                continue
        if c == '"':
            in_str = True
        i += 1
    return code


# gazeTr( 参数块 = 一串相邻字符串字面量(允许跨行/空白),串内支持 C 转义
GZ = re.compile(r"gazeTr\s*\(\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+)\)", re.DOTALL)
SEG = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")


def unescape(seg):
    out = []
    i, m = 0, len(seg)
    while i < m:
        ch = seg[i]
        if ch == "\\" and i + 1 < m:
            nxt = seg[i + 1]
            if nxt == "n":
                out.append("\n")
            elif nxt == "t":
                out.append("\t")
            elif nxt == "r":
                out.append("\r")
            elif nxt == "\\":
                out.append("\\")
            elif nxt == '"':
                out.append('"')
            elif nxt == "0":
                out.append("\0")
            elif nxt == "x":
                j = i + 2
                while j < m and seg[j] in "0123456789abcdefABCDEF":
                    j += 1
                out.append(chr(int(seg[i + 2:j], 16)))
                i = j
                continue
            else:
                out.append(nxt)
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def extract_sources():
    """返回 {源串: 首次出现文件}。"""
    found = {}
    hits = 0
    for dirpath, _, files in os.walk(SRC):
        for fn in files:
            if not (fn.endswith(".cpp") or fn.endswith(".h")):
                continue
            p = os.path.join(dirpath, fn)
            try:
                with open(p, "r", encoding="utf-8-sig") as f:
                    text = f.read()
            except (UnicodeDecodeError, OSError):
                print("  [!] 跳过无法按 UTF-8 读取:", p)
                continue
            code = find_code_regions(text)
            for m in GZ.finditer(text):
                if not code[m.start()]:
                    continue  # 落在注释/字符串里,不是真实调用
                parts = SEG.findall(m.group(1))
                src = "".join(unescape(seg) for seg in parts)
                if not src:
                    continue
                hits += 1
                found.setdefault(src, os.path.relpath(p, ROOT).replace("\\", "/"))
    print("提取到 gazeTr 调用 %d 处,去重源串 %d 条" % (hits, len(found)))
    return found


XML_RE = re.compile(r"[&<>\"\']")


def xml_escape(s):
    def repl(mm):
        return {
            "&": "&amp;", "<": "&lt;", ">": "&gt;",
            '"': "&quot;", "'": "&apos;",
        }[mm.group(0)]
    return XML_RE.sub(repl, s)


def load_json_list(dirpath):
    out = {}
    if not os.path.isdir(dirpath):
        return out
    for fn in sorted(os.listdir(dirpath)):
        if not fn.endswith(".json"):
            continue
        with open(os.path.join(dirpath, fn), "r", encoding="utf-8") as f:
            try:
                d = json.load(f)
            except json.JSONDecodeError as e:
                print("  [!] JSON 解析失败 %s: %s" % (fn, e))
                continue
            for k, v in d.items():
                out.setdefault(k, v)
        print("  字典:", fn, "(%d 条)" % len(d))
    return out


def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else None
    print("== 提取源串 ==")
    sources = extract_sources()

    print("== 合并字典 ==")
    merged = {}
    for f in sorted(os.listdir(TRANS)):
        if f == "common.json":
            continue
        p = os.path.join(TRANS, f)
        if os.path.isfile(p) and f.endswith(".json"):
            print("  字典:", f)
            with open(p, "r", encoding="utf-8") as fh:
                d = json.load(fh)
            for k, v in d.items():
                merged.setdefault(k, v)
    # common.json 最后覆盖 = 最高优先级(核心词表钉死一致性)
    with open(os.path.join(TRANS, "common.json"), "r", encoding="utf-8") as fh:
        comm = json.load(fh)
    print("  字典: common.json (%d 条)" % len(comm))
    merged.update(comm)

    print("== 生成 gaze_en.ts ==")
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             "<!DOCTYPE TS>",
             '<TS version="2.1" language="en_US">',
             "<context>",
             "    <name>Gaze</name>"]
    translated = missing = 0
    for src in sorted(sources):
        en = merged.get(src)
        lines.append("    <message>")
        lines.append("        <source>%s</source>" % xml_escape(src))
        if en:
            lines.append("        <translation>%s</translation>" % xml_escape(en))
            translated += 1
        else:
            lines.append("        <translation></translation>")
            missing += 1
        lines.append("    </message>")
    lines.append("</context>")
    lines.append("</TS>")
    with open(TS_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("  共 %d 条,已译 %d,缺译 %d" % (len(sources), translated, missing))
    if missing:
        print("== 缺译清单(运行时将回退中文,待补) ==")
        for src in sorted(sources):
            if not merged.get(src):
                print("    -", repr(src), "  @", sources[src])
    if not target_dir:
        return
    qm_path = os.path.join(target_dir, "gaze_en.qm")
    lrelease = os.environ.get("GAZE_LRELEASE", "")
    if not lrelease:
        candidates = [
            r"C:/Qt",
            r"C:\Qt\6.8.3\mingw_64\bin\lrelease.exe",
        ]
        for c in candidates:
            if os.path.exists(c):
                lrelease = c
                break
    if not lrelease or not os.path.exists(lrelease):
        print("[!] 未找到 lrelease(设 GAZE_LRELEASE 环境变量指向它)")
        return 1
    print("== lrelease ->", qm_path, "==")
    r = subprocess.run([lrelease, "-silent", TS_PATH, "-qm", qm_path])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())