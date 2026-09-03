#pragma once
// 回收站删除(Windows Shell,FOF_ALLOWUNDO)—— 右键菜单与 FileGrid 键盘删除共用,
// 保证两条入口行为一致(此前 FileGrid 走 QFile::moveToTrash,右键走 Shell,行为分裂)
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QWidget>
#include <QMessageBox>
#include <QLabel>
#include <QTimer>
#include <QStatusBar>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include "settings.h"
#include "constants.h"
#include "i18n.h"
#include "filelockrelease.h"   // #214:动文件前放掉预览握着的句柄

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

inline bool shellDelete(const QStringList& paths, bool toRecycleBin,
                        QWidget* parent = nullptr) {
    if (paths.isEmpty()) return true;
    // 双 NUL 结尾的多字符串列表。SHFileOperationW 对"相对路径 + FOF_ALLOWUNDO"
    // 会静默变成永久删除 —— 一律转绝对路径,堵死这条通道。
    QString list;
    for (const auto& p : paths) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        list += (QFileInfo(p).isRelative() ? abs : QDir::cleanPath(p)) + QChar(L'\0');
    }
    list += QChar(L'\0');

    auto* buf = new wchar_t[list.size()];
    memcpy(buf, list.constData(), list.size() * sizeof(wchar_t));

    SHFILEOPSTRUCTW op = {};
    op.hwnd = parent ? reinterpret_cast<HWND>(parent->winId()) : NULL;
    op.wFunc = FO_DELETE;
    op.pFrom = buf;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    if (toRecycleBin) {
        op.fFlags |= FOF_ALLOWUNDO;
        // 文件进不了回收站时(超出回收站容量/该盘禁用了回收站/网络盘),
        // 默认会"静默永久删除" —— 这正是绝不能发生的。WANTNUKEWARNING 强制
        // 弹出"太大无法进回收站,是否永久删除"的确认,把不可逆操作交给用户。
        op.fFlags |= FOF_WANTNUKEWARNING;
    }
    int rc = SHFileOperationW(&op);
    delete[] buf;
    return rc == 0 && !op.fAnyOperationsAborted;
}

inline bool deleteToRecycleBin(const QStringList& paths, QWidget* parent = nullptr) {
    return shellDelete(paths, true, parent);
}

// 删除后的左下角浮动提示:自绘在窗口上,浮在状态栏之上,鼠标穿透不挡卡片。
// toastMs 默认 1500 —— 用户提的 300ms 不足以读完一句中文,故按可读时长兜底,可在设置调。
inline void showDeleteToast(QWidget* parent, const QString& text) {
    AppSettings& st = AppSettings::instance();
    if (!st.get("FileOps/deleteToast", true).toBool()) return;
    QWidget* win = parent ? parent->window() : nullptr;
    if (!win) return;
    const int ms = qMax(400, st.get("FileOps/toastMs", 1500).toInt());

    auto* toast = new QLabel(text, win);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast->setTextFormat(Qt::PlainText);
    // 琥珀底+深字加粗:原灰底白字在深色界面里太不显眼(用户反馈)
    toast->setStyleSheet(QString::fromUtf8(
        "QLabel{background:%1;color:%2;font-size:12px;"
        "font-weight:600;padding:8px 14px;border-radius:6px;"
        "border:1px solid rgba(0,0,0,70);}")
        .arg(C_SELECT_YELLOW, C_WIN_BG));
    toast->adjustSize();

    int bottomInset = 0;
    if (auto* sb = win->findChild<QStatusBar*>()) bottomInset = sb->height();
    toast->move(12, win->height() - bottomInset - toast->height() - 10);

    auto* eff = new QGraphicsOpacityEffect(toast);
    eff->setOpacity(1.0);
    toast->setGraphicsEffect(eff);
    auto* fade = new QPropertyAnimation(eff, "opacity", toast);
    fade->setDuration(220);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    // 注意:不要在这里 close()(WA_DeleteOnClose 会在 fade 自己的信号里删掉父对象)
    QObject::connect(fade, &QPropertyAnimation::finished,
                     toast, [toast]() { toast->deleteLater(); });
    QTimer::singleShot(ms, toast, [fade]() { fade->start(); });
    toast->show();
    toast->raise();
}

// 统一删除入口:FileOps/useRecycleBin + FileOps/confirmDelete + FileOps/confirmDeleteDirs
// 在此生效。三处删除(网格键盘/右键菜单/查看器)都走这里,保证行为一致。
// 不进回收站=不可恢复,这种情况强制确认,忽略 confirmDelete。
// 含文件夹时 confirmDeleteDirs(默认开)同样强制确认:一次带走整棵子树。
// 返回 false = 用户取消或删除失败(失败已弹提示),调用方据此什么都不做。
inline bool deleteWithSettings(const QStringList& paths, QWidget* parent) {
    if (paths.isEmpty()) return true;
    AppSettings& st = AppSettings::instance();
    const bool toRecycle = st.get("FileOps/useRecycleBin", true).toBool();
    bool confirm = toRecycle ? st.get("FileOps/confirmDelete", true).toBool() : true;

    int dirCount = 0;
    for (const auto& p : paths) if (QFileInfo(p).isDir()) ++dirCount;
    if (dirCount > 0 && st.get("FileOps/confirmDeleteDirs", true).toBool())
        confirm = true;

    if (confirm) {
        const QString what = paths.size() == 1
            ? QFileInfo(paths.first()).fileName()
            : QString::number(paths.size()) + QStringLiteral(" 个项目");
        QString text;
        if (paths.size() == 1 && dirCount == 1)
            text = gazeTr("你确认要删除文件夹吗？\n");
        else if (dirCount > 0)
            text = gazeTr("确定删除 %1（含 %2 个文件夹）？\n").arg(what).arg(dirCount);
        else
            text = toRecycle
                ? gazeTr("确定将 %1 移至回收站？\n").arg(what)
                : gazeTr("确定永久删除 %1？此操作不可恢复！\n").arg(what);
        text += toRecycle
            ? gazeTr("可从回收站恢复。")
            : gazeTr("此操作不可恢复！");
        const QString title = (paths.size() == 1 && dirCount == 1)
            ? gazeTr("删除文件夹")
            : (toRecycle ? gazeTr("删除") : gazeTr("永久删除"));
        if (QMessageBox::question(parent, title,
                text + "\n" + paths.first(),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return false;
    }
    // #214:确认之后、动手之前放句柄——播放中的视频被 QMediaPlayer(WMF) 锁着,
    // 不放的话 Shell 删除会失败。四条删除入口(网格键盘/右键/树/查看器)全走这里
    releaseGazeFileLocks(paths);
    if (shellDelete(paths, toRecycle, parent)) {
        // 短文案:不带文件名/数量,一个词说完落点(回收站/永久保留区分)
        showDeleteToast(parent,
            toRecycle ? gazeTr("已移至回收站")
                      : gazeTr("已永久删除"));
        return true;
    }
    QMessageBox::warning(parent, gazeTr("删除"),
        gazeTr("删除失败:\n") + paths.first());
    return false;
}

// 系统属性对话框(Shell 自带的那份,含安全/以前的版本等页)。
// 右键菜单与文件夹树共用,原先是 contextmenu.cpp 里的 file-static,树拿不到。
inline void showShellProperties(const QString& path) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = L"properties";
    sei.lpFile = reinterpret_cast<LPCWSTR>(path.utf16());
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}
