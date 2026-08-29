#include "printlayout.h"

#include <QPainter>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QPen>
#include <algorithm>
#include <cmath>

// ═══════════════════════════════════════════
// 排版数学只在这里写一份。预览(QImage)与出图(QPrinter)都调 printRenderPage,
// 区别只是传进来的 paintRect/dpi 和取图器(预览给缩好的小图,出图给原图)。
// ═══════════════════════════════════════════

void printGridShape(int perPage, bool landscape, int& cols, int& rows)
{
    switch (perPage) {
    case 2:  cols = landscape ? 2 : 1; rows = landscape ? 1 : 2; break;
    case 3:  cols = landscape ? 3 : 1; rows = landscape ? 1 : 3; break;
    case 4:  cols = 2; rows = 2; break;
    case 6:  cols = landscape ? 3 : 2; rows = landscape ? 2 : 3; break;
    case 9:  cols = 3; rows = 3; break;
    default: cols = 1; rows = 1; break;
    }
}

int printPageCount(int imageCount, int perPage)
{
    if (imageCount <= 0) return 0;
    const int pp = std::max(1, perPage);
    return (imageCount + pp - 1) / pp;
}

namespace {

QString capName(const PrintImageInfo& meta, const QString& path)
{
    return meta.name.isEmpty() ? QFileInfo(path).fileName() : meta.name;
}

QString captionFor(int mode, const PrintImageInfo& meta, const QString& path)
{
    switch (mode) {
    case PrintCaption::None:     return QString();
    case PrintCaption::Name:     return capName(meta, path);
    case PrintCaption::NameSize:
        return meta.px.isValid()
             ? QStringLiteral("%1  %2×%3").arg(capName(meta, path))
                   .arg(meta.px.width()).arg(meta.px.height())
             : capName(meta, path);
    case PrintCaption::NameDate: {
        const QString name = capName(meta, path);
        return meta.dateText.isEmpty() ? name
                                       : QStringLiteral("%1  %2").arg(name, meta.dateText);
    }
    default: return QString();
    }
}

} // namespace

