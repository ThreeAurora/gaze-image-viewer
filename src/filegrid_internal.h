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

// shell 图标画布去透明边:JUMBO 档取不到真 256 图时,Windows 会把小档图标
// 画在 256 画布的左上角、其余全透明(.lnk/exe 各档都有)——直接拿去等比
// 缩放,内容仍是"左上角一小块"。把不透明内容的边界框裁出来,原大小居中
// 回贴(小图标放缩填满只会糊);内容铺满/贴边的真图标原样返回。
// 与缩略图管线的 th_impl::trimPadCenter 同一口径的 QPixmap 版(QImage 实现,
// 不拖 windows.h 依赖)。
inline QPixmap trimPadCenter(QPixmap pm) {
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull() || !img.hasAlphaChannel()) return pm;
    int minX = img.width(), minY = img.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return pm;                        // 整幅全透明:交给调用方兜底
    const int cw = maxX - minX + 1, ch = maxY - minY + 1;
    if (minX == 0 && minY == 0
        && cw * 4 >= img.width() * 3 && ch * 4 >= img.height() * 3)
        return pm;                                  // 铺满/贴边:不动
    QPixmap out(img.size());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage((img.width() - cw) / 2, (img.height() - ch) / 2,
                img.copy(minX, minY, cw, ch));
    p.end();
    return out;
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
            if (m_g->m_swallowNextRelease) {
                // 双击的收尾松开:双击已经完成选中(并可能已进入子目录),
                // 这次松开若再按光标位置补选一次,就会覆盖掉新目录刚选好的
                // 左上角第一个 —— 正是用户报的"多余的一次选中"。
                m_g->m_swallowNextRelease = false;
            } else if (m_g->m_rubberActive) { m_g->endRubber(ev->pos()); }
            else if (i >= 0) m_g->onCanvasRelease(i);
        } else if (ev->button() == Qt::MiddleButton) {
            if (i >= 0) m_g->onCanvasMiddle(i);
        }
        QWidget::mouseReleaseEvent(ev);
    }
    void mouseDoubleClickEvent(QMouseEvent* ev) override {
        const int i = m_g->indexAt(ev->pos());
        if (i >= 0 && ev->button() == Qt::LeftButton) {
            // Qt 的双击序列是 press → release → dblclick → release(第二下按下被
            // dblclick 顶替)。后一个 release 与这记双击是同一轮操作,先立标记
            // 让它在 release 分支里被吞掉,不再按光标位置改一次选中。
            m_g->m_swallowNextRelease = true;
            m_g->onCanvasDblClick(i);
        }
        QWidget::mouseDoubleClickEvent(ev);
    }
    void mousePressEvent(QMouseEvent* ev) override {
        // 新的一次按下 = 新的一轮点击:清掉上一记双击留下的标记,免得
        // 一次真实单击的松开被误吞(双击与收尾松开之间不会有 press,清这里安全)
        m_g->m_swallowNextRelease = false;
        // 拖出起点:先让 FileGrid 记住按下位置与条目,移动够距离才发起拖拽,
        // 避免单击选中被误判成拖文件
        if (ev->button() == Qt::LeftButton) {
            const int i = m_g->indexAt(ev->pos());
            if (i < 0 && !(ev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                // #268 框选:从空白处按下 → 拖矩形框选;单点即空白单击(取消选择)
                m_g->beginRubber(ev->pos());
            } else {
                m_g->onCanvasPressStart(ev->pos());
            }
        }
        QWidget::mousePressEvent(ev);
    }
    void mouseMoveEvent(QMouseEvent* ev) override {
        // #268 框选拖动:框选期间不给悬停/起拖让路(悬停提示会盖住选框范围)
        if ((ev->buttons() & Qt::LeftButton) && m_g->m_rubberActive) {
            m_g->updateRubber(ev->pos());
            return;
        }
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
