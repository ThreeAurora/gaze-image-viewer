// ═══════════════════════════════════════════════════════════
// MainWindow 全屏胶片条(filmstrip) —— 2026-09-02 用户令
//
// G 全屏预览时:光标挪到窗口顶端 → 顶部浮现一排"就近图片"的缩略图条。
// 滚轮在条上 = 切换文件;点击某张 → 跳过去显示在中间,当前查看那张加蓝框。
// 不光标到顶 = 零装饰,与"全屏只留画面"的总原则一致。
//
// 数据源:FileGrid 当前目录的显示列表(m_entries),以当前文件为中心取 ±8
// 张(共 17)。缩略图走 Thumbnailer::enqueue → thumbnailReady 异步回填,
// 网格刚展示过的文件必然命中缓存,秒出。
// ═══════════════════════════════════════════════════════════

#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "previewpanel.h"
#include "thumbnailer.h"
#include "constants.h"
#include "fileentry.h"
#include "i18n.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QApplication>

namespace {
constexpr int kFilmHalf = 8;          // 当前文件前后各 8 张
constexpr int kFilmH    = 64;         // 条目高度
constexpr int kFilmEdge = 48;         // 光标顶边触发区
}

void MainWindow::createFilmStrip() {
    m_filmStrip = new QWidget(this);
    m_filmStrip->setStyleSheet(QString::fromUtf8(
        "QWidget#filmStrip{background:rgba(18,18,24,235);border:1px solid #3A3A42;"
        "border-radius:8px;}"));
    m_filmStrip->setObjectName(QStringLiteral("filmStrip"));
    m_filmStrip->hide();
    // 触达:整条最上层,滚轮/点击都落在条上
    m_filmStrip->raise();
}

// 光标到顶 → 显示并刷新条目;离开 → 隐藏。进全屏时由 toggleFullView 主动刷一次,
// 这里只负责显隐 + 滚轮/点击不悬空(mouseMove 在 mouse 没有按下时也来)。
void MainWindow::updateFilmStrip(const QPoint* cursor) {
    if (!m_fullView) { m_filmStrip->hide(); return; }
    const bool nearTop = cursor && cursor->y() <= kFilmEdge;
    if (!nearTop) { m_filmStrip->hide(); return; }
    refreshFilmStrip();
    m_filmStrip->adjustSize();
    m_filmStrip->move((width() - m_filmStrip->width()) / 2, 8);
    m_filmStrip->raise();
    m_filmStrip->show();
}

// 以当前文件为中心重建条目。缩略图异步回填:先清空放占位,enqueue 后由
// mainwindow.cpp 的 thumbnailReady 回调按路径落图(与标签缩略图同一机制)。
void MainWindow::refreshFilmStrip() {
    if (!m_filmStrip) return;
    // 从 FileGrid 拿当前目录的显示列表;拿不到就空条
    // 取当前文件在列表里的位置
    const int total = m_fileGrid ? m_fileGrid->fileCount() : 0;
    if (total <= 0 || m_currentFile.isEmpty()) { m_filmStrip->hide(); return; }
    int cur = -1;
    for (int i = 0; i < total; ++i) {
        if (m_fileGrid->pathAt(i) == m_currentFile) { cur = i; break; }
    }
    if (cur < 0) { m_filmStrip->hide(); return; }

    const int from = qMax(0, cur - kFilmHalf);
    const int to   = qMin(total - 1, cur + kFilmHalf);
    // 2026-09-03 夜修:光标在顶部每动一次 mouseMove 都会刷一遍,这里
    // 先比"窗口路径列表"——没变就直接返回,不再每次销毁重建 17 个标签
    //(旧实现每拍全量重建,还要走一遍 setLayout,见下方修法)
    QStringList want;
    want.reserve(to - from + 1);
    for (int i = from; i <= to; ++i)
        want << m_fileGrid->pathAt(i);
    if (want == m_filmPaths && m_filmItems.size() == want.size()) return;
    m_filmPaths = want;

    // 2026-09-03 夜修"单个黑点":旧代码每次 new QHBoxLayout 后 setLayout,
    // 但 widget 已有 layout 时 setLayout 会被 Qt 拒绝(仅告警)——第二拍起
    // 新标签全堆在 (0,0)、旧 layout 里还挂着已 delete 的标签,整条塌缩成
    // 一个小黑块。正确做法:先删旧 layout(不删它管的子控件),再删旧标签,
    // 然后 QHBoxLayout(parent) 构造即自动挂载,不需要再 setLayout。
    if (QLayout* old = m_filmStrip->layout()) { delete old; }
    qDeleteAll(m_filmItems);
    m_filmItems.clear();
    auto* lay = new QHBoxLayout(m_filmStrip);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);
    for (int k = 0; k < m_filmPaths.size(); ++k) {
        const bool isCur = (from + k) == cur;
        auto* lb = new QLabel;
        lb->setFixedSize(72, kFilmH);
        lb->setAlignment(Qt::AlignCenter);
        lb->setScaledContents(false);
        lb->setToolTip(QFileInfo(m_filmPaths[k]).fileName());
        // 蓝框:当前文件这张;其余透明
        lb->setStyleSheet(isCur
            ? QString("QLabel{background:#26262B;border:2px solid %1;border-radius:4px;}")
                  .arg(QColor(0, 120, 215).name())
            : QString("QLabel{background:#26262B;border:1px solid %1;border-radius:4px;}")
                  .arg(C_SEPARATOR));
        lb->setCursor(Qt::PointingHandCursor);
        lb->setProperty("idx", from + k);
        if (from + k == cur) {
            // 当前那张放一张"当前"标记也 OK,但蓝框已足够;这里不叠加
        }
        lb->installEventFilter(this);          // 点击/滚轮由 eventFilter 分支收
        lay->addWidget(lb);
        m_filmItems << lb;
        // 缩略图异步回填
        const QString& p = m_filmPaths[k];
        QString su = QFileInfo(p).suffix().toLower();
        if (!su.isEmpty()) su.prepend(QLatin1Char('.'));
        Thumbnailer::instance().enqueue(p, 72, VIDEO_EXTS.count(su) > 0);
    }
    // thumbnailReady 连接(见 ctor)会按路径回填 m_filmItems 里对应那张
    // 注:QHBoxLayout(parent) 构造时已自动挂到 m_filmStrip,不再 setLayout(见上)
}

void MainWindow::jumpToFilmItem(int idx) {
    if (idx < 0 || idx >= m_filmPaths.size()) return;
    const QString path = m_filmPaths[idx];
    if (path == m_currentFile) return;
    // 与"点击标签"/"导航"同一条路:选中列表项会一路 loadFile + 刷标题
    if (m_fileGrid) m_fileGrid->selectByPath(path);
    refreshFilmStrip();          // 当前文件变了,蓝框跟过去
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