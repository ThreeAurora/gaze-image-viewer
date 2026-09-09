#pragma once
// ═══════════════════════════════════════════════════════════
// 外部工具定位(#110/#112/#113 规约的唯一实现)
//
// 顺序:exe 旁 <子目录>/<工具>.exe 优先 → PATH 兜底 → 空(调用方降级)。
// ffmpeg/ffprobe/jpegtran 都按这套裁决,收口到这里避免"改了一处忘了另一处"。
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QProcess>

inline QString locateVendoredTool(const QString& subDir, const QString& baseName) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cand = appDir + QLatin1Char('/') + subDir + QLatin1Char('/')
                       + baseName + QStringLiteral(".exe");
    if (QFileInfo::exists(cand)) return QDir::toNativeSeparators(cand);
    const QString fromPath = QStandardPaths::findExecutable(baseName);
    return fromPath.isEmpty() ? QString() : QDir::toNativeSeparators(fromPath);
}

inline QString locateFfmpegTool(const QString& baseName) {
    return locateVendoredTool(QStringLiteral("ffmpeg"), baseName);
}

// ── 常见安装位置探测(2026-09-09) ──
// 公开仓库不能写死某个人的盘符:python / jpegtran 这类"用户机器上多半装了、
// 但不随包分发"的工具,一律按 环境变量 → 常见安装目录 → PATH 的顺序找;
// 都找不到就返回空,由调用方降级提示(绝不猜一个必然不存在的路径)。
inline QString locateFirstExisting(const QStringList& candidates) {
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return QDir::toNativeSeparators(c);
    return {};
}

// Python 解释器:GAZE_PYTHON → 各盘 miniconda/anaconda → PATH。
// (PATH 放最后:Windows 自带的 python.exe 商店别名常常不是真解释器。)
inline QString locatePython() {
    const QByteArray env = qgetenv("GAZE_PYTHON");
    if (!env.isEmpty()) {
        const QString p = QString::fromLocal8Bit(env);
        if (QFileInfo::exists(p)) return QDir::toNativeSeparators(p);
    }
    QStringList cands;
    const QStringList drives = {QStringLiteral("C:"), QStringLiteral("D:"),
                                QStringLiteral("E:"), QStringLiteral("F:")};
    for (const QString& d : drives) {
        cands << d + QStringLiteral("/miniconda3/python.exe")
              << d + QStringLiteral("/anaconda3/python.exe")
              << d + QStringLiteral("/ProgramData/miniconda3/python.exe")
              << d + QStringLiteral("/ProgramData/anaconda3/python.exe");
    }
    const QString home = QDir::homePath();
    cands << home + QStringLiteral("/miniconda3/python.exe")
          << home + QStringLiteral("/anaconda3/python.exe");
    const QString found = locateFirstExisting(cands);
    if (!found.isEmpty()) return found;
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("python"));
    return fromPath.isEmpty() ? QString() : QDir::toNativeSeparators(fromPath);
}

// jpegtran:exe 旁 jpegtran/ → PATH(两者都在 locateVendoredTool 里) → 常见安装位置
inline QString locateJpegtran() {
    const QString vendored = locateVendoredTool(QStringLiteral("jpegtran"),
                                                QStringLiteral("jpegtran"));
    if (!vendored.isEmpty()) return vendored;
    QStringList cands;
    const QStringList drives = {QStringLiteral("C:"), QStringLiteral("D:"),
                                QStringLiteral("E:"), QStringLiteral("F:")};
    for (const QString& d : drives) {
        cands << d + QStringLiteral("/miniconda3/Library/bin/jpegtran.exe")
              << d + QStringLiteral("/anaconda3/Library/bin/jpegtran.exe")
              << d + QStringLiteral("/Program Files/ImageMagick/jpegtran.exe")
              << d + QStringLiteral("/Program Files (x86)/ImageMagick/jpegtran.exe");
    }
    return locateFirstExisting(cands);
}

// Windows 下 QProcess 拉起控制台程序(ffmpeg/ffprobe/gswin32c/python/jpegtran 全是
// 控制台 exe)时,父进程是 GUI、没有控制台,CreateProcess 若不带 CREATE_NO_WINDOW
// 就会为每个子进程凭空闪现一个黑色控制台窗口——"批量生成缩略图/渲染 PDF 时
// 小窗口一闪而过"全来自这里。跑外部工具的统一先 hideConsoleWindow()。
// 0x08000000 = CREATE_NO_WINDOW:写字面量是为避免头文件 include <windows.h> 的宏污染。
inline void hideConsoleWindow(QProcess& p) {
#if defined(Q_OS_WIN)
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000;
    });
#endif
}
