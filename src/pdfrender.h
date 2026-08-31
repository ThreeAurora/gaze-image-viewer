#pragma once
// ═══════════════════════════════════════════
// PDF 预览渲染(#82)
//   Qt 6.8 的 Windows 二进制不带 QtPdf 模块,故走系统已装的 Ghostscript
//   (gswin64c.exe)把指定页渲染成 PNG,再当普通图片显示。
//   找不到 Ghostscript 时回退 Windows Shell 缩略图(Explorer 那种首页缩略图),
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

namespace Pdf {

// Ghostscript 可执行文件:优先 PATH,再试常见安装路径(本机 gs10.07.0)
inline QString gsExe() {
    static QString cached;
    static bool looked = false;
    if (looked) return cached;
    looked = true;

    auto probe = [](const QString& p) {
        return !p.isEmpty() && QFileInfo::exists(p) ? p : QString();
    };
    QString p = probe(QStandardPaths::findExecutable(QStringLiteral("gswin64c")));
    if (p.isEmpty()) p = probe(QStandardPaths::findExecutable(QStringLiteral("gswin32c")));
    if (p.isEmpty()) p = probe(QStandardPaths::findExecutable(QStringLiteral("gs")));
    if (p.isEmpty()) {
        // 扫 Program Files 下的 gs* 目录(gswin64c 在 bin/ 里)
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