int printRenderPage(QPainter& g, const QRectF& paintRect, qreal dpi,
                    const PrintOptions& opt,
                    const QStringList& paths, int pageIndex,
                    const PrintInfoFetcher& infoFn,
                    const PrintImageFetcher& imageFn,
                    PrintPageResult* result)
{
    PrintPageResult res;
    const int perPage = std::max(1, opt.perPage);
    const int first = pageIndex * perPage;
    const int n = std::min(perPage, int(paths.size()) - first);
    if (n <= 0) { if (result) *result = res; return 0; }

    const qreal mmPix  = dpi / 25.4;                        // 1mm = 多少设备像素
    const qreal gap    = std::max(0.0, opt.gapMm) * mmPix;
    // 边距最大只能吃到"还剩 1px":0-50mm 的数值框在 A4 上碰不到,但自定义纸张会
    const qreal maxMargin = std::max(0.0,
        std::min(paintRect.width(), paintRect.height()) / 2.0 - 1.0);
    const qreal margin = std::min(std::max(0.0, opt.marginMm) * mmPix, maxMargin);

    // 背景只铺"可印区":纸边那一圈打印机本来就印不到,铺满也是自欺
    if (opt.background != PrintBg::None)
        g.fillRect(paintRect, opt.background == PrintBg::Black ? QColor(0, 0, 0)
                                                               : QColor(255, 255, 255));

    int cols, rows;
    printGridShape(perPage, opt.landscape, cols, rows);
    const QRectF inner = paintRect.adjusted(margin, margin, -margin, -margin);
    const qreal cw = (inner.width()  - gap * (cols - 1)) / cols;
    const qreal ch = (inner.height() - gap * (rows - 1)) / rows;

    QFont capFont = g.font();
    capFont.setPointSizeF(std::max(4.0, double(opt.captionPt)));
    const qreal capBand = opt.caption == PrintCaption::None
                        ? 0.0 : QFontMetricsF(capFont).height() + 1.0 * mmPix;
    const QColor textColor(opt.background == PrintBg::Black ? 0xFFFFFF : 0x141414);
    QPen thinPen(QColor(120, 120, 120));
    thinPen.setWidthF(std::max(1.0, 0.3 * mmPix));

    g.save();
    g.setRenderHint(QPainter::SmoothPixmapTransform, true);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setBrush(Qt::NoBrush);

    auto markFailed = [&g, mmPix](const QRectF& box) {
        QPen failPen(QColor(150, 150, 150));
        failPen.setWidthF(std::max(1.0, 0.3 * mmPix));
        g.setPen(failPen);
        g.drawRect(box);
        g.drawLine(box.topLeft(), box.bottomRight());
        g.drawLine(box.bottomLeft(), box.topRight());
    };

    for (int i = 0; i < n; ++i) {
        const int idx = first + i;
        const QRectF cell(inner.left() + (i % cols) * (cw + gap),
                          inner.top()  + (i / cols) * (ch + gap), cw, ch);
        QRectF imgBox = cell;
        if (capBand > 0) imgBox.setHeight(std::max<qreal>(1, cell.height() - capBand));

        PrintImageInfo meta = infoFn ? infoFn(idx) : PrintImageInfo();
        QImage img = imageFn ? imageFn(idx) : QImage();
        const QString capText = captionFor(opt.caption, meta, paths.value(idx));

        // 读不到像素就一律占格画叉 —— 不管有没有尺寸信息。
        // 静默少画一张,纸上留个洞,没人会发现。
        if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
            markFailed(imgBox);
            ++res.failed;
        } else {
            // 排版一律按"原始像素尺寸"算,不按这次解码出来的位图尺寸算。
            // 预览为提速给的是降采样图,而各格式对降采样的处理并不一致
            // (实测 Qt6.5.3:JPEG 缩到 1/6.67 后 dotsPerMeter 仍是 300,
            //  PNG 同样缩到 1/6.67 后 DPI 也变成 300/6.67=45)。
            // 用位图自身尺寸,"不放大/原始尺寸"两档在预览里和出图里就会给出两套排版。
            QSizeF nom(img.width(), img.height());          // 排版用
            const QSizeF dec(img.width(), img.height());    // src 矩形用
            if (meta.px.width() > 0 && meta.px.height() > 0) {
                const QSizeF orig(meta.px.width(), meta.px.height());
                // 长宽比对不上 = 取图器没按 EXIF 转正,这时只能信图本身
                if (std::abs((orig.width() / orig.height()) / (dec.width() / dec.height()) - 1.0) < 0.01)
                    nom = orig;
            }
            const qreal rx = dec.width() / nom.width();
            const qreal ry = dec.height() / nom.height();

            const qreal fitScale = std::min(imgBox.width() / nom.width(),
                                            imgBox.height() / nom.height());
            qreal scale;
            switch (opt.fit) {
            case PrintFit::Fill:
                scale = std::max(imgBox.width() / nom.width(),
                                 imgBox.height() / nom.height());
                break;
            case PrintFit::Actual: {
                const qreal d = meta.dpiX > 0 ? meta.dpiX : 96.0;
                scale = dpi / d;                    // 像素 → 该 DPI 下的真实物理尺寸
                if (nom.width() * scale > imgBox.width() + 0.5 ||
                    nom.height() * scale > imgBox.height() + 0.5) {
                    scale = std::min(scale, fitScale);   // 放不下才收缩,并如实计数
                    ++res.shrunk;
                }
                break;
            }
            case PrintFit::NoUpscale:
                scale = std::min<qreal>(1.0, fitScale);
                break;
            default:
                scale = fitScale;
            }

            QRectF target(0, 0, nom.width() * scale, nom.height() * scale);
            QRectF src(0, 0, dec.width(), dec.height());
            if (opt.fit == PrintFit::Fill) {
                target = imgBox;
                // 可见的原始像素宽 / 缩放 → 再乘回解码比例,才是位图坐标系里的裁剪框
                src = QRectF(0, 0, imgBox.width() / scale * rx, imgBox.height() / scale * ry)
                      .translated((dec.width() - imgBox.width() / scale * rx) / 2.0,
                                  (dec.height() - imgBox.height() / scale * ry) / 2.0)
                      .intersected(QRectF(0, 0, dec.width(), dec.height()));
            } else {
                target.moveCenter(imgBox.center());
            }

            QImage toDraw = opt.grayscale
                          ? img.convertToFormat(QImage::Format_Grayscale8) : img;
            const QRect srcR = src.toAlignedRect();
            if (target.width() >= 1 && target.height() >= 1 && !srcR.isEmpty()) {
                g.drawImage(target, toDraw, srcR);
                ++res.drawn;
                if (opt.border) { g.setPen(thinPen); g.drawRect(target); }
            } else {
                markFailed(imgBox);
                ++res.failed;
            }
        }

        if (capBand > 0 && !capText.isEmpty()) {
            g.setFont(capFont);
            g.setPen(QPen(textColor));
            const QRectF band(cell.left(), cell.bottom() - capBand, cell.width(), capBand);
            g.drawText(band, Qt::AlignHCenter | Qt::AlignVCenter,
                       QFontMetricsF(capFont).elidedText(capText, Qt::ElideMiddle,
                                                         int(cell.width())));
        }
    }
    g.restore();

    if (result) *result = res;
    return res.drawn;
}
