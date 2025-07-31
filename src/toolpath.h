#pragma once
// ═══════════════════════════════════════════════════════════
// 外部工具定位(#110/#112/#113 规约的唯一实现)
//
// 顺序:exe 旁 <子目录>/<工具>.exe 优先 → PATH 兜底 → 空(调用方降级)。
// ffmpeg/ffprobe/jpegtran 都按这套裁决,收口到这里避免"改了一处忘了另一处"。
// ═══════════════════════════════════════════════════════════

#include <QString>
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
