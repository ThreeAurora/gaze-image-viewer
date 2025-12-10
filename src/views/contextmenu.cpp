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

// ═══════════════════════════════════════════
// 外部进程异步跑(#70):jpegtran / ffmpeg 以前都在 GUI 线程里 waitForFinished,
//   无损旋转 20s、拆帧 120s —— 进程只要慢一点(大图解码、杀软拦一道),整个应用
//   就冻那么久,日志看门狗还会把它记成"卡死"。这里改成 start + finished 回调。
//   · outFile 非空 → stdout 重定向到它(jpegtran 靠 stdout 出图)
//   · ok 的判据:退出码 0,且(给了 outFile 时)那个文件非空。
//     光看 waitForFinished 的返回值不行:进程根本没启动成功时它也是"已结束",
//     旧的拆帧代码因此在 ffmpeg 不在 PATH 时弹一句"帧提取完成"谎报成功。
//   · 超时 kill 并回报。回调最多一次(kill 之后 finished 还会再发一发信号)。
//   · 回调不碰 this:FileContextMenu 每次弹窗现建、关掉即析构,异步续上去就是 UAF。
//     需要网格的调用方自己带 QPointer。
// ═══════════════════════════════════════════
static void runProcessAsync(const QString& program, const QStringList& args,
                            const QString& outFile, int timeoutMs,
                            std::function<void(bool ok, QString why)> done) {
    auto* proc = new QProcess();
    hideConsoleWindow(*proc);   // jpegtran/ffmpeg 异步变换静默起,不闪控制台窗
    auto* timer = new QTimer(proc);            // 子对象,跟着进程一起回收
    timer->setSingleShot(true);
    timer->setInterval(timeoutMs);
    if (!outFile.isEmpty()) proc->setStandardOutputFile(outFile);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    auto once = std::make_shared<bool>(false);
    auto finish = [once, proc, timer, done, program](bool ok, QString why) {
        if (*once) return;
        *once = true;
        timer->stop();
        proc->deleteLater();
        Logger::event(QStringLiteral("proc-async: '%1' %2 %3")
                          .arg(program, ok ? QStringLiteral("ok") : QStringLiteral("fail"), why));
        if (done) done(ok, std::move(why));
    };
    QObject::connect(proc, &QProcess::finished, proc,
        [finish, outFile](int code, QProcess::ExitStatus) {
            const bool good = code == 0
                && (outFile.isEmpty() || QFileInfo(outFile).size() > 0);
            finish(good, good ? QString() : QStringLiteral("exit=%1").arg(code));
        });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
        [finish, program](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;   // 读写错误由退出码兜住
            finish(false, QStringLiteral("无法启动 %1(不在 PATH?)").arg(program));
        });
    QObject::connect(timer, &QTimer::timeout, proc, [finish, proc, timeoutMs]() {
        proc->kill();
        finish(false, QStringLiteral("超过 %1 秒未完成,已终止").arg(timeoutMs / 1000));
    });
    proc->start(program, args);
    timer->start();
}

// ── FileOps/losslessBackup:动文件之前留一份原件 ──
// 只在"确实要动文件"的那一刻备份:此前链路可能整个失败,提前备份会留下没用的
// _original 孤儿文件(#59 的教训)
static void backupOriginal(const QString& path) {
    if (!AppSettings::instance().get("FileOps/losslessBackup", true).toBool()) return;
    QFileInfo fi(path);
    const QString backup = fi.absolutePath() + "/" + fi.completeBaseName()
                         + "_original." + fi.suffix();
    if (!QFileInfo::exists(backup)) QFile::copy(path, backup);
}

// 内容换了但修改/创建时间按原值还回去(排序、"最近修改"筛选都不该被旋转打乱)
static void restoreFileTimes(const QString& path, const QDateTime& mod, const QDateTime& birth) {
    QFile tf(path);
    if (!tf.open(QIODevice::ReadOnly)) return;
    tf.setFileTime(mod, QFileDevice::FileModificationTime);
    if (birth.isValid()) tf.setFileTime(birth, QFileDevice::FileBirthTime);
}

// ── 非 JPEG 或 jpegtran 不可用/失败:QImage 重编码到 tmp。true=结果可用 ──
// (PNG/BMP 像素无损;JPEG 回退为 95 有损 + EXIF 无法保留,仅兜底)
static bool reencodeRotate(const QString& path, const QString& tmp,
                           int mode, const QString& rotExt) {
    QImage img(path);
    QImage out;
    QTransform t;
    if (!img.isNull()) {
        switch (mode) {
        case 0: t.rotate(-90); out = img.transformed(t, Qt::SmoothTransformation); break;
        case 1: t.rotate(90);  out = img.transformed(t, Qt::SmoothTransformation); break;
        case 2: out = img.mirrored(true, false); break;
        case 3: out = img.mirrored(false, true); break;
        }
    }
    QFile f(tmp);
    // 必须显式给格式:save(device, nullptr) 会让 Qt 拿 device 的文件名
    // 后缀猜格式,而临时文件后缀是 "gaze_rot_tmp",无任何 handler 匹配
    // → 保存恒失败 → 非 JPEG 的旋转/翻转成了静默空操作。
    QByteArray fmt = rotExt.toUtf8();
    if (fmt == "jpg") fmt = "jpeg";
    else if (fmt == "tif") fmt = "tiff";
    const bool canWrite = !out.isNull()
        && QImageWriter::supportedImageFormats().contains(fmt);
    bool ok = false;
    if (canWrite && f.open(QIODevice::WriteOnly)) {
        ok = out.save(&f, fmt.constData(), 95);
        f.close();
    }
    if (!ok) QFile::remove(tmp);
    return ok;
}

