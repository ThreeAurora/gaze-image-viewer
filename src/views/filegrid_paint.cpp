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


// ═══════════════════════════════════════════
// 自绘:paintEvent 只画与曝光区相交的卡片
// 滚动 = 移动画布 + 重绘,开销与"拖动距离"无关,只与帧率有关
// ═══════════════════════════════════════════
namespace {

Qt::AlignmentFlag alignFlag(int v) {
    return v == 0 ? Qt::AlignLeft : (v == 2 ? Qt::AlignRight : Qt::AlignHCenter);
}

// 图片在盒内的实际显示矩形(选中框贴它绘制,与原 m_thumbRect 同式)
QRect fittedRect(const QSize& src, const QRect& box, bool cover, int imageAlign) {
    if (src.isEmpty() || box.isEmpty()) return box;
    const QSize s = src.scaled(box.size(),
        cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
    const int offX = imageAlign == 0 ? 0
                   : imageAlign == 2 ? box.width() - s.width()
                                     : (box.width() - s.width()) / 2;
    return QRect(box.x() + offX, box.y() + (box.height() - s.height()) / 2,
                 s.width(), s.height());
}

} // namespace

void FileGrid::paintCanvas(QPainter& p, const QRect& clipIn) {
    PerfLog::Scope _perf("paintCanvas", 16);
    ensureGeometry();
    if (m_entries.empty() || !m_canvas || m_byY.empty()) {
        // #6 异步装载:首次启动/进大目录的扫描期画一行"正在读取目录…"占位,
        // 不再是一块死灰的四边形(旧观感:等几秒什么都没有)
        if (m_loading) {
            QFont f = p.font();
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(QColor(QString::fromUtf8(C_TEXT_FAINT)));
            p.drawText(m_canvas->rect(), Qt::AlignCenter,
                       gazeTr("正在读取目录…"));
        }
        return;
    }
    const QRect clip = clipIn.isNull() ? m_canvas->rect() : clipIn;
    p.setRenderHint(QPainter::Antialiasing);
    // 只遍历曝光窗口:起点 = 顶边 >= clip.top - 最高卡片 的第一个,终点 = 顶边越过 clip 底边
    const int from = lowerBoundRow(clip.top() - m_maxCardH);
    for (int k = from; k < static_cast<int>(m_byY.size()); ++k) {
        const int i = m_byY[k];
        const QRect& r = m_geom[i];
        if (r.top() > clip.bottom()) break;
        if (r.isNull() || !r.intersects(clip)) continue;
        paintCard(p, i, r);
    }

    // #268 框选:松开前把半透明蓝框画在卡片之上,让"将要选中谁"一眼可见。
    // 点在空白处未拖成矩形时不画(空手单击=取消选择,画个点反而奇怪)
    if (m_rubberActive) {
        const QRect rub = QRect(m_rubberStart, m_rubberCur).normalized();
        if (rub.width() >= 3 && rub.height() >= 3) {
            p.fillRect(rub, QColor(0, 120, 215, 36));
            p.setPen(QPen(QColor(0, 120, 215, 210), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rub);
        }
    }
}

void FileGrid::paintCard(QPainter& p, int idx, const QRect& r) {
    // #267 详细列表走专属行绘制(整行选中/定宽列),不再借缩略图卡片形态
    if (m_viewMode == VM_DETAILS) { paintDetailsRow(p, idx, r); return; }
    const FileEntry& e = m_entries[idx];
    const fg_impl::CardBoxes bx = fg_impl::boxesFor(r, m_viewMode, m_labelGap);
    const QFont  base  = p.font();
    const bool   sel   = m_selected.contains(idx);
    // 2026-09-02 用户令:选中色随焦点分流 —— 网格有焦点 = 真选中(亮蓝 rgb(0,120,215)),
    // 焦点在文件树时网格的"选中"只是视觉残留(暗蓝 rgb(33,100,168)),让人一眼看出
    // F2 重命名的到底是谁(改谁看焦点,文件树和文件页两套选中态)。
    const QColor selBlue = hasFocus()
        ? QColor(0, 120, 215)   // #0078D7 亮蓝:当前正被操作
        : QColor(33, 100, 168); // #2164A8 暗蓝:仅视觉残留,并未被选中
    // #104:多选不再换颜色(黄框与单击蓝框不一致是用户明确否掉的)。
    // 多选时"键盘当前落点"改画一条内侧焦点细线(只在网格真有焦点时),
    // 颜色一律走单选那套蓝,否则整个选中态就没任何可见信号了
    const bool   anchor = sel && idx == m_lastClicked && m_selected.size() > 1;

    // ── 图像:成品缩略图优先,未解码则用缓存的占位图标(名称始终先显示) ──
    // 目录同样吃这条管线:Thumbs/folder4 生成的内容 2x2 拼图就是它的卡片图
    // (XnView 同款)。目录内没有可用图片时 Thumbnailer 返回空 → 落回文件夹图标。
    QRect imgR = bx.img;
    const QPixmap raw = m_thumbCache.value(e.path);
    if (!raw.isNull()) {
        const QPixmap fit = fitFor(e.path, bx.img);
        if (fit.isNull()) {
            // 尚未预建(刚进视口/盒子变了未重建):按 fittedRect 同一几何快速最近邻
            // 缩放绘制。不能平滑缩放(拖尾感主因),也不能左上裁切——raw 与盒子
            // 宽高比不同时,裁切画出的图和边框对不上(边框看着"偏小")
            const QSize s = raw.size().scaled(bx.img.size(),
                bx.cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            const int offX = m_imageAlign == 0 ? 0
                           : m_imageAlign == 2 ? bx.img.width() - s.width()
                                               : (bx.img.width() - s.width()) / 2;
            const QRect dst(bx.img.x() + offX,
                            bx.img.y() + (bx.img.height() - s.height()) / 2,
                            s.width(), s.height());
            QPixmap draw = raw;
            if (s != raw.size())
                draw = raw.scaled(s, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            const QRect vis = dst.intersected(bx.img);   // cover 模式溢出部分裁掉
            if (!vis.isEmpty())
                p.drawPixmap(vis.topLeft(), draw,
                             QRect(vis.topLeft() - dst.topLeft(), vis.size()));
        } else {
            p.drawPixmap(bx.img.topLeft(), fit);
        }
        imgR = fittedRect(raw.size(), bx.img, bx.cover, m_imageAlign)
                   .intersected(bx.img);   // 瀑布流 cover 溢出被裁,框贴可见区
    } else {
        if (e.hidden) p.setOpacity(0.45);
        p.drawPixmap(bx.img.topLeft(), iconPixmap(e, bx.img.width()));
        if (e.hidden) p.setOpacity(1.0);
    }

    // ── 文件名:格式标签色底块 + 居中/左对齐文字(中间省略) ──
    if (!bx.name.isNull()) {
        QColor bg;
        if (sel)        bg = selBlue;
        else if (LabelColors::enabled()) bg = LabelColors::colorForExt(e.ext.mid(1));
        if (bg.alpha() > 0) p.fillRect(bx.name, bg);
        // 选中=蓝底白字;未选中=主题文字色(浅色档下白字在白卡上会消失)
        p.setPen(QColor(sel ? QStringLiteral("#FFFFFF")
                            : QString::fromUtf8(e.hidden ? C_TEXT_HIDDEN : C_TEXT)));
        const Qt::Alignment al =
            (m_viewMode == VM_LIST || m_viewMode == VM_DETAILS)
                ? (Qt::AlignLeft | Qt::AlignVCenter)
                : (alignFlag(m_labelAlign) | Qt::AlignVCenter);
        p.drawText(bx.name, al,
                   QFontMetrics(base).elidedText(e.name, Qt::ElideMiddle,
                                                 bx.name.width() - 6));
    }

    // ── 详细行:大小 [+ 类型] + 修改时间 ──
    if (!bx.detail.isNull()) {
        QFont f = base;
        f.setPixelSize(m_viewMode == VM_DETAILS ? 11 : 10);
        p.setFont(f);
        p.setPen(e.hidden ? QColor(C_TEXT_HIDDEN) : QColor(C_TEXT));
        const QString sz = entrySizeText(e);   // #10:目录悬停统计后不再恒 0KB
        const QString date = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(e.mtime)).toString("yyyy/M/d HH:mm");
        const QString txt = m_viewMode == VM_DETAILS
            ? QString("%1    %2    %3").arg(sz, -12).arg(mimeType(e.ext), -14).arg(date)
            : sz + "  " + date;
        p.drawText(bx.detail,
                   (m_viewMode == VM_DETAILS ? Qt::AlignLeft : alignFlag(m_labelAlign))
                       | Qt::AlignVCenter, txt);
        p.setFont(base);
    }

    // ── 选中框 / 悬停白描边:紧贴图片实际显示区 ──
    // 2px 笔宽以路径为中心:整数矩形外扩 2 会让描边外浮 3px、且与图之间
    // 留 1px 缝。QRectF 外扩 1 恰好内缘贴住图像、外缘突出 2px,四边对称
    const QRectF hug(imgR.left() - 1.0, imgR.top() - 1.0,
                     imgR.width() + 2.0, imgR.height() + 2.0);
    if (sel) {
        p.setPen(QPen(selBlue, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(hug);
        // 键盘落点:多选时框颜色已与单选一致,只能靠这条内侧虚线指出"方向键在这儿"。
        // 焦点不在网格上就不画 —— 画了反而是个假指示器
        if (anchor && hasFocus()) {
            p.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::DotLine));
            p.drawRect(hug.adjusted(2.5, 2.5, -2.5, -2.5));
        }
    } else if (idx == m_hoverIdx && e.colorLabel == 0) {
        p.setPen(QPen(QColor(255, 255, 255, 220), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(hug);
    }

    // ── Appearance/borderSize 卡片边框(#248 浅色档给浅灰,深色逐字保留) ──
    if (m_border > 0) {
        p.setPen(QPen(QColor(Theme::T("#3A3A42", "#D9D9E0")), m_border));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(r.x() + m_border / 2.0, r.y() + m_border / 2.0,
                          r.width() - m_border, r.height() - m_border));
    }

    // ── 颜色标记圆圈(Browser/showRating 关时不画) ──
    if (m_showRating && e.colorLabel > 0) {
        QColor c = LabelStore::colorValue(e.colorLabel);
        if (c.isValid()) {
            p.setPen(QPen(QColor("#FFFFFF"), 1.5));
            p.setBrush(c);
            p.drawEllipse(imgR.topLeft() + QPointF(9, 9), 7, 7);
        }
    }
}

// ── #267 详细列表行:整行选中/悬停 + 名称纯文本 + 右锚定定宽列(XnView 形态)。
// 与缩略图卡片形态的区别:不 hug 图片框、不画格式标签底块,列文本与排序表头
// 逐像素对齐(updateDetailColumns 推送的 lead/尾垫片保证两处同源) ──
void FileGrid::paintDetailsRow(QPainter& p, int idx, const QRect& r) {
    const FileEntry& e = m_entries[idx];
    const bool   sel   = m_selected.contains(idx);
    // 选中色随焦点分流(与缩略图模式同一规则)
    const QColor selBlue = hasFocus() ? QColor(0, 120, 215) : QColor(33, 100, 168);

    // 整行选中/悬停底色
    if (sel)
        p.fillRect(r, selBlue);
    else if (idx == m_hoverIdx)
        p.fillRect(r, QColor(QString::fromUtf8(C_CARD_HOVER)));

    // 小图标:详情列表不进缩略图管线,直接类型/文件夹图标(20px,带缓存)
    const fg_impl::CardBoxes bx = fg_impl::boxesFor(r, m_viewMode, m_labelGap);
    if (e.hidden) p.setOpacity(0.45);
    p.drawPixmap(bx.img.topLeft(), iconPixmap(e, bx.img.width()));
    if (e.hidden) p.setOpacity(1.0);

    // 颜色标记圆点:放行尾,与图标列互不打架
    if (m_showRating && e.colorLabel > 0) {
        QColor c = LabelStore::colorValue(e.colorLabel);
        if (c.isValid()) {
            p.setPen(QPen(QColor("#FFFFFF"), 1.5));
            p.setBrush(c);
            p.drawEllipse(QPointF(r.right() - 12.0, r.center().y()), 5, 5);
        }
    }

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);

    // 名称:纯文本(无底块),超宽右省略;选中=白字,隐藏=淡灰
    const int nameX = r.x() + 28;
    const int col0  = detailColX(r, 0);          // 第一可见列左缘;全部隐藏=r.right()+1
    const int nameW = qMax(0, col0 - 6 - nameX);
    p.setPen(sel ? QColor(QStringLiteral("#FFFFFF"))
                 : QColor(QString::fromUtf8(e.hidden ? C_TEXT_HIDDEN : C_TEXT)));
    p.drawText(QRect(nameX, r.y(), nameW, r.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(f).elidedText(e.name, Qt::ElideRight, nameW));

    // 定宽列文本(全部隐藏则整段跳过)
    if (col0 <= r.right()) {
        p.setPen(e.hidden ? QColor(QString::fromUtf8(C_TEXT_HIDDEN))
                          : QColor(QString::fromUtf8(C_TEXT)));
        const QString dateFmt = QStringLiteral("yyyy/M/d HH:mm");
        for (int i = 0; i < 6; ++i) {
            const int w = detailColW(i);
            if (w <= 0) continue;
            const int x = detailColX(r, i);
            QString txt;
            switch (i) {
            case 0:
                txt = entrySizeText(e);   // #10:目录=统计值/统计中…
                break;
            case 1: txt = e.isDir ? gazeTr("文件夹") : mimeType(e.ext); break;
            case 2: txt = e.ext.isEmpty() ? QString() : e.ext.mid(1).toUpper(); break;
            case 3: txt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.ctime))
                              .toString(dateFmt); break;
            case 4: txt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.mtime))
                              .toString(dateFmt); break;
            case 5: {
                const double t = m_exifCache.value(e.path, 0.0);
                if (t > 0)
                    txt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t))
                              .toString(dateFmt);
                break;   // 未到/无 EXIF:留空,不拿别的日期冒充
            }
            }
            if (txt.isEmpty()) continue;
            p.drawText(QRect(x + 4, r.y(), w - 8, r.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, txt);
        }
    }
}

