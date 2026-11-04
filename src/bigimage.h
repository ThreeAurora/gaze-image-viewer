#pragma once
// ═══════════════════════════════════════════════════════════════════
// 超大图防护:允许展示的最大像素 + 内存估算
//
// 背景:QImageReader 的分配上限按"解码后位图字节数"算,一张 20MB 的
// JPEG 解成 32bpp 位图可以到 389MB(压缩比约 19 倍)。硬编码一个上限
// 对用户不透明 —— 改成用户可配的"最大像素数",并在设置页实时换算成
// 内存占用给用户看。
//
// 口径:
//   · 像素数 = width * height(未转正的原始像素,与 EXIF 方向无关)
//   · 内存估算按 32bpp(QImage 解 JPEG/PNG 的实际落点 Format_RGB32/ARGB32)
//   · QImageReader 上限由像素上限换算得出,两者永远同步(见 applyAllocationLimit)
// ═══════════════════════════════════════════════════════════════════
#include <QString>
#include <QSize>
#include <QFileInfo>
#include <QImageReader>
#include <QCoreApplication>
#include "settings.h"
#include "i18n.h"

namespace BigImage {

// 默认允许展示的最大像素数 = 2048MB / 4 字节 ≈ 5.37 亿像素。
// 与"把 QImageReader 上限设成 2048MB"是同一个口径的两种表达。
inline constexpr qint64 kDefaultMaxPixels = 536870912LL;   // 2^29

// 设置键。放在 Viewer 组下,与"查看 → 其他"页的其他性能项同组。
inline constexpr const char* kKeyMaxPixels = "Viewer/maxPixels";

// QImageReader 上限的下限:低于此值连 4K 图都打不开,不给出这种选项
inline constexpr int kMinAllocationMb = 64;
// 上限:QImageReader::setAllocationLimit 接受 int MB,给个够大的天花板
inline constexpr int kMaxAllocationMb = 32768;   // 32GB

// 用户设定的最大像素数(0 或非法值 → 默认值)
inline qint64 maxPixels() {
    const qint64 v = AppSettings::instance().get(kKeyMaxPixels, kDefaultMaxPixels).toLongLong();
    return v > 0 ? v : kDefaultMaxPixels;
}

// 像素数 → 32bpp 位图字节数
inline qint64 bytesForPixels(qint64 px) {
    return px > 0 ? px * 4 : 0;
}

// 字节数 → 人类可读("389 MB" / "2.0 GB")
inline QString formatBytes(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("0 MB");
    const double mb = double(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0)
        return QString::number(mb / 1024.0, 'f', 1) + QStringLiteral(" GB");
    return QString::number(mb, 'f', 0) + QStringLiteral(" MB");
}

// 像素数 → "1.02 亿" 这种中文口径(设置页与弹窗共用同一套说法)
inline QString formatPixels(qint64 px) {
    if (px <= 0) return QStringLiteral("0");
    const double yi = double(px) / 1e8;
    if (yi >= 1.0)
        return QString::number(yi, 'f', 2) + gazeTr(" 亿");
    const double wan = double(px) / 1e4;
    return QString::number(wan, 'f', 0) + gazeTr(" 万");
}

// 像素上限 → 对应的 QImageReader 分配上限(MB)
inline int allocationMbForPixels(qint64 px) {
    const qint64 bytes = bytesForPixels(px > 0 ? px : kDefaultMaxPixels);
    // 向上取整到 MB:刚好卡在边界上的图不应被拒
    qint64 mb = (bytes + (1024 * 1024) - 1) / (1024 * 1024);
    if (mb < kMinAllocationMb) mb = kMinAllocationMb;
    if (mb > kMaxAllocationMb) mb = kMaxAllocationMb;
    return int(mb);
}

// 本次会话内用户明确允许过的最大像素(弹窗「确定」/「始终允许」累积)。
// 不落盘,进程结束即失效 —— 「确定」就是"这一次(会话)"的语义。
// 存在的理由:用户点完确定后若又改了别的设置,applyAllocationLimit 会被
// 重新触发;不把它并进来,上限会回落,同一张图重新解码(如切换 CMYK 对比)
// 就会静默失败。
inline qint64& sessionAllowedPixels() {
    static qint64 v = 0;
    return v;
}

// 这次允许了 px 这么大的图(不落盘)。调用方随后应 applyAllocationLimit()。
inline void noteSessionAllowance(qint64 px) {
    if (px > sessionAllowedPixels()) sessionAllowedPixels() = px;
}

// 把当前设置应用到 QImageReader 全局上限。启动时调一次;设置变更时再调。
// 取"设置值"与"本会话已允许过的最大值"中较大者,规则见 sessionAllowedPixels。
inline void applyAllocationLimit() {
    const qint64 px = qMax(maxPixels(), sessionAllowedPixels());
    QImageReader::setAllocationLimit(allocationMbForPixels(px));
}

// 读图片像素数(只读文件头,不解码)。失败返回无效 QSize。
// 注意:这是**未转正**的尺寸,与 EXIF 方向无关 —— 像素总数与方向无关,够用。
inline QSize imagePixelSize(const QString& path) {
    QImageReader r(path);
    return r.size();
}

// 该文件是否超过当前设定的最大像素
inline bool exceedsLimit(const QString& path, qint64* outPixels = nullptr) {
    const QSize s = imagePixelSize(path);
    if (!s.isValid() || s.width() <= 0 || s.height() <= 0) return false;   // 读不出尺寸 → 交给正常流程报错
    const qint64 px = qint64(s.width()) * qint64(s.height());
    if (outPixels) *outPixels = px;
    return px > maxPixels();
}

} // namespace BigImage
