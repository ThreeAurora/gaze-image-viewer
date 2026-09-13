#include "contextmenu.h"
#include "filegrid.h"
#include "livephoto.h"
#include "labelstore.h"
#include "iconlib.h"
#include "shelldelete.h"
#include "clipboardops.h"
#include "printdialog.h"
#include "cropdialog.h"
#include "settings.h"
#include "validname.h"
#include "toolpath.h"
#include "i18n.h"

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

// ── jpegtran 无损变换工具查找 ──
// 收口到 toolpath.h 的 locateJpegtran():exe 旁 jpegtran/ → PATH → 各盘
// conda/ImageMagick 常见安装位置。公开仓不写死盘符(旧候选里 "C:/miniconda3"
// 还是目录不是 exe,存在性检查为真却根本不是可执行文件)。
static QString findJpegtran() {
    return locateJpegtran();
}

// ═══════════════════════════════════════════
// 外部进程异步跑(#70):jpegtran / ffmpeg 以前都在 GUI 线程里 waitForFinished,
//   无损旋转 20s、拆帧 120s —— 进程只要慢一点(大图解码、杀软拦一道),整个应用
//   就冻那么久,日志看门狗还会把它记成"卡死"。这里改成 start + finished 回调。
//   · outFile 非空 → stdout 重定向到它(jpegtran 靠 stdout 出图)
//   · ok 的判据:退出码 0,且(给了 outFile 时)那个文件非空。
//     旧代码只问"等完了没有"、从不看退出码:ffmpeg 真跑起来但失败(编码错、目标
//     不可写)照样弹"帧提取完成"。实测(Qt 6.5.3/Windows,见 cache/tmp/proc_async_test.cpp)
//     反向的坑也有:程序根本不在 PATH 时 waitForFinished 返回的是 **false**,
//     所以旧代码在那个场景下弹的是"帧提取超时" —— 谎报的是原因,不是结果。
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
            const bool codeGood = code == 0;
            const bool fileGood = outFile.isEmpty() || QFileInfo(outFile).size() > 0;
            QString why;
            if (!codeGood)        why = QStringLiteral("exit=%1").arg(code);
            else if (!fileGood)   why = gazeTr("输出文件为空: %1").arg(outFile);
            finish(codeGood && fileGood, why);
        });
    QObject::connect(proc, &QProcess::errorOccurred, proc,
        [finish, program](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;   // 读写错误由退出码兜住
            finish(false, gazeTr("无法启动 %1(不在 PATH?)").arg(program));
        });
    QObject::connect(timer, &QTimer::timeout, proc, [finish, proc, timeoutMs]() {
        proc->kill();
        finish(false, gazeTr("超过 %1 秒未完成,已终止")
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
    // Windows 的 SetFileTime 要求句柄具写属性访问权;只读打开必得
    // ERROR_ACCESS_DENIED,还原静默失效 —— 必须以读写方式开
    QFile tf(path);
    if (!tf.open(QIODevice::ReadWrite)) return;
    const bool modOk = tf.setFileTime(mod, QFileDevice::FileModificationTime);
    const bool birthOk = !birth.isValid()
        || tf.setFileTime(birth, QFileDevice::FileBirthTime);
    if (!modOk || !birthOk)
        Logger::event(QStringLiteral("restoreFileTimes failed on %1").arg(path));
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
    // 后缀猜格式,而临时文件后缀是 "Gaze_rot_tmp",无任何 handler 匹配
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
    QMessageBox::warning(nullptr, gazeTr("旋转/翻转"),
        gazeTr("无法完成该变换(解码或写回失败):\n") + path);
}

// 结果已经在 tmp 落盘:备份原件 → 原子替换 → 恢复时间戳 → 让网格重读
static void commitRotateResult(const QString& path, const QString& tmp,
                               const QDateTime& mod, const QDateTime& birth,
                               QPointer<FileGrid> grid) {
    backupOriginal(path);
    if (!QFile::rename(tmp, path)) {
        QFile::remove(tmp);
        QMessageBox::warning(nullptr, gazeTr("旋转/翻转"),
            gazeTr("写回文件失败:\n") + path);
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
    addAction(IconLib::appIcon("cmd_open"), gazeTr("打开"), this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
    });
    addAction(gazeTr("全屏"), this, [this]() {
        // 通知主窗口:导航到该文件所在目录并进入全屏
        // 查找用 indexOfMethod:openFullscreen 是 Q_INVOKABLE 方法而非槽
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("openFullscreen(QString)") < 0)
            mw = mw->parent();
        if (mw)
            QMetaObject::invokeMethod(mw, "openFullscreen", Q_ARG(QString, m_filePath));
    });
    addAction(IconLib::appIcon("cmd_openWith"), gazeTr("打开方式"), this, [this]() {
        openWithDialog(m_filePath);
    });
    // 用系统默认文件管理器打开所在目录(尊重 Directory Opus 等接管:
    // ShellExecute "open" 目录会走注册的 open command,不用写死 explorer)
    addAction(gazeTr("在资源管理器中显示"), this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(m_filePath).absolutePath()));
    });
    addAction(gazeTr("打开全部选中文件"), this, [sel]() {
        for (const auto& p : sel)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    // 查看器标签:浏览器侧唯一"另起一张标签"的入口。没有它,标签表永远只有一张,
    // Interface/multiViewerTabs 与 oneViewerTab 两个开关就没有任何可观测差别。
    if (!QFileInfo(m_filePath).isDir()) {
        addAction(gazeTr("在新标签卡中打开"), this, [this]() {
            QObject* mw = this;
            while (mw && mw->metaObject()->indexOfMethod("openViewerTab(QString)") < 0)
                mw = mw->parent();
            if (mw)
                QMetaObject::invokeMethod(mw, "openViewerTab", Q_ARG(QString, m_filePath));
        });
    }
    // #243:收藏夹入口 —— 整个选择集一起收(目录/文件都行)。主窗口
    // Q_INVOKABLE addFavorite 经元调用进来,不引主窗口头文件
    {
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("addFavorite(QString)") < 0)
            mw = mw->parent();
        if (mw)
            addAction(gazeTr("添加到收藏夹"), this, [mw, sel]() {
                for (const auto& p : sel)
                    QMetaObject::invokeMethod(mw, "addFavorite", Q_ARG(QString, p));
            });
    }
    addSeparator();

    // ── 剪贴板组 ──
    // 对话框父窗口用网格:本菜单是弹出窗口,弹出结束即销毁,当父窗口会让
    // 提示跟着一起消失(删除提示因此看不见)
    addAction(IconLib::appIcon("cmd_cut"), gazeTr("剪切"), this, [sel]() { clipboardSetFiles(sel, true); });
    addAction(IconLib::appIcon("cmd_copy"), gazeTr("复制"), this, [sel]() { clipboardSetFiles(sel, false); });
    addAction(gazeTr("粘贴"), this, [this, grid]() {
        QDir target = QFileInfo(m_filePath).isDir()
            ? QDir(m_filePath) : QFileInfo(m_filePath).dir();
        QStringList errs;
        if (!clipboardPasteInto(target, &errs)) {
            if (!errs.isEmpty())
                QMessageBox::warning(grid, gazeTr("粘贴失败"), errs.join(QLatin1Char('\n')));
            return;
        }
        if (!errs.isEmpty())
            QMessageBox::warning(grid, gazeTr("部分项目未能粘贴"), errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addSeparator();

    // 复制到... / 移动到..:交给 clipboardops 的公共实现。
    // 旧写法对文件夹只做 mkpath,一个有几万张照片的文件夹粘过去只剩空壳。
    addAction(IconLib::appIcon("cmd_copyTo"), gazeTr("复制到..."), this, [sel, grid]() {
        QString dst = QFileDialog::getExistingDirectory(
            grid, gazeTr("复制到..."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        if (!copyPathsTo(sel, dst, nullptr, &errs) && !errs.isEmpty())
            QMessageBox::warning(grid, gazeTr("复制失败"), errs.join(QLatin1Char('\n')));
        else if (!errs.isEmpty())
            QMessageBox::warning(grid, gazeTr("部分项目未能复制"), errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("min_moveTo"), gazeTr("移动到.."), this, [sel, grid]() {
        QString dst = QFileDialog::getExistingDirectory(
            grid, gazeTr("移动到..."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        if (!movePathsTo(sel, dst, nullptr, &errs) && !errs.isEmpty())
            QMessageBox::warning(grid, gazeTr("移动失败"), errs.join(QLatin1Char('\n')));
        else if (!errs.isEmpty())
            QMessageBox::warning(grid, gazeTr("部分项目未能移动"), errs.join(QLatin1Char('\n')));
        if (grid) grid->refreshCurrentDir();
    });
    addAction(IconLib::appIcon("cmd_delete"), gazeTr("删除  (Del)"), [sel, grid]() {
        if (!deleteWithSettings(sel, grid)) return;
        if (grid) grid->reloadAfterDelete(sel);
    });
    addAction(IconLib::appIcon("cmd_rename"), gazeTr("重命名..."), this, [this, grid]() {
        // FileOps/renameDialog:开=弹对话框(默认,既有行为);关=在卡片上就地改
        if (!AppSettings::instance().get("FileOps/renameDialog", true).toBool()) {
            if (grid) grid->beginInlineRename();   // 普通方法,元调用够不着(见 #89)
            return;
        }
        QFileInfo fi(m_filePath);
        QWidget* par = grid;   // 挂到网格:菜单一关就没了,不能当对话框父窗口
        QString name = QInputDialog::getText(par, gazeTr("重命名"),
            gazeTr("新名称:"), QLineEdit::Normal, fi.fileName()).trimmed();
        if (name.isEmpty() || name == fi.fileName()) return;
        // 分隔符进名字 = QFile::rename 把文件搬去别处,界面上一切如常。必须先挡。
        if (const QString why = invalidNameReason(name); !why.isEmpty()) {
            QMessageBox::warning(par, gazeTr("重命名"), why);
            return;
        }
        QString np = QDir(fi.absolutePath()).filePath(name);
        if (QFileInfo::exists(np)) {
            QMessageBox::warning(par, "重命名", gazeTr("目标名已存在:\n") + np);
            return;
        }
        // #214:改名目标若正被预览播放,句柄不放 rename 会失败
        releaseGazeFileLocks({m_filePath});
        if (!renameWithRetry(m_filePath, np)) {
            QMessageBox::warning(par, gazeTr("重命名失败"), m_filePath);
            return;
        }
        if (grid) grid->setPreferPath(np);
        if (grid) grid->refreshCurrentDir();
    });
    // ── FileOps/duplicateTemplate:创建副本的命名模板 ──
    // 模板里的 # 是自增序号(从 1 起找第一个不冲突的名字);命名规则与
    // 裁剪"先建副本"共用 duplicateTargetFor(clipboardops.h)
    addAction(IconLib::appIcon("cmd_copy"), gazeTr("创建副本"), this, [this, grid]() {
        QFileInfo fi(m_filePath);
        if (!fi.isFile()) return;
        const QString target = duplicateTargetFor(m_filePath);
        if (target.isEmpty()) return;
        if (!QFile::copy(m_filePath, target)) {
            QMessageBox::warning(nullptr, gazeTr("创建副本失败"), target);
            return;
        }
        if (grid) { grid->setPreferPath(target); grid->refreshCurrentDir(); }
    });
    addAction(IconLib::appIcon("cmd_newFolder"), gazeTr("新建文件夹"), this, [this, grid]() {
        QString base = QFileInfo(m_filePath).isDir()
            ? m_filePath : QFileInfo(m_filePath).absolutePath();
        QWidget* par = grid;
        QString name = QInputDialog::getText(par, gazeTr("新建文件夹"), gazeTr("文件夹名:"),
                                             QLineEdit::Normal, gazeTr("新建文件夹")).trimmed();
        if (name.isEmpty()) return;
        // mkpath 会照输入把整条路径逐层建出来:"a/b" 一次冒两个目录,
        // 校验 + mkdir(单层)才是"在这里建一个文件夹"的语义
        if (const QString why = invalidNameReason(name); !why.isEmpty()) {
            QMessageBox::warning(par, gazeTr("新建文件夹"), why);
            return;
        }
        QString full = QDir(base).filePath(name);
        if (QFileInfo::exists(full)) {
            QMessageBox::warning(par, gazeTr("新建文件夹"),
                gazeTr("同名文件夹已存在:\n") + full);
            return;
        }
        if (!QDir().mkdir(full)) {
            QMessageBox::warning(par, gazeTr("新建文件夹"), gazeTr("创建失败:\n") + full);
            return;
        }
        if (grid) { grid->setPreferPath(full); grid->refreshCurrentDir(); }
    });
    addSeparator();

    addAction(IconLib::appIcon("cmd_print"), gazeTr("打印...(Ctrl+P)"), this, [grid, sel]() {
        PrintDialog::printImages(grid, sel);
    });

    // ── 旋转/翻转(仅图片;JPEG 走 jpegtran 无损变换) ──
    // 注意:IMAGE_EXTS 存带点扩展名(".jpg"),suffix() 不带点,必须手动加点匹配
    if (IMAGE_EXTS.count("." + QFileInfo(m_filePath).suffix().toLower())) {
        const QString rotExt = QFileInfo(m_filePath).suffix().toLower();
        const bool allowLossless = AppSettings::instance()
            .get("Browser/losslessRotate", true).toBool();
        const bool lossless = allowLossless
                              && (rotExt == "jpg" || rotExt == "jpeg")
                              && !findJpegtran().isEmpty();
        const QString lossTag = lossless ? gazeTr("(无损)") : QString();
        auto* rotMenu = addMenu(IconLib::appIcon("cmd_rotate"), gazeTr("旋转/翻转"));
        auto doRot = [this, grid, lossless, rotExt](int mode) {
            AppSettings& st = AppSettings::instance();
            // 副本:无损那步改到后台跑,续命回调里不能再碰 this(菜单关掉就析构了)
            const QString path = m_filePath;
            QFileInfo fi(path);
            QDateTime mod  = fi.lastModified();          // 原修改时间
            QDateTime birth = fi.birthTime();            // 原创建时间
            // 每次变换独占一个临时名:两连点转同一文件时不再共用一个 tmp 互相踩
            static int seq = 0;
            const QString tmp = path + QStringLiteral(".Gaze_rot_tmp%1").arg(++seq);

            // ── Browser/rotateExifOnly(默认开):JPEG 先试"只改 EXIF 方向" ──
            // 只动一个元数据字节,pixel 数据完全不动,零损失且瞬时;
            // 失败(无 Orientation 标签 / 镜像类取向)再走下面的变换链路
            if (st.get("Browser/rotateExifOnly", true).toBool()
                && (rotExt == "jpg" || rotExt == "jpeg")) {
                int quarter = 0;
                if (mode == 0) quarter = -1;        // 左旋 90°
                else if (mode == 1) quarter = 1;    // 右旋 90°
                if (quarter != 0 && rotateJpegOrientationOnly(path, quarter)) {
                    // 此分支是就地改写,备份必须补上(时间戳仍按原值恢复)
                    backupOriginal(path);
                    restoreFileTimes(path, mod, birth);
                    if (grid) grid->refreshCurrentDir();
                    return;
                }
            }

            // 收尾两条腿,无损/兜底共用(结果已在 tmp,或干脆没结果)
            auto commit = [path, tmp, mod, birth, g = QPointer<FileGrid>(grid)]() {
                commitRotateResult(path, tmp, mod, birth, g);
            };
            auto reencodeThenCommit = [path, tmp, mod, birth, g = QPointer<FileGrid>(grid),
                                       mode, rotExt]() {
                QFile::remove(tmp);           // 清掉无损留下的半成品
                if (!reencodeRotate(path, tmp, mode, rotExt)) { rotateFailedMsg(path); return; }
                commitRotateResult(path, tmp, mod, birth, g);
            };

            // ── JPEG:jpegtran DCT 级无损变换(Browser/losslessRotate 关时直接走重编码) ──
            // ── -copy all 保留全部元数据(EXIF/XMP/ICC);FileOps/losslessKeepMeta=关 则丢弃 ──
            // 交后台跑:原来是 GUI 线程 waitForFinished(20s),jpegtran 一慢界面就整个钉死
            if (lossless) {
                const QString jt = findJpegtran();
                if (!jt.isEmpty()) {
                    QStringList args = st.get("FileOps/losslessKeepMeta", true).toBool()
                        ? QStringList{"-copy", "all"} : QStringList{"-copy", "none"};
                    switch (mode) {
                    case 0: args << "-rotate" << "270"; break;   // 左旋90°(逆时针)
                    case 1: args << "-rotate" << "90";  break;   // 右旋90°(顺时针)
                    case 2: args << "-flip" << "horizontal"; break;
                    case 3: args << "-flip" << "vertical"; break;
                    }
                    args << path;
                    // tmp 由 QProcess 重定向 stdout 写;失败/超时回落重编码
                    runProcessAsync(jt, args, tmp, 20000,
                        [commit, reencodeThenCommit](bool ok, const QString&) {
                            if (ok) commit(); else reencodeThenCommit();
                        });
                    return;
                }
            }

            // ── 非 JPEG、无损开关关、或 jpegtran 找不到:当场重编码 ──
            reencodeThenCommit();
        };
        rotMenu->addAction(IconLib::appIcon("cmd_rotate90"),
            gazeTr("左旋 90°") + lossTag, this, [doRot]() { doRot(0); });
        rotMenu->addAction(IconLib::appIcon("cmd_rotate270"),
            gazeTr("右旋 90°") + lossTag, this, [doRot]() { doRot(1); });
        rotMenu->addAction(IconLib::appIcon("cmd_horizontalFlip"),
            gazeTr("水平翻转") + lossTag, this, [doRot]() { doRot(2); });
        rotMenu->addAction(IconLib::appIcon("cmd_verticalFlip"),
            gazeTr("垂直翻转") + lossTag, this, [doRot]() { doRot(3); });

        // ── #83 无损裁剪 ──
        // JPEG:jpegtran -crop WxH+X+Y -perfect -copy all(选区已吸附到 16px MCU,
        //      -perfect 保证做不到无损就直接失败,不偷偷降质)
        // 其它格式:QImage::copy 后按原格式保存(PNG 无损;其余如实说明会重编码)
        addAction(IconLib::appIcon("cmd_crop"), gazeTr("裁剪...(无损)"),
                  this, [this, grid]() {
            CropDialog dlg(m_filePath, nullptr);
            if (dlg.exec() != QDialog::Accepted) return;

            // 2026-09-05 用户令:裁剪绝不写回原文件 —— 先按"重复文件命名"设置
            // 建好副本,再对副本执行裁剪(此前顺序反了:直接改写原件,原件被
            // 占用/只读时就报"写回文件失败",原件还险些被覆盖)。
            const QRect r = dlg.cropRect();
            QFileInfo fi(m_filePath);
            const QString target = duplicateTargetFor(m_filePath);
            if (target.isEmpty()) {
                QMessageBox::warning(nullptr, gazeTr("裁剪失败"),
                                     gazeTr("无法为副本取名(重名过多):\n") + m_filePath);
                return;
            }
            if (!QFile::copy(m_filePath, target)) {
                QMessageBox::warning(nullptr, gazeTr("裁剪失败"),
                                     gazeTr("创建副本失败:\n") + target);
                return;
            }

            const QFileInfo tfi(target);
            const QString ext = tfi.suffix().toLower();
            QDateTime mod = tfi.lastModified(), birth = tfi.birthTime();
            const QString tmp = target + ".Gaze_crop_tmp";

            // 收尾段不碰菜单对象(FileContextMenu 关掉即析构,异步续上去就是
            // UAF):需要网格自己带 QPointer
            QPointer<FileGrid> gridP(grid);
            const QString src = m_filePath;
            auto finalize = [src, target, tmp, mod, birth, gridP](bool ok) {
                if (!ok) {
                    // 裁剪没成:刚建的空壳副本一并删掉,如实报错(原件分毫未动)
                    QFile::remove(tmp);
                    QFile::remove(target);
                    QMessageBox::warning(nullptr, gazeTr("裁剪失败"),
                        gazeTr("无法无损完成该裁剪:\n%1\n\n"
                                      "可换个选区(建议选区再大一点、离边缘远一点)重试。")
                            .arg(src));
                    return;
                }
                if (!QFile::rename(tmp, target)) {
                    QFile::remove(tmp);
                    QFile::remove(target);
                    QMessageBox::warning(nullptr, gazeTr("裁剪失败"),
                                         gazeTr("写回副本失败:\n") + target);
                    return;
                }
                // 时间戳照旧保留(与原件一致),与旋转同一规矩
                restoreFileTimes(target, mod, birth);
                if (gridP) { gridP->setPreferPath(target); gridP->refreshCurrentDir(); }
            };
            // 非 JPEG 或 jpegtran 拒绝(-perfect 失败):QImage 重编码兜底
            auto reencodeFallback = [target, tmp, ext, r, finalize]() {
                bool ok = false;
                QImage img(target);
                if (!img.isNull()) {
                    QImage out = img.copy(r);
                    QFile f(tmp);
                    if (!out.isNull() && f.open(QIODevice::WriteOnly))
                        ok = out.save(&f, ext.isEmpty() ? nullptr : ext.toLatin1().constData(),
                                      ext == "png" ? -1 : 95);
                    f.close();
                }
                finalize(ok);
            };

            if ((ext == "jpg" || ext == "jpeg") && !findJpegtran().isEmpty()) {
                const QString spec = QStringLiteral("%1x%2+%3+%4")
                                         .arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
                QStringList args = AppSettings::instance()
                        .get("FileOps/losslessKeepMeta", true).toBool()
                    ? QStringList{"-copy", "all"} : QStringList{"-copy", "none"};
                args << "-crop" << spec << "-perfect" << target;
                // jpegtran 异步跑(#70 同款):大图/杀软拦截时整窗冻结 20 秒的教训
                runProcessAsync(findJpegtran(), args, tmp, 20000,
                                [finalize, reencodeFallback](bool ok, const QString&) {
                    if (ok) finalize(true);
                    else reencodeFallback();   // jpegtran 拒绝 → 重编码兜底
                });
                return;
            }

            reencodeFallback();   // 非 JPEG(或 jpegtran 缺失):当场重编码
        });
    }

    // ── 颜色标记子菜单 ──
    auto* labelMenu = addMenu(IconLib::appIcon("label_item"), gazeTr("添加颜色标记"));
    struct { int c; QString name; } colors[] = {
        {1, gazeTr("红色")}, {2, gazeTr("橙色")}, {3, gazeTr("黄色")}, {4, gazeTr("绿色")}, {5, gazeTr("蓝色")},
    };
    for (auto& c : colors) {
        QPixmap pix(12, 12);
        pix.fill(LabelStore::colorValue(c.c));
        labelMenu->addAction(QIcon(pix), c.name, this, [grid, c]() {
            if (grid) grid->applyColorLabelToSelection(c.c);
        });
    }
    labelMenu->addSeparator();
    labelMenu->addAction(gazeTr("取消颜色标记"), this, [grid]() {
        if (grid) grid->applyColorLabelToSelection(0);
    });

    addSeparator();
    // #123:原「批量重命名...」占位项已删 —— 批量重命名在 TODO_ALL §9 否决清单(@153611),
    // 菜单里挂一个"即将支持"的入口等于承诺用户永远不会来的功能。
    addAction(IconLib::appIcon("cmd_openProperties"), gazeTr("属性.."), this, [this]() {
        showShellProperties(m_filePath);
    });

    // Live Photo 附加项
    if (m_isLive && !m_liveInfo.value("embedded").toBool()) {
        addSeparator();
        addAction(gazeTr("播放实况视频"), this, [this]() {
            QString vp = m_liveInfo.value("video_path").toString();
            if (!vp.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(vp));
        });
    }
    if (m_isLive) {
        addAction(gazeTr("拆帧保存"), this, [this]() {
            auto answer = QMessageBox::question(
                nullptr, gazeTr("拆帧保存"),
                gazeTr("将视频拆帧保存到当前目录？"),
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
    // 整个钉死那么久;而且判据是"这次等待返回 true 就算成功"、从不看退出码,
    // ffmpeg 跑起来但失败照样弹"帧提取完成"。(ffmpeg 不在 PATH 时等待返回 false,
    // 旧代码因此弹"帧提取超时" —— 原因写错了,实测见 cache/tmp/proc_async_test.cpp)
    // 改成后台跑 + 认退出码;工具定位走 #113 同一份 vendor/ffmpeg 优先。
    const QString ff = locateFfmpegTool(QStringLiteral("ffmpeg"));
    if (ff.isEmpty()) {
        QMessageBox::warning(nullptr, gazeTr("帧提取失败"),
            gazeTr("ffmpeg 未找到(exe旁 ffmpeg/ 与 PATH 均无)。\n") + outDir);
        return;
    }
    runProcessAsync(ff,
        { "-i", videoPath, "-vsync", "0", "-q:v", "2", "-y", pattern },
        QString(), 120000,
        [outDir](bool ok, const QString& why) {
            if (ok)
                QMessageBox::information(nullptr, gazeTr("完成"),
                    gazeTr("帧提取完成:\n") + outDir);
            else
                QMessageBox::warning(nullptr, gazeTr("帧提取失败"), why + "\n" + outDir);
        });
}
