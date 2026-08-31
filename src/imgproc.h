#pragma once
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QColor>
#include <QSize>
#include <QFileInfo>
#include <cmath>
#include <cstring>
#include "wicdecode.h"
#include "foreignimg.h"

// ═══════════════════════════════════════════
// 图像后处理公共件(缩略图管线 + 查看器渲染共用)
//   之前 linearResample/sharpen/checkerBg 在 thumbnailer.cpp 和
//   previewpanel.cpp 各写了一份,已经出现参数漂移 —— 收口到这里。
// ═══════════════════════════════════════════
namespace ImgProc {

// sRGB ↔ 线性光查找表(进程内只建一次)
struct GammaLut {
    double s2l[256];
    double l2s[1024];
    GammaLut() {
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            s2l[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }
        for (int i = 0; i < 1024; ++i) {
            const double c = i / 1023.0;
            l2s[i] = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        }
    }
};

inline const GammaLut& lut() {
    static const GammaLut g;
    return g;
}

// 线性光重采样:先 sRGB→linear 再盒式平均,最后 linear→sRGB。
// 直接在 gamma 空间做插值,亮部/暗部边界会失真(降采样发灰、放大发暗)
inline QImage linearResample(const QImage& src, const QSize& dstSize) {
    if (src.isNull() || dstSize.isEmpty()) return {};
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage out(dstSize, QImage::Format_ARGB32);
    if (out.isNull()) return {};
    const GammaLut& g = lut();
    const double rx = double(s.width())  / dstSize.width();
    const double ry = double(s.height()) / dstSize.height();
    for (int y = 0; y < dstSize.height(); ++y) {
        for (int x = 0; x < dstSize.width(); ++x) {
            const int x0 = int(x * rx), x1 = qMin(s.width(),  int((x + 1) * rx));
            const int y0 = int(y * ry), y1 = qMin(s.height(), int((y + 1) * ry));
            double lr = 0, lg = 0, lb = 0, la = 0;
            int n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    const QRgb px = s.pixel(xx, yy);
                    const double av = qAlpha(px) / 255.0;
                    lr += g.s2l[qRed(px)]   * av;
                    lg += g.s2l[qGreen(px)] * av;
                    lb += g.s2l[qBlue(px)]  * av;
                    la += av;
                    ++n;
                }
            }
            if (n == 0 || la <= 0) { out.setPixel(x, y, 0); continue; }
            const auto enc = [&g](double lin) {
                const int idx = qBound(0, int(lin * 1023.0 + 0.5), 1023);
                return qBound(0, int(g.l2s[idx] * 255.0 + 0.5), 255);
            };
            out.setPixel(x, y, qRgba(enc(lr / la), enc(lg / la), enc(lb / la),
                                     qBound(0, int(la / n * 255.0 + 0.5), 255)));
        }
    }
    return out;
}

// 3x3 锐化卷积。amount=锐化强度:0.5 ≈ XnView"使用锐化 50%",
// 也是缩略图/查看器管线实际采用的档位(1.0 过强:光晕失真+量化伪影被放大)
inline QImage sharpen(const QImage& src, double amount) {
    if (src.isNull()) return {};
    QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage dst(s.size(), QImage::Format_ARGB32);
    const int w = s.width(), h = s.height();
    const double c = 1.0 + 4.0 * amount;      // 中心系数
    const double n = -amount;                 // 邻域系数
    const double k[3][3] = {{0, n, 0}, {n, c, n}, {0, n, 0}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = qBound(0, y + dy, h - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = qBound(0, x + dx, w - 1);
                    const QRgb px = s.pixel(xx, yy);
                    const double kv = k[dy + 1][dx + 1];
                    r += qRed(px)   * kv;
                    g += qGreen(px) * kv;
                    b += qBlue(px)  * kv;
                    a += qAlpha(px) * kv;
                }
            }
            const auto cl = [](double v) { return static_cast<int>(qBound(0.0, v, 255.0)); };
            dst.setPixel(x, y, qRgba(cl(r), cl(g), cl(b), cl(a)));
        }
    }
    return dst;
}

