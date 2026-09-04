#include "filegrid.h"
#include "contextmenu.h"
#include "thumbnailer.h"
#include "livephoto.h"
#include "labelstore.h"
#include "settings.h"
#include "constants.h"
#include "shelldelete.h"
#include "clipboardops.h"
#include "validname.h"
#include "exifdate.h"
#include "namesort.h"
#include "perflog.h"
#include "logger.h"

#include <set>
#include <algorithm>
#include <numeric>
#include <memory>
#include <array>

#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QScrollBar>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QToolTip>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QCoreApplication>
#include <QThreadPool>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QApplication>
#include <QProcess>
#include <QPainter>
#include <QPainterPath>
#include <QCollator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include "filegrid_internal.h"

void FileGrid::wheelEvent(QWheelEvent* event) {
    PerfLog::Scope _perf("wheelEvent", 50);
    // 滚轮固定滚动一行(定死);若当前首排是"半截"的(拖过滚动条),
    // 第一次滚动先对齐:把该排贴到文件页顶端完整显示,下次滚动才正常滚一行
    if (m_entries.empty() || m_cols < 1) { event->accept(); return; }
    // 缩略图请求由 valueChanged 统一合并处理,滚轮同样会改滚动条值,这里不再另开路径
    int rowH = cardH(0) + m_spacing;
    if (rowH <= 0) { event->accept(); return; }
    int vpTop = verticalScrollBar()->value();
    int maxV = verticalScrollBar()->maximum();
    int delta = event->angleDelta().y();
    if (delta == 0) { event->accept(); return; }
    if (vpTop <= 0 && delta > 0) { event->accept(); return; }   // 已在顶部
    if (vpTop >= maxV && delta < 0) { event->accept(); return; } // 已在底部:不抽搐

    bool aligned = (vpTop % rowH == 0);
    if (!aligned) {
        // 对齐规则按滚动方向区分:
        // 往上滚 → 首排贴顶(向上对齐行边界)。原逻辑无方向区分,贴底值 maxV
        //   通常不是 rowH 整数倍,到底后上滚仍被"贴底"吞掉——表现为
        //   "滚到底后不能往上滚"(用户反馈)
        // 往下滚 → 底部余量不足一排则末排贴底(修末排被顶出底部),否则下一行贴顶
        int vpH = viewport()->height();
        int firstRow = vpTop / rowH;
        int lastRow = (static_cast<int>(m_entries.size()) - 1) / m_cols;
        int totalH = (lastRow + 1) * rowH;
        int bottomRemain = totalH - (vpTop + vpH);
        int target;
        if (delta > 0)
            target = qBound(0, firstRow * rowH, maxV);
        else
            target = bottomRemain < rowH
                ? maxV                                     // 末排贴底
                : qBound(0, (firstRow + 1) * rowH, maxV);  // 下一行贴顶
        if (target != vpTop)
            verticalScrollBar()->setValue(target);
        event->accept();
        return;
    }
    int steps = delta > 0 ? -1 : 1;
    verticalScrollBar()->setValue(qBound(0, vpTop + steps * rowH, maxV));
    event->accept();
}

// ═══════════════════════════════════════════
// 布局计算
// ═══════════════════════════════════════════
void FileGrid::setFixedCols(int n) {
    m_fixedCols = qBound(0, n, 16);
    AppSettings::instance().set("Browser/fixedCols", m_fixedCols);   // 持久化
    if (m_fixedCols == 0)
        m_cardSize = m_cardSizeAuto;   // 恢复自动:尺寸回到 slider 设定值
    updateLayout();
    requestVisibleThumbs();
}

void FileGrid::setViewMode(int mode) {
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    m_cols = 0;   // 强制重算列数
    AppSettings::instance().set("Browser/viewMode", m_viewMode);     // 持久化
    // #266 切换按钮:记住"缩略图侧"模式,详细↔缩略图来回切都回到它
    if (mode != VM_DETAILS && mode != VM_LIST)
        AppSettings::instance().setPersist("Browser/lastThumbMode", m_viewMode);
    m_fitCache.clear();   // 成品图按盒子缓存,查看方式换了盒子形状不同
    if (m_header) { m_header->setDetailMode(m_viewMode == VM_DETAILS); updateDetailColumns(); }
    updateLayout();
    requestVisibleThumbs();
    emit viewModeChanged(m_viewMode);
}