static void rotateFailedMsg(const QString& path) {
    QMessageBox::warning(nullptr, QString::fromUtf8("旋转/翻转"),
        QString::fromUtf8("无法完成该变换(解码或写回失败):\n") + path);
}

// 结果已经在 tmp 落盘:备份原件 → 原子替换 → 恢复时间戳 → 让网格重读
static void commitRotateResult(const QString& path, const QString& tmp,
                               const QDateTime& mod, const QDateTime& birth,
                               QPointer<FileGrid> grid) {
    backupOriginal(path);
    if (!QFile::rename(tmp, path)) {
        QFile::remove(tmp);
        QMessageBox::warning(nullptr, QString::fromUtf8("旋转/翻转"),
            QString::fromUtf8("写回文件失败:\n") + path);
        return;
    }
    restoreFileTimes(path, mod, birth);
    if (grid) grid->refreshCurrentDir();
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

    // ── 旋转/翻转(仅图片;JPEG 走 jpegtran 无损变换) ──
    // 注意:IMAGE_EXTS 存带点扩展名(".jpg"),suffix() 不带点,必须手动加点匹配
    if (IMAGE_EXTS.count("." + QFileInfo(m_filePath).suffix().toLower())) {
        const QString rotExt = QFileInfo(m_filePath).suffix().toLower();
        const bool lossless = (rotExt == "jpg" || rotExt == "jpeg")
                              && !findJpegtran().isEmpty();
        const QString lossTag = lossless ? QString::fromUtf8("(无损)") : QString();
        auto* rotMenu = addMenu(IconLib::appIcon("cmd_rotate"), QString::fromUtf8("旋转/翻转"));
        auto doRot = [this, grid](int mode) {
            QImage img(m_filePath);
            if (img.isNull()) return;
            QImage out;
            QTransform t;
            switch (mode) {
            case 0: t.rotate(-90); out = img.transformed(t, Qt::SmoothTransformation); break;
            case 1: t.rotate(90);  out = img.transformed(t, Qt::SmoothTransformation); break;
            case 2: out = img.mirrored(true, false); break;
            case 3: out = img.mirrored(false, true); break;
            }
            if (out.isNull()) return;
            QFileInfo fi(m_filePath);
            QString backup = fi.absolutePath() + "/" + fi.completeBaseName()
                           + "_original." + fi.suffix();
            if (!QFileInfo::exists(backup))
                QFile::copy(m_filePath, backup);          // 首次生成备份原件
            QDateTime mod = fi.lastModified();            // 记录原修改时间
            // 先写临时文件再原子替换:直接 open(WriteOnly) 会截断原文件,
            // 若 save 编码失败(如 tif/webp 无编码器)原图将损坏
            QString tmp = m_filePath + ".gaze_rot_tmp";
            QFile f(tmp);
            if (!f.open(QIODevice::WriteOnly)) return;
            bool ok = out.save(&f, nullptr, 95);
            f.close();
            if (!ok) { QFile::remove(tmp); return; }
            if (!QFile::rename(tmp, m_filePath)) {        // Windows MoveFileEx 覆盖替换
                QFile::remove(tmp);
                QMessageBox::warning(nullptr, QString::fromUtf8("旋转/翻转"),
                    QString::fromUtf8("写回文件失败:\n") + m_filePath);
                return;
            }
            QFile tf(m_filePath);                          // 保留原修改时间(元数据不变)
            if (tf.open(QIODevice::ReadOnly))
                tf.setFileTime(mod, QFileDevice::FileModificationTime);
            if (grid) grid->refreshCurrentDir();
        };
        rotMenu->addAction(IconLib::appIcon("cmd_rotate90"),
            QString::fromUtf8("左旋 90°") + lossTag, this, [doRot]() { doRot(0); });
        rotMenu->addAction(IconLib::appIcon("cmd_rotate270"),
            QString::fromUtf8("右旋 90°") + lossTag, this, [doRot]() { doRot(1); });
        rotMenu->addAction(IconLib::appIcon("cmd_horizontalFlip"),
            QString::fromUtf8("水平翻转") + lossTag, this, [doRot]() { doRot(2); });
        rotMenu->addAction(IconLib::appIcon("cmd_verticalFlip"),
            QString::fromUtf8("垂直翻转") + lossTag, this, [doRot]() { doRot(3); });
    }

    // ── ★ 标记(内存工作集,与下面的颜色标记是两套) ──
    addAction(QString::fromUtf8("\xe2\x98\x85 标记 / 取消标记  (M)"), this, [grid]() {
        if (grid) grid->toggleMarkOnSelection();
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
