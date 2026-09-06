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
#include "i18n.h"

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
#include <QPolygon>
#include <QPainter>

// ═══════════════════════════════════════════
// 内联搜索条(#107):Ctrl+F 在文件列表上落一个搜索框,不弹窗。
// 树右键的"搜索..."(递归子目录、可边搜边看)保持弹窗不动,两者定位不同。
// 匹配 = 文件名不区分大小写包含;到边界/无结果时对应按钮变灰(不回绕)。
// ═══════════════════════════════════════════

void FileGrid::buildFindBar() {
    // 用户令:查找条不浮在右上角,改为贴着排序表头正下方、与表头一样长的整条。
    // 父件 = FileGrid 本体(视口子件会被滚动条裁窄),y=0 即表头正下方。
    m_findBar = new QWidget(this);
    m_findBar->setObjectName("findBar");   // 样式在应用级 QSS(#89 收敛)
    auto* lay = new QHBoxLayout(m_findBar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(gazeTr("查找文件名..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(180);
    m_findEdit->installEventFilter(this);   // Enter/Shift+Enter/Up/Down/Esc
    // 输入即搜:当前项仍命中就原地不动,否则跳到落点之后(无落点则从头)的第一个命中
    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        const QString q = m_findEdit->text().trimmed();
        const int n = static_cast<int>(m_entries.size());
        const int cur = m_lastClicked;
        if (!q.isEmpty() && !(cur >= 0 && cur < n
                && m_entries[cur].name.contains(q, Qt::CaseInsensitive))) {
            int start = (cur >= 0 && cur < n) ? cur : -1;
            for (int k = 1; k <= n; ++k) {
                const int i = start + k;
                if (i >= n) break;
                if (m_entries[i].name.contains(q, Qt::CaseInsensitive)) { selectIndex(i); break; }
            }
        }
        findRefresh();
    });
    lay->addWidget(m_findEdit, 1);

    m_findInfo = new QLabel(m_findBar);
    m_findInfo->setObjectName("findInfo");   // 样式在应用级 QSS(#89 收敛)
    m_findInfo->setAlignment(Qt::AlignCenter);
    m_findInfo->setMinimumWidth(52);
    lay->addWidget(m_findInfo);

    m_findPrev = new QToolButton(m_findBar);
    m_findPrev->setIcon(fg_impl::paintedArrow(QStyle::SP_ArrowUp));
    m_findPrev->setToolTip(gazeTr("上一个(Shift+Enter)"));
    m_findPrev->setFixedSize(24, 24);
    connect(m_findPrev, &QToolButton::clicked, this, [this]() { findStep(-1); });
    lay->addWidget(m_findPrev);

    m_findNext = new QToolButton(m_findBar);
    m_findNext->setIcon(fg_impl::paintedArrow(QStyle::SP_ArrowDown));
    m_findNext->setToolTip(gazeTr("下一个(Enter)"));
    m_findNext->setFixedSize(24, 24);
    connect(m_findNext, &QToolButton::clicked, this, [this]() { findStep(1); });
    lay->addWidget(m_findNext);

    auto* btnClose = new QToolButton(m_findBar);
    btnClose->setText(gazeTr("✕"));   // 普通 ✕ 文本,不用图标
    btnClose->setObjectName("findCloseBtn");
    btnClose->setToolTip(gazeTr("关闭(Esc)"));
    btnClose->setFixedSize(24, 24);
    connect(btnClose, &QToolButton::clicked, this, [this]() { closeFind(); });
    lay->addWidget(btnClose);
}

void FileGrid::startFind() {
    if (!m_findBar) buildFindBar();
    placeFindBar();
    m_findBar->show();
    m_findBar->raise();
    m_findEdit->setFocus(Qt::OtherFocusReason);
    m_findEdit->selectAll();
    findRefresh();
}

void FileGrid::closeFind() {
    if (m_findBar) m_findBar->hide();
    m_canvas->setFocus(Qt::OtherFocusReason);
}

void FileGrid::placeFindBar() {
    if (!m_findBar) return;
    const QSize sz = m_findBar->sizeHint();
    // 贴排序表头正下方的整条(用户令):宽度吃满 FileGrid 本体,与表头同长,y=0
    m_findBar->setGeometry(0, 0, qMax(sz.width(), width()), sz.height());
}

// 命中数/当前序号/按钮置灰,一次 O(n) 扫完。当前项 = m_lastClicked:
// 它本身命中 → 显示 第k/N;不命中 → 0/N 且"下一个"跳到它之后的第一个命中
void FileGrid::findRefresh() {
    if (!m_findBar || !m_findBar->isVisible()) return;
    const QString q = m_findEdit->text().trimmed();
    const int cur = (m_lastClicked >= 0 && m_lastClicked < static_cast<int>(m_entries.size()))
                        ? m_lastClicked : -1;
    m_findHitCount = 0;
    m_findOrdinal  = -1;
    bool before = false, after = false;
    if (!q.isEmpty()) {
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (!m_entries[i].name.contains(q, Qt::CaseInsensitive)) continue;
            if (i == cur) m_findOrdinal = m_findHitCount;
            if (cur < 0 || i < cur) before = true;
            if (cur < 0 || i > cur) after  = true;
            ++m_findHitCount;
        }
    }
    m_findInfo->setText(m_findHitCount == 0
        ? gazeTr("无匹配")
        : QString("%1/%2").arg(m_findOrdinal >= 0 ? m_findOrdinal + 1 : 0).arg(m_findHitCount));
    m_findPrev->setEnabled(before);
    m_findNext->setEnabled(after);
}

void FileGrid::findStep(int delta) {
    if (!m_findBar || !m_findBar->isVisible()) return;
    const QString q = m_findEdit->text().trimmed();
    if (q.isEmpty() || m_entries.empty()) return;
    const int n = static_cast<int>(m_entries.size());
    int start = m_lastClicked;
    if (start < 0 || start >= n) start = delta > 0 ? -1 : n;   // 无落点:从头/从尾扫
    for (int k = 1; k <= n; ++k) {
        const int i = start + delta * k;
        if (i < 0 || i >= n) break;    // 到边界:不回绕(置灰按钮已表达"没有更多")
        if (m_entries[i].name.contains(q, Qt::CaseInsensitive)) {
            selectIndex(i);
            findRefresh();
            return;
        }
    }
}

