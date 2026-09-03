#pragma once
// 剪贴板文件复制/剪切/粘贴 —— 右键菜单与 FileGrid Ctrl+C/X/V 共用一份实现,
// 此前这套逻辑只存在于 contextmenu.cpp 的 lambda 里,网格快捷键无法复用。
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QUrl>
#include <QString>
#include <QStringList>
#include "filelockrelease.h"   // #214:移动前放掉预览握着的句柄
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline void clipboardSetFiles(const QStringList& paths, bool cut) {
    if (paths.isEmpty()) return;
    auto* mime = new QMimeData;
    QList<QUrl> urls;
    for (const auto& p : paths) urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);
    // Windows 资源管理器语义:Preferred DropEffect 2=移动 5=复制
    QByteArray drop(4, Qt::Uninitialized);
    DWORD effect = cut ? 2 : 5;
    memcpy(drop.data(), &effect, sizeof(DWORD));
    mime->setData("Preferred DropEffect", drop);
    QApplication::clipboard()->setMimeData(mime);
}

// 剪贴板里是否有可粘贴的本地文件(右键菜单据此灰掉"粘贴")
inline bool clipboardHasFiles() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) return false;
    for (const auto& u : mime->urls())
        if (u.isLocalFile()) return true;
    return false;
}

// 剪贴板标记的是"剪切"(移动)而非"复制"。资源管理器写 2/5,
// 判定位而非等值:5 = COPY|LINK 不含 MOVE 位。
inline bool clipboardPrefersMove() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime) return false;
    const QByteArray d = mime->data("Preferred DropEffect");
    if (d.size() < int(sizeof(DWORD))) return false;
    DWORD effect = 0;
    memcpy(&effect, d.constData(), sizeof(DWORD));
    return (effect & DROPEFFECT_MOVE) != 0;
}

// ── 目标名冲突:沿用资源管理器命名 "名字 (2)"、"名字 (3)"… ──
inline QString uniqueDest(const QString& dir, const QString& name) {
    const QString base = dir + QLatin1Char('/') + name;
    if (!QFileInfo::exists(base)) return base;
    // 分割点:文件夹整体当"名",文件拆 基本名/扩展名(含多段后缀时只切最后一段)
    QString stem, suffix;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) { stem = name.left(dot); suffix = name.mid(dot); }
    else stem = name;
    for (int n = 2; n < 10000; ++n) {
        const QString cand = dir + QLatin1Char('/')
            + stem + QStringLiteral(" (%1)").arg(n) + suffix;
        if (!QFileInfo::exists(cand)) return cand;
    }
    return base;   // 万一同名一万次仍冲突(几乎不可能):交调用方报错
}

// ── 递归复制目录树 ──
// 旧实现是 QDir().mkpath(dst):整个文件夹粘过去只剩一个空壳,内容全丢。
// 链接/junction 只复制其本身不跟随 —— 跟随会把环套进递归里。
inline bool copyTree(const QString& src, const QString& dst) {
    if (!QDir().mkpath(dst)) return false;
    bool ok = true;
    QDir sd(src);
    const QFileInfoList list = sd.entryInfoList(
        QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
        QDir::Unsorted);
    for (const QFileInfo& fi : list) {
        const QString d = QDir(dst).filePath(fi.fileName());
        if (fi.isSymLink()) {
            if (!QFile::copy(fi.absoluteFilePath(), d)) ok = false;
        } else if (fi.isDir()) {
            if (!copyTree(fi.absoluteFilePath(), d)) ok = false;
        } else if (!QFile::copy(fi.absoluteFilePath(), d)) {
            ok = false;
        }
    }
    return ok;
}

// dst 落在 src 内部时复制/移动都是自我嵌套(Windows 会给出"不能复制进自身")
inline bool isInside(const QString& child, const QString& ancestor) {
    const QString c = QDir::cleanPath(child), a = QDir::cleanPath(ancestor);
    return c == a || c.startsWith(a + QLatin1Char('/'));
}

// 把一批路径复制到 dstDir;done=成功数,errors=逐条失败原因
inline bool copyPathsTo(const QStringList& paths, const QString& dstDir,
                        int* doneOut = nullptr, QStringList* errors = nullptr) {
    int done = 0;
    for (const auto& p : paths) {
        const QFileInfo fi(p);
        if (!fi.exists()) {
            if (errors) *errors << QStringLiteral("%1:源已不存在").arg(p);
            continue;
        }
        if (isInside(dstDir, fi.absoluteFilePath())) {
            if (errors) *errors << QStringLiteral("%1:不能复制进自己的子目录").arg(fi.fileName());
            continue;
        }
        const QString dst = uniqueDest(dstDir, fi.fileName());
        const bool ok = fi.isDir() ? copyTree(p, dst) : QFile::copy(p, dst);
        if (ok) ++done;
        else if (errors) *errors << QStringLiteral("%1:复制失败").arg(fi.fileName());
    }
    if (doneOut) *doneOut = done;
    return done > 0;
}

// 移动到 dstDir:同卷直接 rename,跨卷回落"复制 + 删除源"
// (源仅在目的已复制成功时才清除,不会两头都不剩)
inline bool movePathsTo(const QStringList& paths, const QString& dstDir,
                        int* doneOut = nullptr, QStringList* errors = nullptr) {
    // #214:跨卷回落是"复制+删源",源被预览播放锁着时删除会失败,动手前统一放句柄
    releaseGazeFileLocks(paths);
    int done = 0;
    for (const auto& p : paths) {
        const QFileInfo fi(p);
        if (!fi.exists()) {
            if (errors) *errors << QStringLiteral("%1:源已不存在").arg(p);
            continue;
        }
        if (isInside(dstDir, fi.absoluteFilePath())) {
            if (errors) *errors << QStringLiteral("%1:不能移动到子目录中").arg(fi.fileName());
            continue;
        }
        const QString dst = uniqueDest(dstDir, fi.fileName());
        if (QFile::rename(p, dst)) { ++done; continue; }
        // 跨卷/权限差异 → rename 失败,复制一份再把源清掉
        const bool copied = fi.isDir() ? copyTree(p, dst) : QFile::copy(p, dst);
        if (!copied) {
            if (errors) *errors << QStringLiteral("%1:移动失败").arg(fi.fileName());
            continue;
        }
        const bool removed = fi.isDir() ? QDir(p).removeRecursively()
                                       : QFile::remove(p);
        if (!removed && errors)
            *errors << QStringLiteral("%1:已复制到目标,但源未能删除(留下副本)").arg(fi.fileName());
        ++done;
    }
    if (doneOut) *doneOut = done;
    return done > 0;
}

// 把剪贴板里的本地文件粘到 target;返回是否真的有文件可粘。
// 剪贴板标记为"剪切"时执行移动(旧实现无视标记,剪切与复制行为相同)。
inline bool clipboardPasteInto(const QDir& target, QStringList* errors = nullptr) {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) return false;
    QStringList srcs;
    for (const auto& u : mime->urls())
        if (u.isLocalFile() && !u.toLocalFile().isEmpty()) srcs << u.toLocalFile();
    if (srcs.isEmpty()) return false;
    const QString dst = target.absolutePath();
    int done = 0;
    if (clipboardPrefersMove()) movePathsTo(srcs, dst, &done, errors);
    else                        copyPathsTo(srcs, dst, &done, errors);
    return done > 0;
}
