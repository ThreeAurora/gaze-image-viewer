#pragma once
// FileGrid 结构性拆分后的内部头：原本写死在 filegrid.cpp 里、现被多个
// filegrid_*.cpp 共用的文件级实体 —— FileCanvas 画布类与卡片盒 helper。
// FileCanvas 保持全局作用域（filegrid.h 里是 friend class FileCanvas）。
// helper 必须保持 inline：原为匿名 namespace 函数（同 TU 内可内联），跨 TU 后
// 只有 inline 才能保住逐卡片绘制 / 缩略图预建这条热路径上的内联机会。
#include "filegrid.h"
#include <algorithm>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QToolTip>
#include <QStyle>
#include <QPolygon>
#include <QIcon>

namespace fg_impl {

// 标准图标按主题文字色染色(深=白,浅=黑):查找条上/下一页钮共用。
// filegrid_find.cpp 构建时与 filegrid.cpp 主题切换重染时都走这一份。
inline QIcon findStdIcon(QStyle* st, QStyle::StandardPixmap sp) {
    const QColor ink = QColor(QString::fromUtf8(Theme::T("#FFFFFF", "#1F1F26")));
    const QPixmap pm = st->standardIcon(sp).pixmap(20, 20);
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a) line[x] = ink.rgba() & 0x00FFFFFF | (a << 24);
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

// 自绘三角箭头:windowsvista 风格的 SP_ArrowUp/Down 标准图标取位图常为空,
// 染色后"看起来没有图标"。直接画实心三角,颜色随主题文字色(查找条翻页钮用)
inline QIcon paintedArrow(QStyle::StandardPixmap sp) {
    const QColor ink = QColor(QString::fromUtf8(Theme::T("#FFFFFF", "#1F1F26")));
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    QPolygon tri;
    if (sp == QStyle::SP_ArrowUp) tri << QPoint(8, 3) << QPoint(14, 12) << QPoint(2, 12);
    else                          tri << QPoint(2, 4)  << QPoint(14, 4)  << QPoint(8, 13);
    p.drawPolygon(tri);
    p.end();
    return QIcon(pm);
}

} // namespace fg_impl

// ═══════════════════════════════════════════
// 画布:整个列表只有这一个控件
// 滚动 = 移动画布 + 重绘与曝光区相交的卡片,不再有"每条目一个子控件"
// 的成本(旧实现在快速拖动滚动条时按跨行数线性重建卡片,perf.log 实测
// 单次 layoutCards 84–288ms,拖动越远越卡)
// ═══════════════════════════════════════════
class FileCanvas : public QWidget {
public:
    explicit FileCanvas(FileGrid* g) : QWidget(nullptr), m_g(g) {
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);   // 焦点交给 FileGrid(键盘导航)
    }

