#include "contextmenu.h"
#include "filecard.h"
#include "filegrid.h"
#include "livephoto.h"
#include "labelstore.h"
#include "iconlib.h"

#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QCoreApplication>

#include <windows.h>
#include <shellapi.h>

// ── 助手:向上查找指定类型的祖先 ──
template<typename T>
static T* findAncestor(QObject* o) {
    while (o) {
        if (auto* t = qobject_cast<T*>(o)) return t;
        o = o->parent();
    }
    return nullptr;
}

// ── 回收站删除(唯一允许的删除方式,绝不永久删除) ──
static bool deleteToRecycleBin(const QStringList& paths) {
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

// ── 系统属性对话框 ──
static void showShellProperties(const QString& path) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = L"properties";
    sei.lpFile = (const wchar_t*)path.utf16();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

// ── "打开方式"系统对话框 ──
static void openWithDialog(const QString& path) {
    QProcess::startDetached("rundll32",
        {"shell32.dll,OpenAs_RunDLL", QFileInfo(path).absoluteFilePath()});
}

FileContextMenu::FileContextMenu(FileCard* card, QWidget* parent)
    : QMenu(parent), m_filePath(card->filePath()), m_isLive(card->isLivePhoto())
{
    // 样式走 main.cpp 全局 QSS
    FileGrid* grid = findAncestor<FileGrid>(card);
    QStringList sel = grid ? grid->selectedPaths() : QStringList{ m_filePath };
    if (!sel.contains(m_filePath)) sel = QStringList{ m_filePath };

    if (m_isLive) {
        auto info = LivePhoto::detect(m_filePath);
        if (info) {
            m_liveInfo["type"] = info->type;
            m_liveInfo["video_path"] = info->videoPath;
            m_liveInfo["embedded"] = info->embedded;
            m_liveInfo["video_offset"] = info->videoOffset;
            m_liveInfo["video_length"] = info->videoLength;
        }
    }

    // ── 打开组 ──
    addAction(IconLib::appIcon("cmd_open"), "打开", this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
    });
    addAction("全屏", this, [this]() {
        // 通知主窗口:导航到该文件并进入全屏
        QWidget* w = this;
        while (w && !w->metaObject()->indexOfSlot("openFullscreen(QString)") < 0)
            w = w->parentWidget();
        // 直接用 QMetaObject 调用 MainWindow::openFullscreen
        QObject* mw = this;
        while (mw) {
            if (mw->metaObject()->indexOfSlot("openFullscreen(QString)") >= 0) break;
            mw = mw->parent();
        }
        if (mw) QMetaObject::invokeMethod(mw, "openFullscreen",
                                          Q_ARG(QString, m_filePath));
    });
    addAction(IconLib::appIcon("cmd_openWith"), "打开方式", this, [this]() {
        openWithDialog(m_filePath);
    });
    addAction("打开当前文件夹", this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(m_filePath).absolutePath()));
    });
    addAction("用资源管理器打开文件", this, [this]() {
        QProcess::startDetached("explorer", {"/select,",
            QDir::toNativeSeparators(m_filePath)});
    });
    addAction("打开全部选中文件", this, [sel]() {
        for (const auto& p : sel)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    addSeparator();

    // ── 剪贴板组 ──
    auto setClip = [sel](bool cut) {
        auto* mime = new QMimeData;
        QList<QUrl> urls;
        for (const auto& p : sel) urls << QUrl::fromLocalFile(p);
        mime->setUrls(urls);
        // Windows 资源管理器语义:Preferred DropEffect 2=移动 5=复制
        QByteArray drop(4, Qt::Uninitialized);
        DWORD effect = cut ? 2 : 5;
        memcpy(drop.data(), &effect, sizeof(DWORD));
        mime->setData("Preferred DropEffect", drop);
        QApplication::clipboard()->setMimeData(mime);
    };
    addAction(IconLib::appIcon("cmd_cut"), "剪切", this, [setClip]() { setClip(true); });
    addAction(IconLib::appIcon("cmd_copy"), "复制", this, [setClip]() { setClip(false); });
    addAction("粘贴", this, [this]() {
        const QMimeData* mime = QApplication::clipboard()->mimeData();
        if (!mime || !mime->hasUrls()) return;
        QDir target = QFileInfo(m_filePath).isDir()
            ? QDir(m_filePath) : QFileInfo(m_filePath).dir();
        for (const auto& u : mime->urls()) {
            if (!u.isLocalFile()) continue;
            QString src = u.toLocalFile();
            QString dst = target.filePath(QFileInfo(src).fileName());
            if (QFileInfo(src).isDir())
                QDir().mkpath(dst);
            else
                QFile::copy(src, dst);
        }
    });
    addSeparator();

    addAction(IconLib::appIcon("cmd_copyTo"), "复制到...", this, [sel]() {
        QString dst = QFileDialog::getExistingDirectory(
            nullptr, "复制到...", QString());
        if (dst.isEmpty()) return;
        for (const auto& p : sel) {
            QString d = dst + "/" + QFileInfo(p).fileName();
            if (QFileInfo(p).isDir()) QDir().mkpath(d);
            else QFile::copy(p, d);
        }
    });
    addAction(IconLib::appIcon("min_moveTo"), "移动到..", this, [sel, grid]() {
        QString dst = QFileDialog::getExistingDirectory(
            nullptr, "移动到...", QString());
        if (dst.isEmpty()) return;
        for (const auto& p : sel) {
            QString d = dst + "/" + QFileInfo(p).fileName();
            if (!QFile::rename(p, d))
                QMessageBox::warning(nullptr, "移动失败", p);
        }
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("cmd_delete"), "删除", this, [sel, grid]() {
        if (!deleteToRecycleBin(sel))
            QMessageBox::warning(nullptr, "删除", "删除到回收站失败");
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("cmd_rename"), "重命名...", this, [this, grid]() {
        QFileInfo fi(m_filePath);
        QString name = QInputDialog::getText(nullptr, "重命名",
            "新名称:", QLineEdit::Normal, fi.fileName());
        if (name.isEmpty() || name == fi.fileName()) return;
        QString np = fi.absolutePath() + "/" + name;
        if (!QFile::rename(m_filePath, np))
            QMessageBox::warning(nullptr, "重命名失败", m_filePath);
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("cmd_newFolder"), "新建文件夹", this, [this, grid]() {
        QString base = QFileInfo(m_filePath).isDir()
            ? m_filePath : QFileInfo(m_filePath).absolutePath();
        QString name = QInputDialog::getText(nullptr, "新建文件夹", "文件夹名:",
                                             QLineEdit::Normal, "新建文件夹");
        if (name.isEmpty()) return;
        QDir(base).mkpath(name);
        if (grid) grid->refreshCurrentDir();
    });
    addSeparator();

    addAction(IconLib::appIcon("cmd_print"), "打印...(Ctrl+P)", this, []() {
        QMessageBox::information(nullptr, "打印", "打印功能即将支持");
    });

    // ── 颜色标记子菜单 ──
    auto* labelMenu = addMenu(IconLib::appIcon("label_item"), "添加标记");
    struct { int c; QString name; } colors[] = {
        {1, "红色"}, {2, "橙色"}, {3, "黄色"}, {4, "绿色"}, {5, "蓝色"},
    };
    for (auto& c : colors) {
        QPixmap pix(12, 12);
        pix.fill(LabelStore::colorValue(c.c));
        labelMenu->addAction(QIcon(pix), c.name, this, [grid, c]() {
            if (grid) grid->applyColorLabelToSelection(c.c);
        });
    }
    labelMenu->addSeparator();
    labelMenu->addAction("取消标记", this, [grid]() {
        if (grid) grid->applyColorLabelToSelection(0);
    });

    addSeparator();
    addAction(IconLib::appIcon("cmd_batchRename"), "批量重命名...", this, []() {
        QMessageBox::information(nullptr, "批量重命名", "批量重命名功能即将支持");
    });
    addAction(IconLib::appIcon("cmd_openProperties"), "属性..", this, [this]() {
        showShellProperties(m_filePath);
    });

    // Live Photo 附加项
    if (m_isLive && !m_liveInfo.value("embedded").toBool()) {
        addSeparator();
        addAction("播放实况视频", this, [this]() {
            QString vp = m_liveInfo.value("video_path").toString();
            if (!vp.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(vp));
        });
    }
    if (m_isLive) {
        addAction("拆帧保存", this, [this]() {
            auto answer = QMessageBox::question(
                nullptr, "拆帧保存",
                "将视频拆帧保存到当前目录？",
                QMessageBox::Yes | QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                extractFrames(m_liveInfo.value("video_path").toString());
            }
        });
    }
}

void FileContextMenu::extractFrames(const QString& videoPath) {
    if (videoPath.isEmpty()) return;

    QFileInfo fi(m_filePath);
    QString outDir = fi.isDir() ? m_filePath : fi.dir().absolutePath();
    QString pattern = outDir + "/frame_%05d.png";

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("ffmpeg", {
        "-i", videoPath, "-vsync", "0", "-q:v", "2", "-y", pattern
    });

    if (proc.waitForFinished(120000)) {
        QMessageBox::information(nullptr, "完成", "帧提取完成");
    } else {
        proc.kill();
        QMessageBox::warning(nullptr, "错误", "帧提取超时");
    }
}
