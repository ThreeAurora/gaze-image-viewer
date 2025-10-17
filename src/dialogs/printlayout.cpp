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

    for (int i = 0; i < n; ++i) {
        const int idx = first + i;
        const QRectF cell(inner.left() + (i % cols) * (cw + gap),
                          inner.top()  + (i / cols) * (ch + gap), cw, ch);
        QRectF imgBox = cell;
        if (capBand > 0) imgBox.setHeight(std::max<qreal>(1, cell.height() - capBand));

        PrintImageInfo meta = infoFn ? infoFn(idx) : PrintImageInfo();
        QImage img = imageFn ? imageFn(idx) : QImage();
        const QString capText = captionFor(opt.caption, meta, paths.value(idx));

        if (img.isNull() && !meta.ok) {
            // 坏文件也占格 + 画叉:静默少画一张是发现不了的
            QPen failPen(QColor(150, 150, 150));
            failPen.setWidthF(std::max(1.0, 0.3 * mmPix));
            g.setPen(failPen);
            g.drawRect(imgBox);
            g.drawLine(imgBox.topLeft(), imgBox.bottomRight());
            g.drawLine(imgBox.bottomLeft(), imgBox.topRight());
            ++res.failed;
        } else if (!img.isNull() && img.width() > 0 && img.height() > 0) {
            const QSizeF isz(img.width(), img.height());
            const qreal fitScale = std::min(imgBox.width() / isz.width(),
                                            imgBox.height() / isz.height());
            qreal scale;
            switch (opt.fit) {
            case PrintFit::Fill:
                scale = std::max(imgBox.width() / isz.width(),
                                 imgBox.height() / isz.height());
                break;
            case PrintFit::Actual: {
                const qreal d = meta.dpiX > 0 ? meta.dpiX : 96.0;
                scale = dpi / d;                    // 像素 → 该 DPI 下的真实物理尺寸
                if (isz.width() * scale > imgBox.width() + 0.5 ||
                    isz.height() * scale > imgBox.height() + 0.5) {
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

            QRectF target(0, 0, isz.width() * scale, isz.height() * scale);
            QRectF src(0, 0, isz.width(), isz.height());
            if (opt.fit == PrintFit::Fill) {
                target = imgBox;
                src.setSize(QSizeF(imgBox.width() / scale, imgBox.height() / scale));
                src.moveCenter(QRectF(0, 0, isz.width(), isz.height()).center());
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
                ++res.failed;
            }
        } else {
            ++res.failed;      // 有尺寸信息却拿不到像素:同样按没印出来算
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
