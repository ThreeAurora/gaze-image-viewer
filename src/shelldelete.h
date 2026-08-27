#pragma once
// 回收站删除(Windows Shell,FOF_ALLOWUNDO)—— 右键菜单与 FileGrid 键盘删除共用,
// 保证两条入口行为一致(此前 FileGrid 走 QFile::moveToTrash,右键走 Shell,行为分裂)
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QWidget>
#include <QMessageBox>
#include "settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

inline bool shellDelete(const QStringList& paths, bool toRecycleBin) {
    if (paths.isEmpty()) return true;
    // 双 NUL 结尾的多字符串列表
    QString list;
    for (const auto& p : paths) list += p + QChar(L'\0');
    list += QChar(L'\0');

    auto* buf = new wchar_t[list.size()];
    memcpy(buf, list.constData(), list.size() * sizeof(wchar_t));

    SHFILEOPSTRUCTW op = {};
    op.hwnd = NULL;
    op.wFunc = FO_DELETE;
    op.pFrom = buf;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    if (toRecycleBin) op.fFlags |= FOF_ALLOWUNDO;
    int rc = SHFileOperationW(&op);
    delete[] buf;
    return rc == 0 && !op.fAnyOperationsAborted;
}

inline bool deleteToRecycleBin(const QStringList& paths) {
    return shellDelete(paths, true);
}

// 统一删除入口:FileOps/useRecycleBin + FileOps/confirmDelete 在此生效。
// 三处删除(网格键盘/右键菜单/查看器)都走这里,保证行为一致。
// 不进回收站=不可恢复,这种情况强制确认,忽略 confirmDelete。
inline bool deleteWithSettings(const QStringList& paths, QWidget* parent) {
    if (paths.isEmpty()) return true;
    AppSettings& st = AppSettings::instance();
    const bool toRecycle = st.get("FileOps/useRecycleBin", true).toBool();
    const bool confirm = toRecycle ? st.get("FileOps/confirmDelete", true).toBool() : true;

    if (confirm) {
        const QString what = paths.size() == 1
            ? QFileInfo(paths.first()).fileName()
            : QString::number(paths.size()) + QStringLiteral(" 个项目");
        const QString text = toRecycle
            ? QString::fromUtf8("确定将 %1 移至回收站？\n").arg(what)
            : QString::fromUtf8("确定永久删除 %1？此操作不可恢复！\n").arg(what);
        if (QMessageBox::question(parent,
                toRecycle ? QString::fromUtf8("删除") : QString::fromUtf8("永久删除"),
                text + paths.first(),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return false;
    }
    return shellDelete(paths, toRecycle);
}
