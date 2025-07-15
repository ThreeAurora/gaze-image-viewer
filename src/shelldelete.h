#pragma once
// 回收站删除(Windows Shell,FOF_ALLOWUNDO)—— 右键菜单与 FileGrid 键盘删除共用,
// 保证两条入口行为一致(此前 FileGrid 走 QFile::moveToTrash,右键走 Shell,行为分裂)
#include <QString>
#include <QStringList>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

inline bool deleteToRecycleBin(const QStringList& paths) {
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
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    int rc = SHFileOperationW(&op);
    delete[] buf;
    return rc == 0 && !op.fAnyOperationsAborted;
}
