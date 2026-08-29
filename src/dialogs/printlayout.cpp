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

QString captionFor(const PrintOptions& opt, const PrintImageInfo& meta, const QString& path)
{
    switch (opt.caption) {
    case PrintCaption::None:     return QString();
    case PrintCaption::Name:     return meta.name.isEmpty() ? QFileInfo(path).fileName() : meta.name;
    case PrintCaption::NameSize: {
        const QString name = meta.name.isEmpty() ? QFileInfo(path).fileName() : meta.name;
        return meta.px.isValid() ? QStringLiteral("%1  %2×%3").arg(name).arg(meta.px.width()).arg(meta.px.height())
                                 : name;
    }
    case PrintCaption::NameDate: {
        const QString name = meta.name.isEmpty() ? QFileInfo(path).fileName() : meta.name;
        return meta.dateText.isEmpty() ? name : QStringLiteral("%1  %2").arg(name, meta.dateText);
    }
    }
    return QString();
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

    const qreal mmPix = dpi / 25.4;                       // 1mm = 多少设备像素
    const qreal margin = std::max(0.0, opt.marginMm) * mmPix;
    const qreal gap    = std::max(0.0, opt.gapMm) * mmPix;

    // 背景只铺"可印区":纸边那一圈打印机本来就印不到,铺了也是自欺
    if (opt.background != PrintBg::None)
        g.fillRect(paintRect, opt.background == PrintBg::Black ? QColor(0, 0, 0)
                                                                : QColor(255, 255, 255));

    int cols, rows;
    printGridShape(perPage, opt.landscape, cols, rows);
    QRectF inner = paintRect.adjusted(margin, margin, -margin, -margin);
    // 边距吃到没地方放图时,把边距退到"留下 1px 一格"而不是画到页外去
    if (inner.width() < cols || inner.height() < rows) {
        const qreal shrink = std::min(paintRect.width() / (cols * 2.0),
                                      paintRect.height() / (rows * 2.0));
        margin_unused:;   // 占位:下面的 clamp 已足够,不再二次调整
        inner = QRectF(paintRect.left() + std::min(margin, shrink),
                       paintRect.top()  + std::min(margin, shrink),
                       std::max<qreal>(1, paintRect.width()  - 2 * std::min(margin, shrink)),
                       std::max<qreal>(1, paintRect.height() - 2 * std::min(margin, shrink)));
    }
    const qreal cw = (inner.width()  - gap * (cols - 1)) / cols;
    const qreal ch = (inner.height() - gap * (rows - 1)) / rows;

    QFont capFont = g.font();
    capFont.setPointSizeF(std::max(4.0, double(opt.captionPt)));
    const qreal capBand = opt.caption == PrintCaption::None
                        ? 0.0 : QFontMetricsF(capFont).height() + 1.0 * mmPix;
    const QColor textColor(opt.background == PrintBg::Black ? 0xFFFFFF : 0x141414);

    g.save();
    g.setRenderHint(QPainter::SmoothPixmapTransform, true);
    g.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < n; ++i) {
        const int idx = first + i;
        const QRectF cell(inner.left() + (i % cols) * (cw + gap),
                          inner.top()  + (i / cols) * (ch + gap), cw, ch);
        QRectF imgBox = cell;
        if (capBand > 0) imgBox.setHeight(std::max<qreal>(1, cell.height() - capBand));

        PrintImageInfo meta = infoFn ? infoFn(idx) : PrintImageInfo();
        QImage img = imageFn ? imageFn(idx) : QImage();
        const QString capText = captionFor(opt, meta, paths.value(idx));

        if (img.isNull() && !meta.ok) {
            // 坏文件也占格 + 画叉:静默少画一张是发现不了的
            QPen pen(QColor(150, 150, 150));
            pen.setWidthF(std::max(1.0, 0.3 * mmPix));
            g.setPen(pen); g.setBrush(Qt::NoBrush);
            g.drawRect(imgBox);
            g.drawLine(imgBox.topLeft(), imgBox.bottomRight());
            g.drawLine(imgBox.bottomLeft(), imgBox.topRight());
            ++res.failed;
        } else {
            const QSize isz = img.isNull() ? meta.px : img.size();
            if (isz.width() > 0 && isz.height() > 0 && !img.isNull()) {
                qreal scale;
                const qreal fitScale = std::min(imgBox.width() / isz.width(),
                                                imgBox.height() / isz.height());
                switch (opt.fit) {
                case PrintFit::Fill:
                    scale = std::max(imgBox.width() / isz.width(),
                                     imgBox.height() / isz.height());
                    break;
                case PrintFit::Actual: {
                    const qreal d = meta.dpiX > 0 ? meta.dpiX : 96.0;
                    scale = dpi / d;                       // 原图像素 → 该 DPI 下的真实物理尺寸
                    if (isz.width() * scale > imgBox.width() + 0.5 ||
                        isz.height() * scale > imgBox.height() + 0.5) {
                        scale = std::min(scale, fitScale);  // 放不下才收缩,并如实计数
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
                if (opt.fit == PrintFit::Fill) target = imgBox;
                else target.moveCenter(imgBox.center());

                QRectF src(0, 0, isz.width(), isz.height());
                if (opt.fit == PrintFit::Fill) {
                    src.setSize(QSizeF(imgBox.width() / scale, imgBox.height() / scale));
                    src.moveCenter(QRectF(0, 0, isz.width(), isz.height()).center());
                }

                QImage toDraw = img;
                if (opt.grayscale && img.hasAlphaChannel())
                    toDraw = img.convertToFormat(QImage::Format_ARGB32);
                if (opt.grayscale)
                    toDraw = toDraw.convertToFormat(QImage::Format_Grayscale8);

                g.drawImage(target, toDraw, src.toRect());
                ++res.drawn;

                if (opt.border) {
                    QPen pen(QColor(120, 120, 120));
                    pen.setWidthF(std::max(1.0, 0.3 * mmPix));
                    g.setPen(pen); g.setBrush(Qt::NoBrush);
                    g.drawRect(target);
                }
            } else if (isz.width() > 0) {
                ++res.drawn;     // 只有尺寸信息没有图像数据:留白格,不算失败
            } else {
                ++res.failed;
            }
        }

        if (capBand > 0 && !capText.isEmpty()) {
            g.setFont(capFont);
            g.setPen(textColor);
            const QRectF band(cell.left(), cell.bottom() - capBand, cell.width(), capBand);
            g.drawText(band, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextDontClip,
                       QFontMetricsF(capFont).elidedText(capText, Qt::ElideMiddle,
                                                         int(cell.width())));
        }
    }
    g.restore();

    if (result) *result = res;
    return res.drawn;
}
