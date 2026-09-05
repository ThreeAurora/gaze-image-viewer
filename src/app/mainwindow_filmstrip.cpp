// ═══════════════════════════════════════════════════════════
// MainWindow 全屏胶片条(filmstrip) —— #203 全面重做(2026-09-03 用户令)
//
// G 全屏预览:光标挪到窗口顶端 → 顶部浮现胶片条。控件本体在 src/views/
// filmstrip.h(QListView 虚拟化):目录全部文件随便滚(#225)、滚到哪缩略图
// 加载到哪、点击跳转、按住平移、底部题注"文件名 · i / n"。本文件只剩
// MainWindow 一侧的三件事:建控件+接跳转、光标到顶显隐、把目录全部文件灌进条。
// 光标不在顶部 = 零装饰,与"全屏只留画面"的总原则一致。
// ═══════════════════════════════════════════════════════════

#include "mainwindow.h"
#include "foldertree.h"      // 拖放提示段:updateFolderDropTarget 摸树行
#include "filegrid.h"
#include "views/filmstrip.h"
#include "previewpanel.h"    // 预览面板(loadFile/fitAuto 等接口经 MainWindow 用)
#include "i18n.h"            // 拖放提示段:gazeTr("复制"/"移动")

#include <QLabel>
#include <QToolButton>
#include <QPainter>
#include <QStyle>
#include <QApplication>

namespace {
constexpr int kFilmEdge  = 48;    // 光标顶边触发区
constexpr int kStripMaxW = 1600;  // 条最大宽(超宽屏上不铺满整窗)
constexpr int kFilmGrace = 24;    // #210:条下沿的滞留带高(光标在此带内条不藏)
constexpr int kNavW = 44;         // #220:左右浮动钮宽
constexpr int kNavH = 88;         // #220:左右浮动钮高
constexpr int kNavInset = 14;     // #220:浮动钮距屏幕左右边缘
constexpr int kNavEdge  = 56;     // #220:光标进入左右边缘多宽才浮现
}