protected:
    // QScrollArea 只在画布"可见"时用 size(),否则退回 sizeHint()。
    // 默认 sizeHint 是无效值(-1x-1)→ 面板被查看器模式/关闭面板藏起来时,
    // 量程算成 0 并停在旧值上:换个文件夹后就能往下拖出一大截空白。
    // 画布尺寸由 rebuildGeometry 定死,报 size() 即精确。
    QSize sizeHint() const override { return size(); }

    void paintEvent(QPaintEvent* ev) override {
        QPainter p(this);
        m_g->paintCanvas(p, ev->rect());
    }
    void mouseReleaseEvent(QMouseEvent* ev) override {
        const int i = m_g->indexAt(ev->pos());
        if (ev->button() == Qt::LeftButton) {
            if (i >= 0) m_g->onCanvasRelease(i);
        } else if (ev->button() == Qt::MiddleButton) {
            if (i >= 0) m_g->onCanvasMiddle(i);
        }
        QWidget::mouseReleaseEvent(ev);
    }
    void mouseDoubleClickEvent(QMouseEvent* ev) override {
        const int i = m_g->indexAt(ev->pos());
        if (i >= 0 && ev->button() == Qt::LeftButton) m_g->onCanvasDblClick(i);
        QWidget::mouseDoubleClickEvent(ev);
    }
    void mousePressEvent(QMouseEvent* ev) override {
        // 拖出起点:先让 FileGrid 记住按下位置与条目,移动够距离才发起拖拽,
        // 避免单击选中被误判成拖文件
        if (ev->button() == Qt::LeftButton) m_g->onCanvasPressStart(ev->pos());
        QWidget::mousePressEvent(ev);
    }
    void mouseMoveEvent(QMouseEvent* ev) override {
        m_g->setHovered(m_g->indexAt(ev->pos()));
        // 按住左键移动超过阈值 → 发起文件拖拽(复制语义,可拖到资源管理器/别的程序)
        if ((ev->buttons() & Qt::LeftButton) && m_g->maybeStartDrag(ev->pos())) return;
        QWidget::mouseMoveEvent(ev);
    }
    void leaveEvent(QEvent* ev) override {
        m_g->setHovered(-1);
        QWidget::leaveEvent(ev);
    }
    void contextMenuEvent(QContextMenuEvent* ev) override {
        m_g->onCanvasMenu(m_g->indexAt(ev->pos()), ev->globalPos());
    }
    bool event(QEvent* ev) override {
        if (ev->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(ev);
            const int i = m_g->indexAt(he->pos());
            if (i < 0) { QToolTip::hideText(); return true; }
            QToolTip::showText(he->globalPos(), m_g->tipFor(i), this);
            return true;
        }
        return QWidget::event(ev);
    }

private:
    FileGrid* m_g;
};


namespace fg_impl {

// 卡片内各元素矩形(数值与原 FileCard::setup 逐模式对齐)
struct CardBoxes {
    QRect img, name, detail;
    bool  cover = false;   // 瀑布流:缩略图填满
};

inline CardBoxes boxesFor(const QRect& r, int mode, int labelGap) {
    CardBoxes b;
    const int w = r.width();
    switch (mode) {
    // 三种缩略图视图:框高 = 卡片高 - chromeFor(...)（Appearance/customThumbH 生效点）
    case VM_THUMBS:
        b.img = QRect(r.x() + 2, r.y() + 2, w - 4,
                      std::max(8, r.height() - FileGrid::chromeFor(VM_THUMBS, labelGap)));
        break;
    case VM_THUMBS_NAME:
    case VM_THUMBS_LABEL: {
        const int ih = std::max(8, r.height() - FileGrid::chromeFor(VM_THUMBS_NAME, labelGap));
        b.img  = QRect(r.x() + 4, r.y() + 4, w - 8, ih);
        b.name = QRect(r.x() + 2, b.img.bottom() + labelGap, w - 4, 18);
        break;
    }
    case VM_THUMBS_DETAIL: {
        const int ih = std::max(8, r.height() - FileGrid::chromeFor(VM_THUMBS_DETAIL, labelGap));
        b.img    = QRect(r.x() + 4, r.y() + 4, w - 8, ih);
        b.name   = QRect(r.x() + 2, b.img.bottom() + labelGap, w - 4, 18);
        b.detail = QRect(r.x() + 2, b.name.bottom() + 1, w - 4, 16);
        break;
    }
    case VM_ICONS:
        b.img  = QRect(r.x() + 4, r.y() + 4, 80, 80);
        b.name = QRect(r.x() + 2, r.y() + 86, 84, 18);
        break;
    case VM_LIST:
        b.img  = QRect(r.x() + 3, r.y() + 3, 18, 18);
        b.name = QRect(r.x() + 26, r.y() + 3, w - 30, 18);
        break;
    case VM_DETAILS:
        b.img    = QRect(r.x() + 3, r.y() + 3, 20, 20);
        b.name   = QRect(r.x() + 28, r.y() + 4, static_cast<int>(w * 0.38), 18);
        b.detail = QRect(r.x() + static_cast<int>(w * 0.40), r.y() + 5,
                         static_cast<int>(w * 0.58), 16);
        break;
    default:                                  // VM_WATERFALL
        b.img   = QRect(r.x() + 2, r.y() + 2, w - 4, r.height() - 4);
        b.cover = true;
        break;
    }
    return b;
}

} // namespace fg_impl
