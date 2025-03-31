#pragma once
// 剪贴板文件复制/剪切/粘贴 —— 右键菜单与 FileGrid Ctrl+C/X/V 共用一份实现,
// 此前这套逻辑只存在于 contextmenu.cpp 的 lambda 里,网格快捷键无法复用。
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QUrl>
#include <QString>
#include <QStringList>
#include "shelldelete.h"
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

// 把剪贴板里的本地文件粘到 target;返回是否真的有文件可粘
inline bool clipboardPasteInto(const QDir& target) {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) return false;
    bool any = false;
    for (const auto& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString src = u.toLocalFile();
        const QString dst = target.filePath(QFileInfo(src).fileName());
        if (QFileInfo(src).isDir())
            QDir().mkpath(dst);
        else
            QFile::copy(src, dst);
        any = true;
    }
    return any;
}