// ── 模式化卡片尺寸 ──
int FileGrid::cardW() const {
    switch (m_viewMode) {
    case VM_ICONS:      return 88;
    case VM_LIST:
    case VM_DETAILS:    return viewport()->width() - 20;
    case VM_WATERFALL:  return m_waterfallColW;
    default:              return m_cardSize;
    }
}

// 缩略图框以外的固定高度(内边距 + 文件名行 + 详细行),与 boxesFor 严格互为逆运算:
// cardH = 缩略图框高 + chromeFor(...)。改一处必须改另一处,否则卡片会错位/裁切。
int FileGrid::chromeFor(int mode, int labelGap) {
    switch (mode) {
    case VM_THUMBS:        return 8;
    case VM_THUMBS_NAME:
    case VM_THUMBS_LABEL:  return 24 + labelGap;
    case VM_THUMBS_DETAIL: return 44 + labelGap;
    default:               return 0;
    }
}

// Appearance/customThumbH:0=与宽同高(接线前既有行为);>0=按设置值
int FileGrid::thumbBoxH() const {
    if (m_thumbH > 0) return m_thumbH;
    return (m_viewMode == VM_THUMBS) ? m_cardSize - 4 : m_cardSize - 8;
}

int FileGrid::cardH(int idx) const {
    switch (m_viewMode) {
    case VM_THUMBS:        return thumbBoxH() + chromeFor(VM_THUMBS, m_labelGap);
    case VM_THUMBS_NAME:
    case VM_THUMBS_LABEL:  return thumbBoxH() + chromeFor(VM_THUMBS_NAME, m_labelGap);
    case VM_THUMBS_DETAIL: return thumbBoxH() + chromeFor(VM_THUMBS_DETAIL, m_labelGap);
    case VM_ICONS:         return 106;
    case VM_LIST:          return 26;
    case VM_DETAILS:       return 28;
    case VM_WATERFALL: {
        if (idx < 0 || idx >= static_cast<int>(m_entries.size()))
            return m_waterfallColW;
        const auto& e = m_entries[idx];
        QSize s = e.isDir ? QSize(3, 4) : imageSize(e.path);
        if (s.width() <= 0) s = QSize(3, 4);
        return m_waterfallColW * s.height() / std::max(1, s.width()) + 8;
    }
    }
    return m_cardSize + 22;
}

int FileGrid::colsForWidth(int w) const {
    if (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS) return 1;
    if (m_viewMode == VM_WATERFALL)
        return std::max(1, (w - MARGIN * 2 + m_spacing) / (m_waterfallColW + m_spacing));
    if (m_fixedCols > 0) return m_fixedCols;   // 手动列数:缩放时只缩放不换列
    int usable = w - MARGIN * 2;
    if (usable <= 0) return 1;
    return std::max(1, (usable + m_spacing) / (m_cardSize + m_spacing));
}