void MainWindow::createFilmStrip() {
    m_filmStrip = new FilmStrip(this);
    m_filmStrip->hide();
    // 条上点击 → 与"点击标签"/"导航"同一条路:selectByPath 会一路 loadFile +
    // 刷标题;onSelectionChanged 再把蓝框/题注带回来(见 mainwindow_nav.cpp)
    connect(m_filmStrip, &FilmStrip::jumpRequested, this, [this](const QString& p) {
        if (m_fileGrid && !p.isEmpty() && p != m_currentFile)
            m_fileGrid->selectByPath(p);
    });
    // 右端只剩"退出全屏"一键(#220 用户令:上一个/下一个挪去屏幕左右浮动钮,
    // 适应窗口有预览右键菜单与查看器键位两处入口,条上不再重复)
    connect(m_filmStrip, &FilmStrip::exitRequested, this, [this]() { exitFullView(); });
    // 2026-09-05 用户令:条上滚动 = 画廊与图片一起滚。走 navigateSelection 这条
    // 与左右浮动钮/方向键完全相同的链:选区变 → 预览换图 → syncCurrent 回填,
    // 蓝框/题注由 locateCurrent 拉回正中(当前图恒在画廊中间)
    connect(m_filmStrip, &FilmStrip::stepRequested, this, [this](int delta) {
        if (m_fileGrid) m_fileGrid->navigateSelection(delta);
    });
    // 目录内容变了(增删/重载)才需要重建数据;平时只对账当前文件
    connect(m_fileGrid, &FileGrid::fileCountChanged, this, [this]() {
        m_filmDirty = true;
    });

    // ── #220 左右浮动钮:上一个/下一个 ──
    // 光标挪到屏幕左右边缘才浮现,挪走(且不在钮上)即藏。点击与滚轮同一条链
    // (FileGrid::navigateSelection):选区一变,预览换图,胶片条经
    // mainwindow_nav.cpp 的 syncCurrent 自动居中+蓝框跟随。
    // 图标染白:与 filmstrip.cpp / pp_impl::whiteIcon 同一套做法的又一份本地副本
    // (那两处各自注明只供本单元使用)。
    auto whiteIcon = [](QStyle::StandardPixmap sp) {
        const QPixmap pm = QApplication::style()->standardIcon(sp).pixmap(32, 32);
        QPixmap white(pm.size());
        white.fill(Qt::transparent);
        QPainter p(&white);
        p.drawPixmap(0, 0, pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(white.rect(), QColor("#FFFFFF"));
        p.end();
        return QIcon(white);
    };
    auto mkNav = [&](QStyle::StandardPixmap sp, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(whiteIcon(sp));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(tip);
        b->setObjectName("filmNavBtn");
        b->setFocusPolicy(Qt::NoFocus);   // 点击不吃焦点:方向键继续归全屏键位
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(kNavW, kNavH);
        b->hide();
        return b;
    };
    m_fullNavPrev = mkNav(QStyle::SP_ArrowBack, gazeTr("上一个文件"));
    m_fullNavNext = mkNav(QStyle::SP_ArrowForward, gazeTr("下一个文件"));
    connect(m_fullNavPrev, &QToolButton::clicked, this, [this] {
        if (m_fileGrid) m_fileGrid->navigateSelection(-1);
    });
    connect(m_fullNavNext, &QToolButton::clicked, this, [this] {
        if (m_fileGrid) m_fileGrid->navigateSelection(1);
    });
}

// 光标到顶 → 显示并刷新;离开顶区且不在滞留区 → 隐藏。条自身的事件(滚轮/
// 拖动/点击)全部在 FilmStrip 内部自理,这里只管显隐 + 几何。
// #210 滞留区:条显示期间,光标在条内或条下沿一小段(kFilmGrace)不算离开 ——
// 否则滚轮切图(光标就停在条上/条下方的画面上)时条当场消失,"当前图居中+蓝框"
// 根本没机会演给用户看。
void MainWindow::updateFilmStrip(const QPoint* cursor) {
    if (!m_filmStrip) return;
    if (!m_fullView) {
        m_filmStrip->hide();
        if (m_fullNavPrev) m_fullNavPrev->hide();
        if (m_fullNavNext) m_fullNavNext->hide();
        return;
    }
    const bool nearTop = cursor && cursor->y() <= kFilmEdge;
    bool sticky = false;
    if (!nearTop && m_filmStrip->isVisible() && cursor)
        sticky = m_filmStrip->geometry().adjusted(0, 0, 0, kFilmGrace).contains(*cursor);
    if (!nearTop && !sticky)
        m_filmStrip->hide();
    else {
        refreshFilmStrip();
        if (!m_filmStrip->isVisible() && m_filmStrip->count() > 0) {
            const int w = qMin(width() - 24, kStripMaxW);
            m_filmStrip->setGeometry((width() - w) / 2, 8, w, FilmStrip::preferredHeight());
            m_filmStrip->raise();
            m_filmStrip->show();
        }
    }
    updateFullNavButtons(cursor);   // 条藏了左右钮也要跟着算:两者显隐互不相干
}

// ── #220 左右浮动钮显隐:光标进左右边缘浮现,挪走(且不在钮上)即藏 ──
// 与顶部胶片条各自独立:光标从上往左下走,条先藏、钮后现,同一拍里各管各的。
void MainWindow::updateFullNavButtons(const QPoint* cursor) {
    if (!m_fullNavPrev || !m_fullNavNext) return;
    // 没有可切的条目(空目录/无当前文件)或拿不到光标坐标时不出钮
    if (!cursor || !m_fileGrid || m_fileGrid->fileCount() <= 0
        || m_currentFile.isEmpty()) {
        m_fullNavPrev->hide();
        m_fullNavNext->hide();
        return;
    }
    const int cy = (height() - kNavH) / 2;
    const QRect gl(kNavInset, cy, kNavW, kNavH);
    const QRect gr(width() - kNavInset - kNavW, cy, kNavW, kNavH);
    // 形参不用 near/far:Win32 老宏,在包含链里会被展开成空(GCC 报错实证)
    auto settle = [&](QToolButton* b, const QRect& r, bool atEdge) {
        const bool sticky = !atEdge && b->isVisible() && r.contains(*cursor);
        if (atEdge || sticky) {
            if (b->geometry() != r) b->setGeometry(r);
            b->raise();
            b->show();
        } else {
            b->hide();
        }
    };
    settle(m_fullNavPrev, gl, cursor->x() <= kNavEdge);
    settle(m_fullNavNext, gr, cursor->x() >= width() - kNavEdge);
}

// 把目录全部文件灌进条(#225:FileGrid::allFilePaths —— 目录行除外、不跟
// 网格筛选走,网格筛成"图片"条里照样有视频/音频;点被筛掉的条目由
// selectByPath 的"切回全部再选"回退兜住)。#203:只有目录或条目数变了才重建
// (m_filmDirty 由 fileCountChanged 置位,目录串对账防"数没变内容换了");
// 平时只把当前文件对进去(蓝框/居中/题注),不再像旧实现那样每拍全量重建。
void MainWindow::refreshFilmStrip() {
    if (!m_filmStrip || !m_fileGrid) return;
    const int total = m_fileGrid->fileCount();
    if (total <= 0 || m_currentFile.isEmpty()) { m_filmStrip->hide(); return; }
    const QString dir = m_fileGrid->currentDir();
    if (!m_filmDirty && dir == m_filmDir && m_filmStrip->count() > 0) {
        m_filmStrip->syncCurrent(m_currentFile);
        return;
    }
    m_filmDir = dir;
    m_filmDirty = false;
    m_filmStrip->setEntries(m_fileGrid->allFilePaths(), m_currentFile);
}

// ═══════════════════════════════════════════════════════════
// 拖放提示(2026-09-02):光标旁"复制/移动"浮标 + 树落点白框
// ═══════════════════════════════════════════════════════════
void MainWindow::updateDragHint(const QPoint& pos, bool valid) {
    if (!m_dragHint) {
        m_dragHint = new QLabel(this);
        m_dragHint->setObjectName("filmDragHint");
    }
    const bool copy = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    const QString verb = copy ? gazeTr("复制") : gazeTr("移动");
    m_dragHint->setText(valid ? verb : QString());
    if (!valid) { m_dragHint->hide(); return; }
    m_dragHint->adjustSize();
    // 位置:光标右下偏移 16px,避免盖住正在拖的项目
    m_dragHint->move(pos + QPoint(16, 16));
    m_dragHint->raise();
    m_dragHint->show();
}

void MainWindow::hideDragHint() {
    if (m_dragHint) m_dragHint->hide();
    updateFolderDropTarget(QPoint(), false);
}

// 树落点白框:把当前悬停的目录行临时画一层白描边。QTreeWidget 的 item 无法
// 单独 setStyleSheet,这里用"临时选中态"(白框)并在离开时还原 —— 只影响视觉,
// 不改选中集。QTreeWidget 的 selected 样式是白字蓝底,不是白框;改用
// 给当前 item 的 foreground 亮白 + 一个"即将放入"的观感,靠树的高亮 bolder。
// 落空(拖到行间空隙/拖在网格上):清标记 —— 否则旧白框残留在上一行
void MainWindow::updateFolderDropTarget(const QPoint& pos, bool highlight) {
    if (!m_folderTree) return;
    if (!highlight) {
        m_folderTree->setProperty("_dropItem", QVariant());   // 清标记
        m_folderTree->viewport()->update();
        return;
    }
    QTreeWidgetItem* it = m_folderTree->itemAt(m_folderTree->mapFrom(this, pos));
    m_folderTree->setProperty("_dropItem", it ? QVariant::fromValue(it) : QVariant());
    m_folderTree->viewport()->update();
}