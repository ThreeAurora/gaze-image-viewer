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
