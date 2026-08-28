#include "contextmenu.h"
#include "filegrid.h"
#include "livephoto.h"
#include "labelstore.h"
#include "iconlib.h"
#include "shelldelete.h"

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
    auto u32 = [&](int at) -> int {
        if (big) return (u16(at) << 16) | u16(at + 2);
        return (u16(at + 2) << 16) | u16(at);
    };
    if (u16(tiff) != 42) return false;   // TIFF 魔数(两种字节序下都读成 42)
    const int ifd0 = tiff + u32(tiff + 4);
    if (ifd0 + 2 > d.size()) return false;
    const int count = u16(ifd0);
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
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfSlot("openFullscreen(QString)") < 0)
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
    addAction(IconLib::appIcon("cmd_delete"), "删除", this, [sel, grid, this]() {
        if (!deleteWithSettings(sel, this)) return;
        if (grid) grid->reloadAfterDelete(sel);
    });
    addAction(IconLib::appIcon("cmd_rename"), "重命名...", this, [this, grid]() {
        // FileOps/renameDialog:开=弹对话框(默认,既有行为);关=在卡片上就地改
        if (!AppSettings::instance().get("FileOps/renameDialog", true).toBool()) {
            if (grid) QMetaObject::invokeMethod(grid, "beginInlineRename");
            return;
        }
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
                if (!out.isNull() && f.open(QIODevice::WriteOnly)) {
                    ok = out.save(&f, nullptr, 95);
                    f.close();
                }
                if (!ok) QFile::remove(tmp);
            }

            if (!ok) return;

            // 原子替换 + 恢复创建/修改时间(元数据不因替换改变)
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