// 透明区域的棋盘格底纹(8px 两色,深色主题配色)
inline QImage checkerBg(int w, int h) {
    QImage bg(w, h, QImage::Format_RGB32);
    const int cell = 8;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            bg.setPixel(x, y, (((x / cell) + (y / cell)) & 1)
                                  ? 0xFF3A3A40 : 0xFF26262B);
    return bg;
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
// 也放在 decodeScaled 之前:同命名空间 inline 函数同样要先声明后使用。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// ── 全图解码统一入口(查看器 + 打印共用) ──────────────────
// 原先只住在 previewpanel.cpp 里(叫 loadFullImage)。打印要的是"屏幕上看到什么,
// 纸上就是什么",所以解码口径必须只有一份 —— CMYK 印刷 JPG 走 WIC 色彩管理那套,
// 复制一份到打印路径迟早会和查看器偏色不一致。
//
// maxSide>0:按最长边降采样解码(预览用,省内存省时间);0:全尺寸。
// ⚠ 降采样后的 QImage 自带 DPI 不可信(实测 Qt6.5.3:JPEG 密度不变、PNG 同比缩小),
//   需要 DPI 的调用方(打印"原始尺寸"档)必须传 maxSide=0。
// ⚠ exifRotate 必须由 GUI 线程 caller 快照后传入:worker 里读 AppSettings/QSettings
//   属跨线程访问(未加锁),表现偶发但真存在崩溃/脏读。
inline QImage decodeScaled(const QString& path, bool exifRotate, int maxSide) {
    // ── 策略0.4: Qt 原生读不了的格式(#116)先行分流 ──
    // AVIF/JXL → 随 gaze 的 ffmpeg 子进程;HEIF/HEIC → 系统 WIC。
    // 失败(无 ffmpeg/无 HEIF 扩展/坏文件)不 return,落回下方 Qt 路径
    // —— Qt 也读不了 → 空图,由上层照常显示占位。
    const QString fSuf = QFileInfo(path).suffix().toLower();
    if (ForeignImg::isFfmpegStill(fSuf)) {
        QImage img = ForeignImg::decodeFfmpegStill(path, maxSide);
        if (!img.isNull()) return img;
    } else if (ForeignImg::isWicHeif(fSuf)) {
        QImage img = WicDecode::decodeHeif(path, maxSide);
        if (!img.isNull()) return img;
    }
    if (WicDecode::isFourChannelJpeg(path)) {
        // WIC 这条路**不做 EXIF 转正**(decodeCmyk 只按 frame 原始宽高走 scaler),
        // 所以 want 必须按未转正尺寸算。CMYK JPEG 带拍摄方向是极罕见的组合,
        // 表现与查看器/缩略图一致(都不转正),打印排版靠长宽比自检退回位图尺寸。
        QSize want;
        const QSize s0 = QImageReader(path).size();
        if (maxSide > 0 && s0.isValid() && qMax(s0.width(), s0.height()) > maxSide)
            want = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
        QImage wic = WicDecode::decodeCmyk(path, want);
        if (!wic.isNull()) return wic;          // 失败则回退 Qt 常规路径(绝不空手而归)
    }
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    if (maxSide > 0) {
        // setScaledSize 工作在**未转正**的像素空间(实测:请求 60x30 + 方向 6 → 得到 30x60)
        const QSize s0 = r.size();
        if (s0.isValid() && qMax(s0.width(), s0.height()) > maxSide) {
            const QSize fit = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
            if (fit.isValid()) r.setScaledSize(fit);
        }
    }
    return r.read();
}

inline QImage decodeFull(const QString& path, bool exifRotate) {
    return decodeScaled(path, exifRotate, 0);
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
// 也放在 decodeScaled 之前:同命名空间 inline 函数同样要先声明后使用。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// ── 全图解码统一入口(查看器 + 打印共用) ──────────────────
// 原先只住在 previewpanel.cpp 里(叫 loadFullImage)。打印要的是"屏幕上看到什么,
// 纸上就是什么",所以解码口径必须只有一份 —— CMYK 印刷 JPG 走 WIC 色彩管理那套,
// 复制一份到打印路径迟早会和查看器偏色不一致。
//
// maxSide>0:按最长边降采样解码(预览用,省内存省时间);0:全尺寸。
// ⚠ 降采样后的 QImage 自带 DPI 不可信(实测 Qt6.5.3:JPEG 密度不变、PNG 同比缩小),
//   需要 DPI 的调用方(打印"原始尺寸"档)必须传 maxSide=0。
// ⚠ exifRotate 必须由 GUI 线程 caller 快照后传入:worker 里读 AppSettings/QSettings
//   属跨线程访问(未加锁),表现偶发但真存在崩溃/脏读。
inline QImage decodeScaled(const QString& path, bool exifRotate, int maxSide) {
    if (WicDecode::isFourChannelJpeg(path)) {
        // WIC 这条路**不做 EXIF 转正**(decodeCmyk 只按 frame 原始宽高走 scaler),
        // 所以 want 必须按未转正尺寸算。CMYK JPEG 带拍摄方向是极罕见的组合,
        // 表现与查看器/缩略图一致(都不转正),打印排版靠长宽比自检退回位图尺寸。
        QSize want;
        const QSize s0 = QImageReader(path).size();
        if (maxSide > 0 && s0.isValid() && qMax(s0.width(), s0.height()) > maxSide)
            want = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
        QImage wic = WicDecode::decodeCmyk(path, want);
        if (!wic.isNull()) return wic;          // 失败则回退 Qt 常规路径(绝不空手而归)
    }
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    if (maxSide > 0) {
        // setScaledSize 工作在**未转正**的像素空间(实测:请求 60x30 + 方向 6 → 得到 30x60)
        const QSize s0 = r.size();
        if (s0.isValid() && qMax(s0.width(), s0.height()) > maxSide) {
            const QSize fit = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
            if (fit.isValid()) r.setScaledSize(fit);
        }
    }
    return r.read();
}

inline QImage decodeFull(const QString& path, bool exifRotate) {
    return decodeScaled(path, exifRotate, 0);
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
// 也放在 decodeScaled 之前:同命名空间 inline 函数同样要先声明后使用。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// ── 全图解码统一入口(查看器 + 打印共用) ──────────────────
// 原先只住在 previewpanel.cpp 里(叫 loadFullImage)。打印要的是"屏幕上看到什么,
// 纸上就是什么",所以解码口径必须只有一份 —— CMYK 印刷 JPG 走 WIC 色彩管理那套,
// 复制一份到打印路径迟早会和查看器偏色不一致。
//
// maxSide>0:按最长边降采样解码(预览用,省内存省时间);0:全尺寸。
// ⚠ 降采样后的 QImage 自带 DPI 不可信(实测 Qt6.5.3:JPEG 密度不变、PNG 同比缩小),
//   需要 DPI 的调用方(打印"原始尺寸"档)必须传 maxSide=0。
// ⚠ exifRotate 必须由 GUI 线程 caller 快照后传入:worker 里读 AppSettings/QSettings
//   属跨线程访问(未加锁),表现偶发但真存在崩溃/脏读。
inline QImage decodeScaled(const QString& path, bool exifRotate, int maxSide) {
    if (WicDecode::isFourChannelJpeg(path)) {
        // WIC 这条路**不做 EXIF 转正**(decodeCmyk 只按 frame 原始宽高走 scaler),
        // 所以 want 必须按未转正尺寸算。CMYK JPEG 带拍摄方向是极罕见的组合,
        // 表现与查看器/缩略图一致(都不转正),打印排版靠长宽比自检退回位图尺寸。
        QSize want;
        const QSize s0 = QImageReader(path).size();
        if (maxSide > 0 && s0.isValid() && qMax(s0.width(), s0.height()) > maxSide)
            want = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
        QImage wic = WicDecode::decodeCmyk(path, want);
        if (!wic.isNull()) return wic;          // 失败则回退 Qt 常规路径(绝不空手而归)
    }
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    if (maxSide > 0) {
        // setScaledSize 工作在**未转正**的像素空间(实测:请求 60x30 + 方向 6 → 得到 30x60)
        const QSize s0 = r.size();
        if (s0.isValid() && qMax(s0.width(), s0.height()) > maxSide) {
            const QSize fit = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
            if (fit.isValid()) r.setScaledSize(fit);
        }
    }
    return r.read();
}

inline QImage decodeFull(const QString& path, bool exifRotate) {
    return decodeScaled(path, exifRotate, 0);
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// 转正后的原始像素尺寸(不解码,只读文件头)。
// QImageReader::size() 报的是**未转正**尺寸 —— 实测 Qt6.5.3:方向 6 的文件
// size()=400x200 而 read() 得到 200x400。打印排版要用后者。
// 也放在 decodeScaled 之前:同命名空间 inline 函数同样要先声明后使用。
inline QSize orientedSize(const QString& path, bool exifRotate) {
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    QSize s = r.size();
    if (exifRotate && s.isValid() &&
        r.transformation().testFlag(QImageIOHandler::TransformationRotate90))
        s.transpose();                          // Rotate90 位=4:90/180 组合里带它的都要换宽高
    return s;
}

// ── 全图解码统一入口(查看器 + 打印共用) ──────────────────
// 原先只住在 previewpanel.cpp 里(叫 loadFullImage)。打印要的是"屏幕上看到什么,
// 纸上就是什么",所以解码口径必须只有一份 —— CMYK 印刷 JPG 走 WIC 色彩管理那套,
// 复制一份到打印路径迟早会和查看器偏色不一致。
//
// maxSide>0:按最长边降采样解码(预览用,省内存省时间);0:全尺寸。
// ⚠ 降采样后的 QImage 自带 DPI 不可信(实测 Qt6.5.3:JPEG 密度不变、PNG 同比缩小),
//   需要 DPI 的调用方(打印"原始尺寸"档)必须传 maxSide=0。
// ⚠ exifRotate 必须由 GUI 线程 caller 快照后传入:worker 里读 AppSettings/QSettings
//   属跨线程访问(未加锁),表现偶发但真存在崩溃/脏读。
inline QImage decodeScaled(const QString& path, bool exifRotate, int maxSide) {
    if (WicDecode::isFourChannelJpeg(path)) {
        // WIC 这条路**不做 EXIF 转正**(decodeCmyk 只按 frame 原始宽高走 scaler),
        // 所以 want 必须按未转正尺寸算。CMYK JPEG 带拍摄方向是极罕见的组合,
        // 表现与查看器/缩略图一致(都不转正),打印排版靠长宽比自检退回位图尺寸。
        QSize want;
        const QSize s0 = QImageReader(path).size();
        if (maxSide > 0 && s0.isValid() && qMax(s0.width(), s0.height()) > maxSide)
            want = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
        QImage wic = WicDecode::decodeCmyk(path, want);
        if (!wic.isNull()) return wic;          // 失败则回退 Qt 常规路径(绝不空手而归)
    }
    QImageReader r(path);
    r.setAutoTransform(exifRotate);
    if (maxSide > 0) {
        // setScaledSize 工作在**未转正**的像素空间(实测:请求 60x30 + 方向 6 → 得到 30x60)
        const QSize s0 = r.size();
        if (s0.isValid() && qMax(s0.width(), s0.height()) > maxSide) {
            const QSize fit = s0.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
            if (fit.isValid()) r.setScaledSize(fit);
        }
    }
    return r.read();
}

inline QImage decodeFull(const QString& path, bool exifRotate) {
    return decodeScaled(path, exifRotate, 0);
}

} // namespace ImgProc