// ── 占位图标:同尺寸同类型只算一次(原实现每卡片重复平滑缩放 256px 系统图标) ──
QPixmap FileGrid::iconPixmap(const FileEntry& e, int side) {
    if (side <= 0) return QPixmap();
    // 专属图标来自文件自身资源的类型必须按路径缓存,其余按扩展名共享
    static const auto* ownIcon = new QSet<QString>{
        ".exe", ".dll", ".ico", ".scr", ".msi", ".cpl", ".lnk", ".ocx" };
    const bool perFile = !e.isDir && (e.ext.isEmpty() || ownIcon->contains(e.ext));
    const QString key = QStringLiteral("%1@%2%3")
        .arg(perFile ? e.path : (e.isDir ? QLatin1String("<dir>") : e.ext))
        .arg(side).arg(e.hidden ? QLatin1Char('h') : QLatin1Char('n'));
    auto it = m_iconCache.find(key);
    if (it != m_iconCache.end()) return *it;

    QIcon icon = e.isDir ? folderIcon(side) : typeIcon(e.ext, e.path);
    QPixmap pm = icon.pixmap(side, side);
    // #17 复发(2026-09-06):.lnk 一类经 shell 取的图标,JUMBO 档取不到真
    // 256px 时 Windows 交付的是"256 画布 + 左上角 48px 内容"的填充图 ——
    // 等比缩放救不了内容在画布内的位置,先裁透明边把内容请回正中
    // (与缩略图管线 trimPadCenter 同口径),再做等比放缩+正中回贴
    pm = fg_impl::trimPadCenter(pm);
    if (pm.width() != side || pm.height() != side)
        pm = pm.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    {
        QPixmap canvas(side, side);
        canvas.fill(Qt::transparent);
        QPainter cp(&canvas);
        cp.drawPixmap((side - pm.width()) / 2, (side - pm.height()) / 2, pm);
        cp.end();
        pm = canvas;
    }
    if (e.hidden) {   // 隐藏条目图标弱化(与原实现同一 0.45 不透明度)
        QPixmap dim(pm.size());
        dim.fill(Qt::transparent);
        QPainter dp(&dim);
        dp.setOpacity(0.45);
        dp.drawPixmap(0, 0, pm);
        dp.end();
        pm = dim;
    }
    if (m_iconCache.size() > 128) m_iconCache.clear();   // 尺寸连续变化时兜底
    m_iconCache.insert(key, pm);
    return pm;
}

