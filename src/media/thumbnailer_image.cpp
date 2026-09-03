#include "thumbnailer.h"
#include "constants.h"
#include "namesort.h"
#include "settings.h"
#include "imgproc.h"
#include "wicdecode.h"
#include "rawdecode.h"    // #140:RAW 缩略图 = 相机内嵌 JPEG
#include "logger.h"

#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <QPainter>
#include <QPainterPath>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QCryptographicHash>
#include <QPixmap>
#include <QImageReader>
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>

// ── 前置声明 ──
static QImage windowsShellThumb(const QString& filePath, int size);
#include <wincodec.h>
#include <QDateTime>
#include <QVariant>
#include <QImageReader>
#include <QBuffer>
#include <QFile>
#include <chrono>
#include <cstring>
#include <cmath>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

#include "thumbnailer_internal.h"

// ── 本编译单元(#129 从 thumbnailer.cpp 拆出):图片缩略图(外来格式与 CMYK 分流) / Shell 缩略图对外入口 ──

QImage Thumbnailer::shellThumbFor(const QString& filePath, int size) {
    return th_impl::windowsShellThumb(filePath, size);
}

// ═══════════════════════════════════════════
// 图片缩略图（回退策略;0.4 为 #116 外来格式分流）
//   策略0.3: RAW → 相机内嵌 JPEG(#140,2026-09-02;零成本,与回放所见一致)
//   策略0.4: AVIF/JXL→ffmpeg、HEIF→WIC(Qt 原生读不了)
//   策略0.5: Windows Shell 缩略图缓存（0-5ms）← Everything 同款
//   策略1: 小文件直接加载（免 reader）
//   策略2: QImageReader::setScaledSize 降采样
//   策略3: QImage 回退
// ═══════════════════════════════════════════
QImage Thumbnailer::imageThumb(const QString& filePath, int size) {
    // 注:Windows Shell 缓存由 generate() 按 Thumbs/useEmbedded 决定是否先取,
    // 此函数是"从原图生成"的路径,不再自己抢先取 Shell
    QFileInfo fi(filePath);
    // Thumbs/highQuality:开=平滑缩放 + 2x 超采样(默认);关=快速缩放 + 1x(更快更糙)
    const Prefs p = prefs();
    const Qt::TransformationMode mode =
        p.highQuality ? Qt::SmoothTransformation : Qt::FastTransformation;
    const int sample = p.highQuality ? 2 : 1;

    // 最终缩放出口:Thumbs/gamma 开 → 线性光降采样;否则常规缩放
    auto scaleTo = [&](const QImage& src) -> QImage {
        QSize dst = src.size().scaled(size, size, Qt::KeepAspectRatio);
        if (dst.isEmpty()) return {};
        if (p.gamma) return ImgProc::linearResample(src, dst);
        return src.scaled(dst, Qt::KeepAspectRatio, mode);
    };

    // ── 策略0.3: RAW → 相机内嵌 JPEG(#140)─────────────────
    // RAW 白名单独立于 IMAGE_EXTS:绝不进 Qt 解码/Shell(它们只会读到黑漆漆的
    // 拜耳马赛克或直接失败)。内嵌 JPEG 即机身预览,取它零成本又与相机回放一致。
    // 失败(机型不嵌 JPEG / 内嵌位图 / 库缺失)才落入下方通用路径。
#ifdef HAS_RAWDEC
    const QString tSuf3 = fi.suffix().toLower();
    if (RAW_EXTS.count("." + tSuf3)) {
        QImage emb = RawDecode::decodeEmbeddedJpeg(filePath, size * sample);
        if (!emb.isNull()) return scaleTo(emb);
        return {};   // 内嵌拿不到就空,不吃 Shell/Qt 的错误图
    }
#endif

    // ── 策略0.4: Qt 原生读不了的静图(#116) → 外部解码 ──
    // AVIF/JXL/HEIF 走 ffmpeg 自带解码(#8 起 HEIC/HEIF/HIF 不再依赖系统
    // "HEIF 图像扩展");ffmpeg 空图才回退 WIC。decodeScaled 分流口径一致。
    const QString tSuf = fi.suffix().toLower();
    if (ForeignImg::isFfmpegStill(tSuf) || ForeignImg::isWicHeif(tSuf)) {
        QImage img = ForeignImg::decodeFfmpegStill(filePath, size * sample);
        if (img.isNull() && ForeignImg::isWicHeif(tSuf))
            img = WicDecode::decodeHeif(filePath, size * sample);   // 降级:系统扩展兜底
        if (!img.isNull()) return scaleTo(img);
        return {};   // 原生 reader 对这些格式必然失败,不再空跑策略1-3
    }

    // ── 策略0.5: CMYK JPG → WIC 色彩管理(与预览同管线) ──
    // Qt/libjpeg 对无 ICC 的 CMYK 只做简单反演,必然偏亮;WIC 按 SWOP 假定转换
    // 与 QQ/Windows 照片同色。Shell 未命中(大图/无缓存)时必须走这条,否则偏亮。
    // #206:收口到 decodeCmykCached —— 大图走 4096 副本,批量扫图不再每张 4~5s
    if (WicDecode::isFourChannelJpeg(filePath)) {
        QImage wic = ImgProc::decodeCmykCached(filePath, size * sample);
        if (!wic.isNull()) return scaleTo(wic);
    }

    // ── 策略1: 小文件直接加载 ──
    if (fi.size() > 0 && fi.size() < 500 * 1024) {
        QImage img(filePath);
        if (!img.isNull()) return scaleTo(img);
    }

    // ── 策略2: QImageReader 降采样(按原图比例,防拉伸变形) ──
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QSize orig = reader.size();
    if (orig.isValid() && orig.width() > 0 && orig.height() > 0)
        reader.setScaledSize(orig.scaled(size * sample, size * sample, Qt::KeepAspectRatio));
    QImage img = reader.read();

    // ── 策略3: 回退 ──
    if (img.isNull())
        img = QImage(filePath);
    if (img.isNull()) return {};

    return scaleTo(img);
}
