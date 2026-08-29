#include "contextmenu.h"
#include "filegrid.h"
#include "livephoto.h"
#include "labelstore.h"
#include "iconlib.h"
#include "shelldelete.h"
#include "clipboardops.h"
#include "validname.h"
#include "validname.h"
#include "clipboardops.h"
#include "clipboardops.h"
#include "clipboardops.h"

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
#include <QStandardPaths>
#include <QImageWriter>
#include <QPointer>
#include <QTimer>
#include <QDateTime>

#include <functional>
#include <memory>

#include "logger.h"
#include <QImageWriter>
#include <QImageWriter>
#include <QImageWriter>

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

// ── "打开方式"系统对话框 ──
static void openWithDialog(const QString& path) {
    QProcess::startDetached("rundll32",
        {"shell32.dll,OpenAs_RunDLL", QFileInfo(path).absoluteFilePath()});
}

// ═══════════════════════════════════════════
// Browser/rotateExifOnly(默认开):JPEG 旋转只改写 EXIF Orientation 标签,
//   一个字节的元数据改动,不动 DCT 系数 —— 真正的零损失且瞬时完成。
//   前提:IFD0 里已有 0x0112 标签且取值是纯旋转(1/3/6/8)。
//   镜像类取向(2/4/5/7)或标签不存在 → 返回 false,调用方回落既有变换。
// ═══════════════════════════════════════════
static bool rotateJpegOrientationOnly(const QString& path, int quarterCW) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return false;
    QByteArray d = f.readAll();
    if (d.size() < 64 || d.size() > (1 << 26)) return false;   // >64MB 不冒这个险
    if (static_cast<unsigned char>(d[0]) != 0xFF
        || static_cast<unsigned char>(d[1]) != 0xD8) return false;

    // JPEG 段遍历找 APP1/Exif
    int pos = 2;
    int tiff = -1;
    while (pos + 4 <= d.size()) {
        if (static_cast<unsigned char>(d[pos]) != 0xFF) { ++pos; continue; }
        const unsigned char marker = static_cast<unsigned char>(d[pos + 1]);
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)
            || marker == 0xFF) { pos += 2; continue; }
        if (marker == 0xDA || marker == 0xD9) break;      // 进入熵编码数据
        const int len = (static_cast<unsigned char>(d[pos + 2]) << 8)
                      |  static_cast<unsigned char>(d[pos + 3]);
        if (len < 2 || pos + 2 + len > d.size()) break;
        if (marker == 0xE1 && pos + 10 <= d.size()
            && std::memcmp(d.constData() + pos + 4, "Exif\0\0", 6) == 0) {
            tiff = pos + 10;
            break;
        }
        pos += 2 + len;
    }
    if (tiff < 0 || tiff + 8 > d.size()) return false;

    // TIFF 头:字节序 + 42 + IFD0 偏移
    const bool big = d[tiff] == 'M';
    auto u16 = [&](int at) -> int {
        const auto a = static_cast<unsigned char>(d[at]);
        const auto b = static_cast<unsigned char>(d[at + 1]);
        return big ? (a << 8) | b : (b << 8) | a;
    };
    auto u32 = [&](int at) -> quint32 {
        // 必须用 quint32 组装:u16<<16 最高到 0xFFFF0000,塞进 int 会翻成负数,
        // 而负偏移能通过"只查上界"的校验 → 后面 d[ent+8] 是负下标堆越界写
        const quint32 a = static_cast<quint32>(u16(at));
        const quint32 b = static_cast<quint32>(u16(at + 2));
        return big ? ((a << 16) | b) : ((b << 16) | a);
    };
    if (u16(tiff) != 42) return false;   // TIFF 魔数(两种字节序下都读成 42)
    const quint32 ifdOff = u32(tiff + 4);
    if (ifdOff > static_cast<quint32>(d.size())) return false;
    const int ifd0 = tiff + static_cast<int>(ifdOff);
    // 下界同样要查:IFD0 必落在 8 字节 TIFF 头之后,且计数字段本身要在文件内
    if (ifd0 < tiff + 8 || ifd0 + 2 > d.size()) return false;
    const int count = u16(ifd0);
    // 每个目录项 12 字节:总数不得超出文件剩余长度(挡住伪造的巨大 count)
    if (count > (d.size() - ifd0 - 2) / 12) return false;
    for (int i = 0; i < count; ++i) {
        const int ent = ifd0 + 2 + i * 12;
        if (ent + 12 > d.size()) break;
        if (u16(ent) != 0x0112) continue;            // Orientation
        if (u16(ent + 2) != 3) return false;         // 类型必须是 SHORT
        const int cur = u16(ent + 8);                // SHORT:值左对齐占前 2 字节
        // 纯旋转取值才能只改标签;镜像类交给 jpegtran
        static const int cw[9]  = {0, 6, 0, 8, 0, 0, 3, 0, 1};   // 1→6 6→3 3→8 8→1
        static const int ccw[9] = {0, 8, 0, 6, 0, 0, 1, 0, 3};
        const int next = (cur >= 1 && cur <= 8)
                       ? (quarterCW > 0 ? cw[cur] : ccw[cur]) : 0;
        if (next == 0) return false;
        // 就地写回(小端/大端各按序存 2 字节)
        if (big) { d[ent + 8] = char(next >> 8); d[ent + 9] = char(next & 0xFF); }
        else     { d[ent + 9] = char(next >> 8); d[ent + 8] = char(next & 0xFF); }
        f.seek(0);
        return f.write(d) == d.size() && f.flush();
    }
    return false;
}