// ── 成品图查询:只读缓存,绘制路径零计算 ──
QPixmap FileGrid::fitFor(const QString& path, const QRect& box) const {
    auto it = m_fitCache.find(path);
    if (it != m_fitCache.end() && it->box == box) return it->pix;
    return QPixmap();
}

// ── 成品图预建:按盒子平滑缩放 + 4px 圆角。只在缩略图到达/进入视口时调用 ──
void FileGrid::buildFit(const QString& path, const QRect& box, bool cover) {
    auto it = m_fitCache.find(path);
    if (it != m_fitCache.end() && it->box == box) return;

    const QSize sz = box.size();
    QPixmap out(sz);
    out.fill(Qt::transparent);
    const QPixmap raw = m_thumbCache.value(path);
    if (!raw.isNull() && !sz.isEmpty()) {
        const QSize s = raw.size().scaled(sz,
            cover ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        // s 已按比例拟合;若再传 KeepAspectRatio,Qt 会把 s 当边界框对原图
        // 二次拟合,部分宽高比下二次取整少 1px,画出的图比选中框窄 1px
        const QPixmap scaled = raw.scaled(s, Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
        const int offX = m_imageAlign == 0 ? 0
                       : m_imageAlign == 2 ? sz.width() - s.width()
                                           : (sz.width() - s.width()) / 2;
        QPainter q(&out);
        q.setRenderHint(QPainter::Antialiasing);
        QPainterPath pp;
        pp.addRoundedRect(0, 0, sz.width(), sz.height(), 4, 4);
        q.setClipPath(pp);
        q.drawPixmap(offX, (sz.height() - s.height()) / 2, scaled);
    }
    if (m_fitCache.size() > 1200) m_fitCache.clear();
    m_fitCache.insert(path, FitThumb{ box, out });
}

