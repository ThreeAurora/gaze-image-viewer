#pragma once
// ═══════════════════════════════════════════
// PDF 预览渲染(#82)
//   Qt 6.8 的 Windows 二进制不带 QtPdf 模块,故用 Ghostscript 把指定页渲染成
//   PNG,再当普通图片显示。
//   GS 随 gaze 分发(#110,用户 2026-08-31 明令):优先用 exe 旁 gs/bin/gswin64c.exe
//   (源 = 仓库 vendor/gs,CMake post-build 自动同步到构建目录),
//   内置版缺失才回退 PATH / Program Files —— 不再依赖"用户装没装 GS"。
//   都找不到时回退 Windows Shell 缩略图(Explorer 那种首页缩略图),
//   还失败就如实返回空图,绝不假装预览成功。
// ═══════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QImage>
#include <QSettings>
#include <QCoreApplication>
#include "toolpath.h"

namespace Pdf {

// Ghostscript 可执行文件:内置(gs/bin,随 exe 分发)优先 → PATH → Program Files/gs*
inline QString gsExe() {
    static QString cached;
    static bool looked = false;
    if (looked) return cached;
    looked = true;

    QString p;
    // 1) 随 gaze 分发的内置版。gswin64c.exe 靠相对自身的 ../Resource 找 gs_init.ps,
    //    所以必须整目录(gs/bin + gs/Resource + gs/lib)一起拷,只拷 exe 会直接报错。
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& name : {QStringLiteral("gswin64c.exe"),
                                QStringLiteral("gswin32c.exe"),
                                QStringLiteral("gs.exe")}) {
        const QString cand = appDir + QStringLiteral("/gs/bin/") + name;
        if (QFileInfo::exists(cand)) { p = QDir::toNativeSeparators(cand); break; }
    }
    if (!p.isEmpty()) { cached = p; return cached; }

    // 2) 回退:PATH 里的 gswin64c / gswin32c / gs
    auto probe = [](const QString& x) {
        return !x.isEmpty() && QFileInfo::exists(x) ? x : QString();
    };
    p = probe(QStandardPaths::findExecutable(QStringLiteral("gswin64c")));
    if (p.isEmpty()) p = probe(QStandardPaths::findExecutable(QStringLiteral("gswin32c")));
    if (p.isEmpty()) p = probe(QStandardPaths::findExecutable(QStringLiteral("gs")));
    if (p.isEmpty()) {
        // 3) 回退:扫 Program Files 下的 gs* 目录
        for (const QString& root : {QStringLiteral("C:/Program Files"),
                                    QStringLiteral("C:/Program Files (x86)")}) {
            const QDir d(root);
            for (const QFileInfo& e : d.entryInfoList({QStringLiteral("gs*")}, QDir::Dirs)) {
                const QString cand = e.absoluteFilePath() + QStringLiteral("/bin/gswin64c.exe");
                if (QFileInfo::exists(cand)) { p = cand; break; }
                const QString c32 = e.absoluteFilePath() + QStringLiteral("/bin/gswin32c.exe");
                if (QFileInfo::exists(c32)) { p = c32; break; }
            }
            if (!p.isEmpty()) break;
        }
    }
    cached = p;
    return cached;
}

// 页数:用 Ghostscript 的 pdfpagecount。拿不到返回 0(表示"未知",UI 不显示页数)
inline int pageCount(const QString& pdfPath) {
    const QString gs = gsExe();
    if (gs.isEmpty()) return 0;
    QProcess proc;
    hideConsoleWindow(proc);   // gswin* 是控制台程序,页数探测不闪黑窗
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(gs, {
        QStringLiteral("-q"), QStringLiteral("-dNODISPLAY"), QStringLiteral("-dNOSAFER"),
        QStringLiteral("-c"),
        QStringLiteral("(%1) (r) file runpdfbegin pdfpagecount = quit")
            .arg(QDir::toNativeSeparators(pdfPath).replace(QLatin1Char('\\'),
                                                           QStringLiteral("\\\\")))
    });
    if (!proc.waitForFinished(6000)) { proc.kill(); return 0; }
    bool ok = false;
    const int n = QString::fromUtf8(proc.readAllStandardOutput()).trimmed().toInt(&ok);
    return ok ? n : 0;
}

// 渲染第 page 页(1 起)为图片。renderDpi 越高越清晰但越慢
inline QImage renderPage(const QString& pdfPath, int page, int renderDpi = 110) {
    const QString gs = gsExe();
    if (gs.isEmpty()) return {};
    if (page < 1) page = 1;

    const QString outDir = QDir::tempPath() + QStringLiteral("/gaze_pdf");
    QDir().mkpath(outDir);
    // 文件名带页号与 dpi:同一文件的不同页/清晰度各自成缓存条目
    const QString key = QString::number(qHash(pdfPath), 16);
    const QString out = QStringLiteral("%1/%2_p%3_d%4.png")
                            .arg(outDir, key).arg(page).arg(renderDpi);

    // 已有缓存且比源文件新 → 直接用(切页来回切不重复渲染)
    QFileInfo src(pdfPath), cached(out);
    if (!cached.exists() || cached.lastModified() < src.lastModified()) {
        QProcess proc;
        hideConsoleWindow(proc);   // PDF 渲页的 gswin* 同样静默起
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(gs, {
            QStringLiteral("-q"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"),
            QStringLiteral("-dSAFER"),
            QStringLiteral("-dFirstPage=%1").arg(page),
            QStringLiteral("-dLastPage=%1").arg(page),
            QStringLiteral("-dUseCropBox"),
            QStringLiteral("-sDEVICE=png16m"),
            QStringLiteral("-r%1").arg(renderDpi),
            QStringLiteral("-dTextAlphaBits=4"),
            QStringLiteral("-dGraphicsAlphaBits=4"),
            QStringLiteral("-sOutputFile=%1").arg(out),
            QDir::toNativeSeparators(pdfPath)
        });
        if (!proc.waitForFinished(20000)) { proc.kill(); return {}; }
        if (!QFileInfo::exists(out)) return {};
    }
    QImage img(out);
    return img;
}

} // namespace Pdf