// ── jpegtran 无损变换工具查找(PATH + 常见安装位置) ──
static QString findJpegtran() {
    QString p = QStandardPaths::findExecutable("jpegtran");
    if (!p.isEmpty()) return p;
    const QStringList fallbacks = {
        "C:/miniconda3",
        "C:/Program Files/ImageMagick/jpegtran",
    };
    for (const auto& f : fallbacks)
        if (QFileInfo::exists(f)) return f;
    return {};
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
            const bool codeGood = code == 0;
            const bool fileGood = outFile.isEmpty() || QFileInfo(outFile).size() > 0;
            QString why;
            if (!codeGood)        why = QStringLiteral("exit=%1").arg(code);
            else if (!fileGood)   why = QStringLiteral("输出文件为空: %1").arg(outFile);
            finish(codeGood && fileGood, why);
        });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
        [finish, program](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;   // 读写错误由退出码兜住
            finish(false, QStringLiteral("无法启动 %1(不在 PATH?)").arg(program));
        });
    QObject::connect(timer, &QTimer::timeout, proc, [finish, proc, timeoutMs]() {
        proc->kill();
        finish(false, QStringLiteral("超过 %1 秒未完成,已终止")
                         .arg(timeoutMs / 1000.0, 0, 'g', 3));
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

FileContextMenu::FileContextMenu(FileGrid* grid, int index, QWidget* parent)
    : QMenu(parent), m_filePath(grid ? grid->pathOf(index) : QString()),
      m_isLive(false)   // 与原实现一致:卡片侧从未做过 Live Photo 检测
{
    // 样式走 main.cpp 全局 QSS
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
        // 通知主窗口:导航到该文件所在目录并进入全屏
        // 查找用 indexOfMethod:openFullscreen 是 Q_INVOKABLE 方法而非槽
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("openFullscreen(QString)") < 0)
            mw = mw->parent();
        if (mw)
            QMetaObject::invokeMethod(mw, "openFullscreen", Q_ARG(QString, m_filePath));
    });
    addAction(IconLib::appIcon("cmd_openWith"), "打开方式", this, [this]() {
        openWithDialog(m_filePath);
    });
    // 用系统默认文件管理器打开所在目录(尊重 Directory Opus 等接管:
    // ShellExecute "open" 目录会走注册的 open command,不用写死 explorer)
    addAction("在资源管理器中显示", this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(m_filePath).absolutePath()));
    });
    addAction("打开全部选中文件", this, [sel]() {
        for (const auto& p : sel)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    // 查看器标签:浏览器侧唯一"另起一张标签"的入口。没有它,标签表永远只有一张,
    // Interface/multiViewerTabs 与 oneViewerTab 两个开关就没有任何可观测差别。
    if (!QFileInfo(m_filePath).isDir()) {
        addAction("在新标签卡中打开", this, [this]() {
            QObject* mw = this;
            while (mw && mw->metaObject()->indexOfMethod("openViewerTab(QString)") < 0)
                mw = mw->parent();
            if (mw)
                QMetaObject::invokeMethod(mw, "openViewerTab", Q_ARG(QString, m_filePath));
        });
    }
    addSeparator();

    // ── 剪贴板组 ──
    // 对话框父窗口用网格:本菜单是弹出窗口,弹出结束即销毁,当父窗口会让
    // 提示跟着一起消失(删除提示因此看不见)
    addAction(IconLib::appIcon("cmd_cut"), "剪切", this, [sel]() { clipboardSetFiles(sel, true); });
    addAction(IconLib::appIcon("cmd_copy"), "复制", this, [sel]() { clipboardSetFiles(sel, false); });
    addAction("粘贴", this, [this, grid]() {
        QDir target = QFileInfo(m_filePath).isDir()
            ? QDir(m_filePath) : QFileInfo(m_filePath).dir();
        QStringList errs;
        if (!clipboardPasteInto(target, &errs)) {
            if (!errs.isEmpty())
                QMessageBox::warning(grid, "粘贴失败", errs.join(QLatin1Char('\n')));
            return;
        }
        if (!errs.isEmpty())
            QMessageBox::warning(grid, "部分项目未能粘贴", errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addSeparator();

    // 复制到... / 移动到..:交给 clipboardops 的公共实现。
    // 旧写法对文件夹只做 mkpath,一个有几万张照片的文件夹粘过去只剩空壳。
    addAction(IconLib::appIcon("cmd_copyTo"), "复制到...", this, [sel, grid]() {
        QString dst = QFileDialog::getExistingDirectory(
            grid, "复制到...", QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        if (!copyPathsTo(sel, dst, nullptr, &errs) && !errs.isEmpty())
            QMessageBox::warning(grid, "复制失败", errs.join(QLatin1Char('\n')));
        else if (!errs.isEmpty())
            QMessageBox::warning(grid, "部分项目未能复制", errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("min_moveTo"), "移动到..", this, [sel, grid]() {
        QString dst = QFileDialog::getExistingDirectory(
            grid, "移动到...", QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        if (!movePathsTo(sel, dst, nullptr, &errs) && !errs.isEmpty())
            QMessageBox::warning(grid, "移动失败", errs.join(QLatin1Char('\n')));
        else if (!errs.isEmpty())
            QMessageBox::warning(grid, "部分项目未能移动", errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("cmd_delete"), "删除  (Del)", [sel, grid]() {
        if (!deleteWithSettings(sel, grid)) return;
        if (grid) grid->reloadAfterDelete(sel);
    });
    addAction(IconLib::appIcon("cmd_rename"), "重命名...", this, [this, grid]() {
        // FileOps/renameDialog:开=弹对话框(默认,既有行为);关=在卡片上就地改
        if (!AppSettings::instance().get("FileOps/renameDialog", true).toBool()) {
            if (grid) QMetaObject::invokeMethod(grid, "beginInlineRename");
            return;
        }
        QFileInfo fi(m_filePath);
        QWidget* par = grid;   // 挂到网格:菜单一关就没了,不能当对话框父窗口
        QString name = QInputDialog::getText(par, "重命名",
            "新名称:", QLineEdit::Normal, fi.fileName()).trimmed();
        if (name.isEmpty() || name == fi.fileName()) return;
        // 分隔符进名字 = QFile::rename 把文件搬去别处,界面上一切如常。必须先挡。
        if (const QString why = invalidNameReason(name); !why.isEmpty()) {
            QMessageBox::warning(par, "重命名", why);
            return;
        }
        QString np = QDir(fi.absolutePath()).filePath(name);
        if (QFileInfo::exists(np)) {
            QMessageBox::warning(par, "重命名", QString::fromUtf8("目标名已存在:\n") + np);
            return;
        }
        if (!QFile::rename(m_filePath, np)) {
            QMessageBox::warning(par, "重命名失败", m_filePath);
            return;
        }
        if (grid) grid->setPreferPath(np);
        if (grid) grid->refreshCurrentDir();
    });
    // ── FileOps/duplicateTemplate:创建副本的命名模板 ──
    // 模板里的 # 是自增序号(从 1 起找第一个不冲突的名字)
    addAction(IconLib::appIcon("cmd_copy"), QString::fromUtf8("创建副本"), this, [this, grid]() {
        QFileInfo fi(m_filePath);
        if (!fi.isFile()) return;
        const QString base = fi.completeBaseName();
        const QString ext  = fi.suffix().isEmpty() ? QString()
                                                   : QStringLiteral(".") + fi.suffix();
        const int tpl = qBound(0, AppSettings::instance()
                            .get("FileOps/duplicateTemplate", 0).toInt(), 4);
        QString target;
        for (int n = 1; n < 10000; ++n) {
            QString name;
            switch (tpl) {
            case 0:  name = base + "-(" + QString::number(n) + ")"; break;
            case 1:  name = base + QString::fromUtf8(" - 副本 (") + QString::number(n) + ")"; break;
            case 2:  name = base + QString::fromUtf8("-副本 (") + QString::number(n) + ")"; break;
            case 3:  name = base + "-" + QString::number(n); break;
            default: name = QString::fromUtf8("副本 (") + QString::number(n) + ") - " + base; break;
            }
            const QString cand = fi.absolutePath() + "/" + name + ext;
            if (!QFileInfo::exists(cand)) { target = cand; break; }
        }
        if (target.isEmpty()) return;
        if (!QFile::copy(m_filePath, target)) {
            QMessageBox::warning(nullptr, QString::fromUtf8("创建副本失败"), target);
            return;
        }
        if (grid) { grid->setPreferPath(target); grid->refreshCurrentDir(); }
    });
    addAction(IconLib::appIcon("cmd_newFolder"), "新建文件夹", this, [this, grid]() {
        QString base = QFileInfo(m_filePath).isDir()
            ? m_filePath : QFileInfo(m_filePath).absolutePath();
        QWidget* par = grid;
        QString name = QInputDialog::getText(par, "新建文件夹", "文件夹名:",
                                             QLineEdit::Normal, "新建文件夹").trimmed();
        if (name.isEmpty()) return;
        // mkpath 会照输入把整条路径逐层建出来:"a/b" 一次冒两个目录,
        // 校验 + mkdir(单层)才是"在这里建一个文件夹"的语义
        if (const QString why = invalidNameReason(name); !why.isEmpty()) {
            QMessageBox::warning(par, "新建文件夹", why);
            return;
        }
        QString full = QDir(base).filePath(name);
        if (QFileInfo::exists(full)) {
            QMessageBox::warning(par, "新建文件夹",
                QString::fromUtf8("同名文件夹已存在:\n") + full);
            return;
        }
        if (!QDir().mkdir(full)) {
            QMessageBox::warning(par, "新建文件夹", QString::fromUtf8("创建失败:\n") + full);
            return;
        }
        if (grid) { grid->setPreferPath(full); grid->refreshCurrentDir(); }
    });
    addSeparator();

    addAction(IconLib::appIcon("cmd_print"), "打印...(Ctrl+P)", this, []() {
        QMessageBox::information(nullptr, "打印", "打印功能即将支持");
    });

    // ── 旋转/翻转(仅图片;先备份原件,保留修改时间) ──
    // 注意:IMAGE_EXTS 存带点扩展名(".jpg"),suffix() 不带点,必须手动加点匹配
    if (IMAGE_EXTS.count("." + QFileInfo(m_filePath).suffix().toLower())) {
        auto* rotMenu = addMenu(IconLib::appIcon("cmd_rotate"), QString::fromUtf8("旋转/翻转"));
        auto doRot = [this, grid](int mode) {
            QFileInfo fi(m_filePath);
            QDateTime mod  = fi.lastModified();          // 原修改时间
            QDateTime birth = fi.birthTime();            // 原创建时间
            QString backup = fi.absolutePath() + "/" + fi.completeBaseName()
                           + "_original." + fi.suffix();
            if (!QFileInfo::exists(backup))
                QFile::copy(m_filePath, backup);         // 首次生成备份原件

            QString tmp = m_filePath + ".gaze_rot_tmp";
            bool ok = false;

            // ── JPEG:jpegtran DCT 级无损变换,-copy all 保留全部元数据(EXIF/XMP/ICC) ──
            if (ext == "jpg" || ext == "jpeg") {
                QString jt = findJpegtran();
                if (!jt.isEmpty()) {
                    QStringList args{"-copy", "all"};
                    switch (mode) {
                    case 0: args << "-rotate" << "270"; break;   // 左旋90°(逆时针)
                    case 1: args << "-rotate" << "90";  break;   // 右旋90°(顺时针)
                    case 2: args << "-flip" << "horizontal"; break;
                    case 3: args << "-flip" << "vertical"; break;
                    }
                    args << m_filePath;
                    QProcess proc;
                    proc.setStandardOutputFile(tmp);     // 变换结果重定向临时文件
                    proc.start(jt, args);
                    ok = proc.waitForFinished(20000) && proc.exitCode() == 0
                         && QFileInfo(tmp).size() > 0;
                    if (!ok) QFile::remove(tmp);
                }
            }

            // ── 非 JPEG 或 jpegtran 不可用:QImage 重编码 ──
            // (PNG/BMP 像素无损;JPEG 回退为 95 有损 + EXIF 无法保留,仅兜底)
            if (!ok) {
                QImage img(m_filePath);
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
                if (canWrite && f.open(QIODevice::WriteOnly)) {
                    ok = out.save(&f, fmt.constData(), 95);
                    f.close();
                }
                if (!ok) QFile::remove(tmp);
            }

            if (!ok) {
                QMessageBox::warning(nullptr, QString::fromUtf8("旋转/翻转"),
                    QString::fromUtf8("无法完成该变换(解码或写回失败):\n") + m_filePath);
                return;
            }

            // 原子替换 + 恢复创建/修改时间(元数据不因替换改变)
            // 走到这里结果已经落盘成功,才允许留原件备份
            makeBackup();
            if (!QFile::rename(tmp, m_filePath)) {
                QFile::remove(tmp);
                QMessageBox::warning(nullptr, QString::fromUtf8("旋转/翻转"),
                    QString::fromUtf8("写回文件失败:\n") + m_filePath);
                return;
            }
            QFile tf(m_filePath);
            if (tf.open(QIODevice::ReadOnly)) {
                tf.setFileTime(mod, QFileDevice::FileModificationTime);
                if (birth.isValid())
                    tf.setFileTime(birth, QFileDevice::FileBirthTime);
            }
            if (grid) grid->refreshCurrentDir();
        };
        rotMenu->addAction(IconLib::appIcon("cmd_rotate90"),
            QString::fromUtf8("左旋 90°"), this, [doRot]() { doRot(0); });
        rotMenu->addAction(IconLib::appIcon("cmd_rotate270"),
            QString::fromUtf8("右旋 90°"), this, [doRot]() { doRot(1); });
        rotMenu->addAction(IconLib::appIcon("cmd_horizontalFlip"),
            QString::fromUtf8("水平翻转"), this, [doRot]() { doRot(2); });
        rotMenu->addAction(IconLib::appIcon("cmd_verticalFlip"),
            QString::fromUtf8("垂直翻转"), this, [doRot]() { doRot(3); });
    }

    // ── ★ 标记(内存工作集,与下面的颜色标记是两套) ──
    addAction(QString::fromUtf8("\xe2\x98\x85 标记 / 取消标记  (M)"), this, [grid]() {
        if (grid) grid->toggleMarkOnSelection();
    });

    // ── 颜色标记子菜单 ──
    auto* labelMenu = addMenu(IconLib::appIcon("label_item"), "添加颜色标记");
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
    labelMenu->addAction("取消颜色标记", this, [grid]() {
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

    // 整段视频拆帧可能几十秒。以前 GUI 线程 waitForFinished(120s) 死等,界面
    // 整个钉死那么久;而且判据只看"进程结束了没",ffmpeg 不在 PATH(启动失败)
    // 同样算结束 → 弹一句"帧提取完成"谎报成功。改成后台跑 + 认退出码。
    runProcessAsync("ffmpeg",
        { "-i", videoPath, "-vsync", "0", "-q:v", "2", "-y", pattern },
        QString(), 120000,
        [outDir](bool ok, const QString& why) {
            if (ok)
                QMessageBox::information(nullptr, "完成",
                    QString::fromUtf8("帧提取完成:\n") + outDir);
            else
                QMessageBox::warning(nullptr, "帧提取失败", why + "\n" + outDir);
        });
}
