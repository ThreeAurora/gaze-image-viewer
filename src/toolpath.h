#pragma once
// ═══════════════════════════════════════════════════════════
// 外部工具定位(#110/#112 规约的唯一实现)
//
// 顺序:exe 旁 ffmpeg/<工具>.exe 优先 → PATH 兜底 → 空(调用方降级)。
// 之前这份逻辑只住在 thumbnailer.cpp 的匿名命名空间里,#116 静图解码
// 也要用同一份 ffmpeg,收口到这里避免"改了一处忘了另一处"。
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>

inline QString locateFfmpegTool(const QString& baseName) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cand = appDir + QStringLiteral("/ffmpeg/") + baseName + QStringLiteral(".exe");
    if (QFileInfo::exists(cand)) return QDir::toNativeSeparators(cand);
    const QString fromPath = QStandardPaths::findExecutable(baseName);
    return fromPath.isEmpty() ? QString() : QDir::toNativeSeparators(fromPath);
}
