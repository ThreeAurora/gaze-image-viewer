#pragma once
// ═══════════════════════════════════════════════════════════
// Qt 原生读不了的静图 → 外部解码回退(#116)
//
//   .avif/.avifs/.jxl → 随 gaze 分发的 LGPL ffmpeg 子进程(dav1d/libjxl)
//                       → PNG 管道(不经临时文件)
//   .heic/.heif/.hif  → Windows WIC "Microsoft HEIF Decoder"(进程内;
//                       系统未装 HEIF 图像扩展时解码失败 → 空图回退)
//
// 背景:novomesk 的 AVIF/HEIF/JXL Qt 插件 release 全是 MSVC 构建,与
// MinGW 的 gaze ABI 不兼容(实测 LoadLibrary 直接失败),故走进程外/
// 系统解码,与 #110 Ghostscript、#112 视频缩略图同一模式。
// 已知限制:动图(avifs/jxl 序列)只取第一帧;irot 旋转不做。
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QSize>
#include <QImage>
#include <QProcess>
#include "toolpath.h"

namespace ForeignImg {

inline bool isFfmpegStill(const QString& suffix) {
    return suffix == QLatin1String("avif") || suffix == QLatin1String("avifs")
        || suffix == QLatin1String("jxl");
}

inline bool isWicHeif(const QString& suffix) {
    return suffix == QLatin1String("heic") || suffix == QLatin1String("heif")
        || suffix == QLatin1String("hif");
}

inline bool handles(const QString& suffix) {
    return isFfmpegStill(suffix) || isWicHeif(suffix);
}

// ffmpeg 子进程解第一帧 → PNG 字节流 → QImage。maxSide>0 时解码级等比降采样。
// 调用方都在线程池里(thumbnailer worker / decodeFullAsync / print 池),
// 同步等待 30s 上限只拖 worker 不拖 GUI。
inline QImage decodeFfmpegStill(const QString& path, int maxSide = 0) {
    const QString exe = locateFfmpegTool(QStringLiteral("ffmpeg"));
    if (exe.isEmpty()) return {};

    QStringList args{
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), path,
        QStringLiteral("-frames:v"), QStringLiteral("1") };
    if (maxSide > 0)
        args << QStringLiteral("-vf")
             << QStringLiteral("scale=%1:%1:force_original_aspect_ratio=decrease").arg(maxSide);
    args << QStringLiteral("-f") << QStringLiteral("image2pipe")
         << QStringLiteral("-vcodec") << QStringLiteral("png")
         << QStringLiteral("-");

    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(5000)) return {};
    if (!p.waitForFinished(30000)) {          // 大图兜底:超时杀掉,绝不悬挂
        p.kill();
        p.waitForFinished(3000);
        return {};
    }
    if (p.exitCode() != 0) return {};
    const QByteArray png = p.readAllStandardOutput();
    if (png.isEmpty()) return {};
    return QImage::fromData(png, "PNG");
}

} // namespace ForeignImg