void FileGrid::updateLayout() {
    // #216:构造期视口宽是假的(splitter/窗口几何 show 后才定),提前算必错——
    // 不可见时只标脏;首个可见态调用(30ms 合并定时器/文件夹加载先到者)放行,
    // 此刻 splitter 分配已定,首算即贴合文件页,消除启动"先窄/先宽再调整"的跳变
    if (!m_layoutReady) {
        if (!isVisible()) { m_geomDirty = true; return; }
        m_layoutReady = true;
    }
    int vw = viewport()->width();
    // 固定列数模式:缩略图贴边缩放——尺寸 = 可用宽度 ÷ 列数(列数不变)
    if (m_fixedCols > 0 && m_viewMode != VM_WATERFALL
        && m_viewMode != VM_LIST && m_viewMode != VM_DETAILS) {
        m_cols = m_fixedCols;
        int usable = vw - MARGIN * 2 - (m_cols - 1) * m_spacing;
        m_cardSize = qBound(48, usable / m_cols, 2048);
    } else {
        m_cols = colsForWidth(vw);
    }
    // 画布尺寸与卡片矩形在 rebuildGeometry 里一次算完(同源);
    // 这里再算一遍总高就会与几何脱节 → 出现拖不到边的空白尾巴
    rebuildGeometry();
    m_geomDirty = false;
    refreshView();
    // 详细列表:列对齐几何随视口重推;EXIF 列后台预填(缺失项到达即定点重绘)
    if (m_viewMode == VM_DETAILS) {
        updateDetailColumns();
        exifPrefillVisible();
    }
    // 搜索条浮在视口上,列表重建时命中统计可能整体过时(当前项被筛掉/增删)
    if (m_findBar && m_findBar->isVisible()) findRefresh();
}

// 只重绘:自绘模式下"数据/选择/外观变了"从不需要重排任何控件
void FileGrid::refreshView() {
    if (m_canvas) m_canvas->update();
}

// 结构变化(列数/尺寸/查看方式/条目)后一次算完:
// ① 每张卡片矩形 ② 内容总高 → 画布尺寸 ③ 按顶边排序的绘制/命中索引
// 之后滚动、悬停、命中、绘制都只是查表
void FileGrid::rebuildGeometry() {
    const int n = static_cast<int>(m_entries.size());
    m_geom.assign(n, QRect());
    m_byY.clear();
    m_maxCardH = 0;
    if (n == 0 || m_cols < 1) {
        m_canvas->setFixedSize(viewport()->width(), viewport()->height());
        return;
    }

    int bottom = 0;   // 内容真实底边(不含尾部间距)
    if (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS) {
        const int rowH = cardH(0) + 2;
        const int h = cardH(0);
        for (int i = 0; i < n; ++i)
            m_geom[i] = QRect(MARGIN, i * rowH, cardW(), h);
        bottom = (n - 1) * rowH + h;
        m_maxCardH = h;
    } else if (m_viewMode == VM_WATERFALL) {
        std::vector<int> colH(m_cols, 0);
        for (int i = 0; i < n; ++i) {
            int c = 0;
            for (int k = 1; k < m_cols; ++k)
                if (colH[k] < colH[c]) c = k;
            const int h = cardH(i);
            m_geom[i] = QRect(MARGIN + c * (m_waterfallColW + m_spacing), colH[c],
                              m_waterfallColW, h);
            colH[c] += h + m_spacing;
        }
        for (const QRect& r : m_geom) {
            bottom = std::max(bottom, r.bottom() + 1);
            m_maxCardH = std::max(m_maxCardH, r.height());
        }
    } else {
        const int cw = cardW();
        const int ch = cardH(0);   // 非瀑布流各卡片等高,不必逐条问
        for (int i = 0; i < n; ++i)
            m_geom[i] = QRect(MARGIN + (i % m_cols) * (cw + m_spacing),
                              (i / m_cols) * (ch + m_spacing), cw, ch);
        bottom = ((n - 1) / m_cols) * (ch + m_spacing) + ch;
        m_maxCardH = ch;
    }

    const int contentW = (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS)
        ? cardW() + MARGIN * 2
        : (m_viewMode == VM_WATERFALL
            ? m_cols * (m_waterfallColW + m_spacing) + MARGIN * 2
            : m_cols * (cardW() + m_spacing) - m_spacing + MARGIN * 2);
    m_canvas->setFixedSize(std::max(contentW, viewport()->width()),
                           std::max(bottom + MARGIN, viewport()->height()));

    // 顶边升序:绘制与命中都只扫视口窗口内的少量条目
    m_byY.resize(n);
    std::iota(m_byY.begin(), m_byY.end(), 0);
    std::sort(m_byY.begin(), m_byY.end(), [this](int a, int b) {
        return m_geom[a].top() < m_geom[b].top();
    });
}

