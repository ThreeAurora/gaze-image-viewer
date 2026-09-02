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
    m_filmPaths.clear();
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
    m_filmPaths.clear();
    for (int i = from; i <= to; ++i)
        m_filmPaths << m_fileGrid->pathAt(i);

    // 重建 label 列表(条目结构变了,直接全量重建最简单)
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
    m_filmStrip->setLayout(lay);
}

void MainWindow::jumpToFilmItem(int idx) {
    if (idx < 0 || idx >= m_filmPaths.size()) return;
    const QString path = m_filmPaths[idx];
    if (path == m_currentFile) return;
    // 与"点击标签"/"导航"同一条路:选中列表项会一路 loadFile + 刷标题
    if (m_fileGrid) m_fileGrid->selectByPath(path);
    refreshFilmStrip();          // 当前文件变了,蓝框跟过去
}