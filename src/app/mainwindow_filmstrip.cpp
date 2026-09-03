// ═══════════════════════════════════════════════════════════
// MainWindow 全屏胶片条(filmstrip) —— #203 全面重做(2026-09-03 用户令)
//
// G 全屏预览:光标挪到窗口顶端 → 顶部浮现胶片条。控件本体在 src/views/
// filmstrip.h(QListView 虚拟化):整个目录随便滚、滚到哪缩略图加载到哪、
// 点击跳转、按住平移、底部题注"文件名 · i / n"。本文件只剩 MainWindow 一侧
// 的三件事:建控件+接跳转、光标到顶显隐、把 FileGrid 显示列表灌进条。
// 光标不在顶部 = 零装饰,与"全屏只留画面"的总原则一致。
// ═══════════════════════════════════════════════════════════

#include "mainwindow.h"
#include "foldertree.h"      // 拖放提示段:updateFolderDropTarget 摸树行
#include "filegrid.h"
#include "views/filmstrip.h"
#include "i18n.h"            // 拖放提示段:gazeTr("复制"/"移动")

#include <QLabel>
#include <QApplication>

namespace {
constexpr int kFilmEdge  = 48;    // 光标顶边触发区
constexpr int kStripMaxW = 1600;  // 条最大宽(超宽屏上不铺满整窗)
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
    // 目录内容变了(增删/重载)才需要重建数据;平时只对账当前文件
    connect(m_fileGrid, &FileGrid::fileCountChanged, this, [this]() {
        m_filmDirty = true;
    });
}

// 光标到顶 → 显示并刷新;离开顶区 → 隐藏。条自身的事件(滚轮/拖动/点击)全部
// 在 FilmStrip 内部自理,这里只管显隐 + 几何。条显示期间光标在条上时
// eventFilter 不触发(事件归条),自然形成"粘滞",无需额外判断。
void MainWindow::updateFilmStrip(const QPoint* cursor) {
    if (!m_filmStrip) return;
    if (!m_fullView) { m_filmStrip->hide(); return; }
    const bool nearTop = cursor && cursor->y() <= kFilmEdge;
    if (!nearTop) { m_filmStrip->hide(); return; }
    refreshFilmStrip();
    if (!m_filmStrip->isVisible() && m_filmStrip->count() > 0) {
        const int w = qMin(width() - 24, kStripMaxW);
        m_filmStrip->setGeometry((width() - w) / 2, 8, w, FilmStrip::preferredHeight());
        m_filmStrip->raise();
        m_filmStrip->show();
    }
}

// 把 FileGrid 当前目录的显示列表灌进条。#203:只有目录或条目数变了才重建
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
    QStringList paths;
    paths.reserve(total);
    for (int i = 0; i < total; ++i) paths << m_fileGrid->pathAt(i);
    m_filmDir = dir;
    m_filmDirty = false;
    m_filmStrip->setEntries(paths, m_currentFile);
}

// ═══════════════════════════════════════════════════════════
// 拖放提示(2026-09-02):光标旁"复制/移动"浮标 + 树落点白框
// ═══════════════════════════════════════════════════════════
void MainWindow::updateDragHint(const QPoint& pos, bool valid) {
    if (!m_dragHint) {
        m_dragHint = new QLabel(this);
        m_dragHint->setStyleSheet(QString::fromUtf8(
            "QLabel{background:rgba(24,24,30,235);color:#FFFFFF;border:1px solid #3A3A42;"
            "border-radius:4px;padding:3px 8px;font-size:12px;}"));
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
// 落空/非目录:不画
void MainWindow::updateFolderDropTarget(const QPoint& pos, bool highlight) {
    if (!m_folderTree) return;
    if (!highlight) {
        m_folderTree->setProperty("_dropItem", QVariant());   // 清标记
        m_folderTree->viewport()->update();
        return;
    }
    QTreeWidgetItem* it = m_folderTree->itemAt(m_folderTree->mapFrom(this, pos));
    if (!it) return;
    m_folderTree->setProperty("_dropItem", QVariant::fromValue(it));
    m_folderTree->viewport()->update();
}