// 极少数路径改了条目却没走 updateLayout:绘制/命中前补一次
void FileGrid::ensureGeometry() {
    if (!m_geomDirty) return;
    m_geomDirty = false;
    rebuildGeometry();
}

// ── #267 详细列表:定宽列与表头同源对齐 ──
int FileGrid::detailColW(int i) const {
    const int w = SortHeader::detailColWidth(i);
    if (!m_header || w <= 0) return w;
    return m_header->detailColumnVisible(i) ? w : 0;
}

// 行矩形内自右向左第 i 列的左缘:隐藏列宽 0 自然压缩,列区恒贴行右缘
int FileGrid::detailColX(const QRect& r, int i) const {
    int x = r.right() + 1;
    for (int k = 5; k >= i; --k) x -= detailColW(k);
    return x;
}

// 把详细态列区两端的对齐垫片推给表头:
//   lead = 名称文字起点对齐(28 名称偏移 + 8 MARGIN + 8 文字内缩 − 6 表头边距 − 8 按钮内边距)
//   tail = 列区右缘吸到网格行右缘(差值随滚动条显隐/窗口宽变化,每次重推)
void FileGrid::updateDetailColumns() {
    if (!m_header || m_viewMode != VM_DETAILS) return;
    m_header->setDetailLead(viewport()->x() + 30);
    const int rowRight = viewport()->x() + MARGIN + cardW();
    m_header->setDetailTail(rowRight - (m_header->width() - 6));
}

// 表头挂接:#107 以来表头是 MainWindow 布局里的兄弟控件,网格持有指针反向驱动。
// 右键配置列 → detailColumnsEdited → 重推垫片 + 重绘(显隐改变列文本位置)
void FileGrid::setSortHeader(SortHeader* h) {
    m_header = h;
    if (!h) return;
    connect(h, &SortHeader::detailColumnsEdited, this, [this]() {
        updateDetailColumns();
        refreshView();
    });
    // 启动即恢复详细态(Browser/viewMode):表头构造期后才挂接,这里补形态
    if (m_viewMode == VM_DETAILS) {
        h->setDetailMode(true);
        updateDetailColumns();
    }
}

// 二分:返回第一个顶边 >= y 的窗口下标
int FileGrid::lowerBoundRow(int y) const {
    int lo = 0, hi = static_cast<int>(m_byY.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (m_geom[m_byY[mid]].top() < y) lo = mid + 1; else hi = mid;
    }
    return lo;
}
// ═══════════════════════════════════════════
// 缩略图:可见行提交 + 到达后定点重绘
// ═══════════════════════════════════════════
void FileGrid::requestVisibleThumbs() {
    if (m_entries.empty() || !m_canvas) return;
    ensureGeometry();
    // #267:详细态走同一合并/滚动/缩放入口,只是服务内容换成 EXIF 列预填
    if (m_viewMode == VM_DETAILS) { exifPrefillVisible(); return; }
    const QRect vis(0, verticalScrollBar()->value(),
                    viewport()->width(), viewport()->height());
    int thumbW = cardW() - 14;
    if (m_viewMode == VM_WATERFALL)                          thumbW = m_waterfallColW;
    else if (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS) thumbW = 64;

    // enqueue() 内部每次都要 QFileInfo 读盘(exists + lastModified),
    // 所以手里已有解码图的条目绝不重复入队
    const int from = lowerBoundRow(vis.top() - m_maxCardH);
    int toBuild = 40;   // 每轮预建上限:合成上百张成品图会当场卡掉一帧
    for (int k = from; k < static_cast<int>(m_byY.size()); ++k) {
        const int i = m_byY[k];
        const QRect& r = m_geom[i];
        if (r.top() > vis.bottom()) break;
        if (r.isNull() || !r.intersects(vis)) continue;
        const FileEntry& e = m_entries[i];
        const bool isVideo = VIDEO_EXTS.count(e.ext) > 0;
        // 目录:Thumbs/folder4 决定拼 4 张还是单张封面(缩略图由 Thumbnailer 生成);
        // 目录内没有可用图片时 thumbnailer 返回空,卡片照旧回落系统文件夹图标
        if (!e.isDir && !isVideo && IMAGE_EXTS.count(e.ext) == 0) continue;

        const QPixmap raw = m_thumbCache.value(e.path);
        if (raw.isNull() || raw.width() < thumbW) {
            // 没解码过,或手里的图比当前盒子小(卡片被放大过):按当前宽重排。
            // 否则旧小图会被成品图管线拉伸放大——"缩略图发糊"的来源之一
            Thumbnailer::instance().enqueue(e.path, thumbW, isVideo);
            continue;                       // 解码到达时由 onThumbReady 预建
        }
        if (toBuild <= 0) continue;
        const fg_impl::CardBoxes bx = fg_impl::boxesFor(r, m_viewMode, m_labelGap);
        if (!bx.img.isEmpty()) {
            // 不带 contains 守卫:盒子变过(窗口缩放/间距调整)后缓存里是旧盒子的
            // 成品图,守卫会把它挡成"已建",fitFor 永远 miss,绘制永远走兜底
            buildFit(e.path, bx.img, bx.cover);   // 盒子没变时内部直接返回
            --toBuild;
        }
    }
}

// Thumbs/wholeFolder:为当前目录全部条目排缩略图(不限视口)
void FileGrid::requestAllThumbs() {
    if (m_entries.empty() || !m_canvas) return;
    if (m_viewMode == VM_DETAILS) return;   // #267:详情行用类型图标,无需解码
    ensureGeometry();
    int thumbW = cardW() - 14;
    if (m_viewMode == VM_WATERFALL)                             thumbW = m_waterfallColW;
    else if (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS) thumbW = 64;
    for (const FileEntry& e : m_entries) {
        if (m_thumbCache.value(e.path).width() >= thumbW) continue;
        const bool isVideo = VIDEO_EXTS.count(e.ext) > 0;
        if (!e.isDir && !isVideo && IMAGE_EXTS.count(e.ext) == 0) continue;
        Thumbnailer::instance().enqueue(e.path, thumbW, isVideo);
    }
}

void FileGrid::onThumbReady(const QString& filePath, const QImage& img) {
    PerfLog::Scope _perf("onThumbReady", 50);
    // worker 线程传来 QImage（线程安全）；转 QPixmap 必须在 GUI 线程完成
    const QPixmap pix = QPixmap::fromImage(img);
    // FIFO 逐出:缓存条数封顶,防止浏览大量文件后内存无限膨胀
    if (!m_thumbCache.contains(filePath)) m_thumbOrder.enqueue(filePath);
    m_thumbCache[filePath] = pix;
    m_fitCache.remove(filePath);   // 原图换了尺寸档位 → 成品图作废
    while (m_thumbOrder.size() > 800) {
        const QString old = m_thumbOrder.dequeue();
        m_thumbCache.remove(old);
        m_fitCache.remove(old);
    }
    const int i = m_pathRow.value(filePath, -1);
    if (i < 0) return;
    ensureGeometry();   // 表头排序后异步重绘未 flush 前到达的缩略图,别按旧几何算脏矩形
    const QRect r = cardRect(i);
    // 顺手预建成品图:重采样摊到解码到达这条本就分散的时间轴上,
    // 不留到拖动中的某一帧去做
    if (!r.isNull()) {
        const fg_impl::CardBoxes bx = fg_impl::boxesFor(r, m_viewMode, m_labelGap);
        if (!bx.img.isEmpty()) buildFit(filePath, bx.img, bx.cover);
    }
    m_canvas->update(r);
}

void FileGrid::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    // 实时响应:30ms 合并洪流后立即重布局(固定列数拖宽时缩略图即时贴边缩放)
    m_resizeTimer.start(30);
    m_reEnqueueTimer.start(100);   // 尺寸稳定后再生高清缩略图
    if (m_findBar) placeFindBar(); // 搜索条锚在视口右上角,跟随宽度
}

