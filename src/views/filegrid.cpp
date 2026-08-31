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
#include "validname.h"
#include "exifdate.h"
#include "namesort.h"
#include "perflog.h"
#include "logger.h"
#include "logger.h"

#include <set>
#include <algorithm>
#include <numeric>
#include <algorithm>
#include <numeric>
#include <memory>

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

FileGrid::FileGrid(QWidget* parent) : QScrollArea(parent) {
    setStyleSheet(QString::fromUtf8("QScrollArea{background:%1;border:none;}")
                      .arg(C_CONTENT));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 与 XnView 一致：即使内容少于一页也保留竖向滚动条，无法拖动时显示为整条长拇指
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setWidgetResizable(false);

    m_canvas = new FileCanvas(this);
    m_canvas->setStyleSheet(QString::fromUtf8("background:%1;").arg(C_CONTENT));
    setWidget(m_canvas);

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) {
        // 自绘模式下滚动不产生任何控件级工作:画布移动 + Qt 自动曝光重绘。
        // 缩略图请求必须"跟着视口走"——推迟到停止后才补,视觉上就是拖尾。
        // 这里合并为每轮事件循环一次;跨屏跳转则丢掉离屏的排队任务,立刻服务新位置。
        if (m_lastScrollVal >= 0
            && qAbs(val - m_lastScrollVal) > viewport()->height())
            Thumbnailer::instance().clearQueue();
        m_lastScrollVal = val;
        m_scrollCoalesce.start();
    });

    connect(&Thumbnailer::instance(), &Thumbnailer::thumbnailReady,
            this, &FileGrid::onThumbReady);

    // Ctrl+滚轮缩放
    m_canvas->installEventFilter(this);
    installEventFilter(this);

    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(30);
    connect(&m_resizeTimer, &QTimer::timeout, this, [this]() {
        if (!m_entries.empty()) updateLayout();
    });

    // 滚动中:同一轮事件循环内的多次 valueChanged 合并成一次可见区请求。
    // Browser/thumbScrollPreview(默认开)= 0ms 合并,滚动过程中缩略图就陆续出现;
    // 关掉则等滚动停止 120ms 再补(滚动中只画占位图标,快速掠过大目录更跟手)。
    m_scrollCoalesce.setSingleShot(true);
    m_scrollCoalesce.setInterval(m_scrollPreview ? 0 : 120);
    connect(&m_scrollCoalesce, &QTimer::timeout, this, [this]() {
        requestVisibleThumbs();
    });

    // 尺寸停止变化后,按最终尺寸重新生成可见卡片的高清缩略图
    m_reEnqueueTimer.setSingleShot(true);
    m_reEnqueueTimer.setInterval(100);
    connect(&m_reEnqueueTimer, &QTimer::timeout, this, [this]() {
        requestVisibleThumbs();
    });

    // 恢复持久化的列数/查看方式/文件名排序方式/卡片间距
    m_fixedCols = qBound(0, AppSettings::instance().get("Browser/fixedCols", 0).toInt(), 16);
    m_viewMode  = qBound(0, AppSettings::instance().get("Browser/viewMode", int(VM_THUMBS_NAME)).toInt(), int(VM_WATERFALL));
    m_nameOrder = qBound(0, AppSettings::instance().get("Browser/nameOrder", int(NameNatural)).toInt(), int(NameNormal));
    // #106:筛选模式同样落盘恢复(此前切"视频"重启回"全部")
    m_filterMode = qBound(int(FILTER_ALL), AppSettings::instance().get("Browser/filterMode", int(FILTER_ALL)).toInt(), int(FILTER_UNRED));
    // #106:筛选模式同样落盘恢复(此前切"视频"重启回"全部")
    m_filterMode = qBound(int(FILTER_ALL), AppSettings::instance().get("Browser/filterMode", int(FILTER_ALL)).toInt(), int(FILTER_UNRED));
    m_spacing   = qBound(0, AppSettings::instance().get("Appearance/spacing", 6).toInt(), 40);

    // 设置活应用:标签颜色(开关/配色)+ 外观间距 + 文件列表过滤/排序规则
    // (此前 changed() 无订阅者,所有设置都要重启才生效)
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        LabelColors::reload();
        const int sp = qBound(0,
            AppSettings::instance().get("Appearance/spacing", 6).toInt(), 40);
        const bool spacingChanged = (sp != m_spacing);
        m_spacing = sp;
        const bool listChanged =
               m_showHidden != AppSettings::instance().get("FileList/showHidden", true).toBool()
            || m_mixSort    != AppSettings::instance().get("FileList/mixSort", false).toBool()
            || m_folderAlpha != AppSettings::instance().get("FileList/folderAlphabetical", true).toBool();
        m_showHidden  = AppSettings::instance().get("FileList/showHidden", true).toBool();
        m_mixSort     = AppSettings::instance().get("FileList/mixSort", false).toBool();
        m_folderAlpha = AppSettings::instance().get("FileList/folderAlphabetical", true).toBool();
    m_showSubFolders = AppSettings::instance().get("FileList/showSubFolders", false).toBool();
    m_showSubFolders = AppSettings::instance().get("FileList/showSubFolders", false).toBool();
        if (listChanged) {
            applyFilter();
            sort(m_sortCol, m_sortAsc);
            updateLayout();
        } else if (spacingChanged) {
            updateLayout();
        }
        recycleCards();          // 卡片外观(边框/对齐/评级圈)重建
        layoutCards();
    });
}

// 设置改动后的重排:重算列宽 + 重算几何 + 重绘
void FileGrid::relayoutNow() {
    m_cols = 0;
    updateLayout();
}

// 标题模板 {颜色标签}:目录加载时已批量读入 m_colorLabels,这里只查内存
int FileGrid::colorLabelOf(const QString& path) const {
    return m_colorLabels.value(path, 0);
}

// 相邻文件路径(delta=+1 下一张/-1 上一张;预读用,越界返回空)
QString FileGrid::neighborOf(const QString& path, int delta) const {
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (m_entries[i].path == path) {
            const int j = i + delta;
            if (j >= 0 && j < static_cast<int>(m_entries.size()))
                return m_entries[j].path;
            break;
        }
    }
    return {};
}

// ═══════════════════════════════════════════
// 内联搜索条(#107):Ctrl+F 在文件列表上落一个搜索框,不弹窗。
// 树右键的"搜索..."(递归子目录、可边搜边看)保持弹窗不动,两者定位不同。
// 匹配 = 文件名不区分大小写包含;到边界/无结果时对应按钮变灰(不回绕)。
// ═══════════════════════════════════════════

// 标准图标在深色底上是黑线,统一染成白色轮廓;QIcon 的 Disabled 模式
// 会从这份 pixmap 自动生成置灰版本,按钮 disable 即"变灰"
static QIcon whiteStdIcon(QStyle* st, QStyle::StandardPixmap sp) {
    const QPixmap pm = st->standardIcon(sp).pixmap(20, 20);
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a) line[x] = qRgba(255, 255, 255, a);
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

void FileGrid::buildFindBar() {
    m_findBar = new QWidget(viewport());
    m_findBar->setObjectName("findBar");
    m_findBar->setStyleSheet(QString::fromUtf8(
        "QWidget#findBar{background:%1;border:1px solid %2;border-radius:4px;}"
        "QLineEdit{background:%3;color:%4;border:1px solid %2;"
        "border-radius:3px;padding:1px 6px;selection-background-color:%5;}"
        "QToolButton{background:transparent;border:none;border-radius:3px;}"
        "QToolButton:hover{background:%6;}"
        "QToolButton:pressed{background:%2;}"
        "QToolButton:disabled{background:transparent;}")
        .arg(C_TOOLBAR, C_SEPARATOR, C_CONTENT, C_TEXT, C_ACCENT, C_CARD_HOVER));
    auto* lay = new QHBoxLayout(m_findBar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QString::fromUtf8("查找文件名..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setFixedSize(180, 24);
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
    lay->addWidget(m_findEdit);

    m_findInfo = new QLabel(m_findBar);
    m_findInfo->setStyleSheet(QString::fromUtf8(
        "color:%1;font-size:12px;background:transparent;border:none;")
        .arg(C_TEXT_SUB));
    m_findInfo->setAlignment(Qt::AlignCenter);
    m_findInfo->setMinimumWidth(52);
    lay->addWidget(m_findInfo);

    m_findPrev = new QToolButton(m_findBar);
    m_findPrev->setIcon(whiteStdIcon(style(), QStyle::SP_ArrowUp));
    m_findPrev->setToolTip(QString::fromUtf8("上一个(Shift+Enter)"));
    m_findPrev->setFixedSize(24, 24);
    connect(m_findPrev, &QToolButton::clicked, this, [this]() { findStep(-1); });
    lay->addWidget(m_findPrev);

    m_findNext = new QToolButton(m_findBar);
    m_findNext->setIcon(whiteStdIcon(style(), QStyle::SP_ArrowDown));
    m_findNext->setToolTip(QString::fromUtf8("下一个(Enter)"));
    m_findNext->setFixedSize(24, 24);
    connect(m_findNext, &QToolButton::clicked, this, [this]() { findStep(1); });
    lay->addWidget(m_findNext);

    auto* btnClose = new QToolButton(m_findBar);
    btnClose->setIcon(whiteStdIcon(style(), QStyle::SP_TitleBarCloseButton));
    btnClose->setToolTip(QString::fromUtf8("关闭(Esc)"));
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
    m_findBar->resize(sz);
    m_findBar->move(viewport()->width() - sz.width() - 12, 12);
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
        ? QString::fromUtf8("无匹配")
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

// ═══════════════════════════════════════════
// 内联搜索条(#107):Ctrl+F 在文件列表上落一个搜索框,不弹窗。
// 树右键的"搜索..."(递归子目录、可边搜边看)保持弹窗不动,两者定位不同。
// 匹配 = 文件名不区分大小写包含;到边界/无结果时对应按钮变灰(不回绕)。
// ═══════════════════════════════════════════

// 标准图标在深色底上是黑线,统一染成白色轮廓;QIcon 的 Disabled 模式
// 会从这份 pixmap 自动生成置灰版本,按钮 disable 即"变灰"
static QIcon whiteStdIcon(QStyle* st, QStyle::StandardPixmap sp) {
    const QPixmap pm = st->standardIcon(sp).pixmap(20, 20);
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a) line[x] = qRgba(255, 255, 255, a);
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

void FileGrid::buildFindBar() {
    m_findBar = new QWidget(viewport());
    m_findBar->setObjectName("findBar");
    m_findBar->setStyleSheet(QString::fromUtf8(
        "QWidget#findBar{background:%1;border:1px solid %2;border-radius:4px;}"
        "QLineEdit{background:%3;color:%4;border:1px solid %2;"
        "border-radius:3px;padding:1px 6px;selection-background-color:%5;}"
        "QToolButton{background:transparent;border:none;border-radius:3px;}"
        "QToolButton:hover{background:%6;}"
        "QToolButton:pressed{background:%2;}"
        "QToolButton:disabled{background:transparent;}")
        .arg(C_TOOLBAR, C_SEPARATOR, C_CONTENT, C_TEXT, C_ACCENT, C_CARD_HOVER));
    auto* lay = new QHBoxLayout(m_findBar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QString::fromUtf8("查找文件名..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setFixedSize(180, 24);
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
    lay->addWidget(m_findEdit);

    m_findInfo = new QLabel(m_findBar);
    m_findInfo->setStyleSheet(
        "color:" C_TEXT_SUB ";font-size:12px;background:transparent;border:none;");
    m_findInfo->setAlignment(Qt::AlignCenter);
    m_findInfo->setMinimumWidth(52);
    lay->addWidget(m_findInfo);

    m_findPrev = new QToolButton(m_findBar);
    m_findPrev->setIcon(whiteStdIcon(style(), QStyle::SP_ArrowUp));
    m_findPrev->setToolTip(QString::fromUtf8("上一个(Shift+Enter)"));
    m_findPrev->setFixedSize(24, 24);
    connect(m_findPrev, &QToolButton::clicked, this, [this]() { findStep(-1); });
    lay->addWidget(m_findPrev);

    m_findNext = new QToolButton(m_findBar);
    m_findNext->setIcon(whiteStdIcon(style(), QStyle::SP_ArrowDown));
    m_findNext->setToolTip(QString::fromUtf8("下一个(Enter)"));
    m_findNext->setFixedSize(24, 24);
    connect(m_findNext, &QToolButton::clicked, this, [this]() { findStep(1); });
    lay->addWidget(m_findNext);

    auto* btnClose = new QToolButton(m_findBar);
    btnClose->setIcon(whiteStdIcon(style(), QStyle::SP_TitleBarCloseButton));
    btnClose->setToolTip(QString::fromUtf8("关闭(Esc)"));
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
    m_findBar->resize(sz);
    m_findBar->move(viewport()->width() - sz.width() - 12, 12);
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
        ? QString::fromUtf8("无匹配")
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

void FileGrid::refreshCurrentDir() {
    if (!m_currentDir.isEmpty()) loadDirectory(m_currentDir);
}

// 删除后重载:落点 = 被删块的后一项,已在末尾则前一项(对齐 XnView)
// 落点必须在重载前的 m_entries 上算 — 重载后索引含义已变
void FileGrid::reloadAfterDelete(const QStringList& deleted) {
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!deleted.contains(m_entries[i].path)) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (last >= 0) {
        if (last + 1 < static_cast<int>(m_entries.size()))
            m_preferPath = m_entries[last + 1].path;
        else if (first > 0)
            m_preferPath = m_entries[first - 1].path;
    }
    const QString dir = m_currentDir;
    if (dir.isEmpty()) { m_preferPath.clear(); return; }
    loadDirectory(dir);
    m_preferPath.clear();   // 重载被中止时不让落点串到下次导航
    // 删掉的文件若还开在查看器标签里,标签就成了指向不存在路径的幽灵
    // (以前只在"进查看器"时清)。这里是所有删除路径唯一的落点:右键/Del/S/
    // 预览侧删都汇到这一处,所以逐标签 stat 也只跟着删除发生,不进导航热路径。
    // pruneDeadViewerTabs 只摘死标签,当前正在看的那张若被删会一并摘掉。
    if (auto* mw = window()) QMetaObject::invokeMethod(mw, "pruneDeadViewerTabs");
}

// 删除后重载:落点 = 被删块的后一项,已在末尾则前一项(对齐 XnView)
// 落点必须在重载前的 m_entries 上算 — 重载后索引含义已变
void FileGrid::reloadAfterDelete(const QStringList& deleted) {
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!deleted.contains(m_entries[i].path)) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (last >= 0) {
        if (last + 1 < static_cast<int>(m_entries.size()))
            m_preferPath = m_entries[last + 1].path;
        else if (first > 0)
            m_preferPath = m_entries[first - 1].path;
    }
    const QString dir = m_currentDir;
    if (dir.isEmpty()) { m_preferPath.clear(); return; }
    loadDirectory(dir);
    m_preferPath.clear();   // 重载被中止时不让落点串到下次导航
    // 删掉的文件若还开在查看器标签里,标签就成了指向不存在路径的幽灵
    // (以前只在"进查看器"时清)。这里是所有删除路径唯一的落点:右键/Del/S/
    // 预览侧删都汇到这一处,所以逐标签 stat 也只跟着删除发生,不进导航热路径。
    // pruneDeadViewerTabs 只摘死标签,当前正在看的那张若被删会一并摘掉。
    if (auto* mw = window()) QMetaObject::invokeMethod(mw, "pruneDeadViewerTabs");
}

// 删除后重载:落点 = 被删块的后一项,已在末尾则前一项(对齐 XnView)
// 落点必须在重载前的 m_entries 上算 — 重载后索引含义已变
void FileGrid::reloadAfterDelete(const QStringList& deleted) {
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!deleted.contains(m_entries[i].path)) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (last >= 0) {
        if (last + 1 < static_cast<int>(m_entries.size()))
            m_preferPath = m_entries[last + 1].path;
        else if (first > 0)
            m_preferPath = m_entries[first - 1].path;
    }
    const QString dir = m_currentDir;
    if (dir.isEmpty()) { m_preferPath.clear(); return; }
    loadDirectory(dir);
    m_preferPath.clear();   // 重载被中止时不让落点串到下次导航
}

// 删除后重载:落点 = 被删块的后一项,已在末尾则前一项(对齐 XnView)
// 落点必须在重载前的 m_entries 上算 — 重载后索引含义已变
void FileGrid::reloadAfterDelete(const QStringList& deleted) {
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!deleted.contains(m_entries[i].path)) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (last >= 0) {
        if (last + 1 < static_cast<int>(m_entries.size()))
            m_preferPath = m_entries[last + 1].path;
        else if (first > 0)
            m_preferPath = m_entries[first - 1].path;
    }
    const QString dir = m_currentDir;
    if (dir.isEmpty()) { m_preferPath.clear(); return; }
    loadDirectory(dir);
    m_preferPath.clear();   // 重载被中止时不让落点串到下次导航
}

// ═══════════════════════════════════════════
// FileList/scanHeader:是否允许读文件头(与 recognizeByExt 配套)
//   0 总是   1 排除软盘/CD/DVD   2 仅电脑本地硬盘   3 从不
// ═══════════════════════════════════════════
bool FileGrid::headerScanAllowed(const QString& dirPath) const {
    const int mode = AppSettings::instance().get("FileList/scanHeader", 0).toInt();
    if (mode == 0) return true;
    if (mode == 3) return false;
    QString root = dirPath;
    if (root.size() >= 2 && root[1] == QLatin1Char(':'))
        root = root.left(2) + QLatin1String("\\");
    const UINT type = GetDriveTypeW(reinterpret_cast<const wchar_t*>(root.utf16()));
    if (mode == 1)   // 排除软盘/CD/DVD:可移动介质上逐文件开门读头代价太高
        return type != DRIVE_REMOVABLE && type != DRIVE_CDROM && type != DRIVE_NO_ROOT_DIR;
    return type == DRIVE_FIXED;   // mode == 2:只认本地硬盘
}

// ═══════════════════════════════════════════
// 目录加载
// ═══════════════════════════════════════════
void FileGrid::loadDirectory(const QString& dirPath) {
    if (m_loading) return;
    m_loading = true;

    // 同一目录重载(refresh/删除后)才谈得上"新增文件":换目录时全部条目都是新的,
    // 若按 newAtEnd/autoSelectNew 处理会把整列表打乱、并抢走正常导航的选中项
    const bool sameDir = (m_currentDir == dirPath);
    QSet<QString> prevPaths;
    if (sameDir)
        for (const auto& e : m_allEntries) prevPaths.insert(e.path);

    m_currentDir = dirPath;

    // 清掉上一目录的缩略图任务
    Thumbnailer::instance().clearQueue();

    m_allEntries = fastScanDir(dirPath);

    // ── FileList/showSubFolders(树右键"显示子文件夹中的文件")──
    // 目录行仍只列本层,只有文件向下递归铺开。整棵子树的枚举代价由探针记账,
    // 逛巨型仓库时慢在哪一眼可见,不用靠猜。
    if (m_showSubFolders) {
        QStringList subDirs;
        subDirs.reserve(static_cast<int>(m_allEntries.size()));
        for (const auto& e : m_allEntries)
            if (e.isDir) subDirs << e.path;
        if (!subDirs.isEmpty()) {
            PerfLog::Scope probe("loadDir.subFolders", 50);
            for (const QString& d : subDirs)
                fastScanSubFiles(d, m_allEntries, !m_showHidden);
        }
    }

    // ── FileList/showSubFolders(树右键"显示子文件夹中的文件")──
    // 目录行仍只列本层,只有文件向下递归铺开。整棵子树的枚举代价由探针记账,
    // 逛巨型仓库时慢在哪一眼可见,不用靠猜。
    if (m_showSubFolders) {
        QStringList subDirs;
        subDirs.reserve(static_cast<int>(m_allEntries.size()));
        for (const auto& e : m_allEntries)
            if (e.isDir) subDirs << e.path;
        if (!subDirs.isEmpty()) {
            PerfLog::Scope probe("loadDir.subFolders", 50);
            for (const QString& d : subDirs)
                fastScanSubFiles(d, m_allEntries, !m_showHidden);
        }
    }

    // ── FileList/recognizeByExt(默认开)= 只看扩展名 ──
    // 关掉时按文件头魔数判定真实格式(扩展名被改错/缺失仍能正确归类);
    // 是否允许读头由 FileList/scanHeader 按卷类型决定(软盘/光盘默认不读,
    // 免得逐文件寻道把 removable 介质拖垮)
    if (!AppSettings::instance().get("FileList/recognizeByExt", true).toBool()
        && headerScanAllowed(dirPath)) {
        for (auto& e : m_allEntries) {
            if (e.isDir) continue;
            const QString real = sniffExtByHeader(e.path);
            if (!real.isEmpty()) e.ext = real;
        }
    }

    m_selected.clear();
    m_lastClicked = -1;

    // 颜色标记批量加载(目录前缀查询,一次 SQL)
    m_colorLabels = LabelStore::instance().colorsForDir(dirPath);
    for (auto& e : m_allEntries)
        e.colorLabel = m_colorLabels.value(e.path, 0);

    applyFilter();

    sort(m_sortCol, m_sortAsc);

    // ── FileList/newAtEnd / autoSelectNew(仅同一目录重载时生效)──
    // newAtEnd:新出现的条目不参与排序,整块挂到列表末尾(内部仍按当前排序规则)
    // autoSelectNew:重载后直接选中第一个新文件(监控下载/导出目录时常用)
    QStringList freshPaths;
    if (sameDir && !prevPaths.isEmpty()) {
        std::vector<FileEntry> rest, fresh;
        rest.reserve(m_entries.size());
        for (const auto& e : m_entries) {
            if (prevPaths.contains(e.path)) rest.push_back(e);
            else { fresh.push_back(e); freshPaths << e.path; }
        }
        if (!fresh.empty()
            && AppSettings::instance().get("FileList/newAtEnd", false).toBool()) {
            rest.insert(rest.end(), fresh.begin(), fresh.end());
            m_entries = std::move(rest);
            // 顺序被"新条目挪到末尾"改动,而 m_pathRow 只在 sort() 里建过:
            // 不重算的话,随后到达的缩略图回调按旧行号定点重绘 → 画到错的卡片
            m_pathRow.clear();
            m_pathRow.reserve(static_cast<int>(m_entries.size()));
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
                m_pathRow.insert(m_entries[i].path, i);
        }
    }

    // 清除缩略图缓存
    m_thumbCache.clear();
    m_thumbOrder.clear();
    m_fitCache.clear();
    m_hoverIdx = -1;

    m_loading = false;
    updateLayout();          // 重算列数/几何/滚动范围 + 重绘
    // 换目录必须归顶(#114):updateLayout 会保留旧滚动值,上个目录滚到中部时
    // 新目录一进来就停在同样的偏移上,首行永远看不见。同目录重载(刷新/删除)
    // 不动滚动,那是打断浏览。
    if (!sameDir)
        verticalScrollBar()->setValue(0);
    requestVisibleThumbs();
    // Thumbs/wholeFolder:开=进目录即为全部条目排缩略图(滚动到哪都有图,代价是
    // 进大目录时后台一下排满);关=只排视口内(默认,与改造前一致)
    if (AppSettings::instance().get("Thumbs/wholeFolder", false).toBool())
        requestAllThumbs();

    emit fileCountChanged();
    if (!m_entries.empty()) {
        // 默认选中第一个;reloadAfterDelete 可用 m_preferPath 指定落点
        int idx = 0;
        if (!m_preferPath.isEmpty()) {
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
                if (m_entries[i].path == m_preferPath) { idx = i; break; }
        }
        // FileList/autoSelectNew:有新文件则优先落到第一个新文件上
        if (m_preferPath.isEmpty() && !freshPaths.isEmpty()
            && AppSettings::instance().get("FileList/autoSelectNew", false).toBool()) {
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
                if (m_entries[i].path == freshPaths.first()) { idx = i; break; }
        }
        m_preferPath.clear();
        m_selected.insert(idx);
        m_lastClicked = idx;
        emit selectionChanged(m_entries[idx].path);
    } else {
        m_preferPath.clear();
        emit selectionChanged({});
    }
}

// 树右键"显示子文件夹中的文件"的落点:开关改变的是条目集合本身(整棵子树),
// 所以按"换目录"对待 —— 沿用同目录重载会让 newAtEnd 把刚展开的文件整块甩到末尾。
void FileGrid::setShowSubFolders(bool on) {
    if (m_showSubFolders == on) return;
    m_showSubFolders = on;
    AppSettings::instance().set("FileList/showSubFolders", on);
    const QString dir = m_currentDir;
    m_currentDir.clear();
    if (!dir.isEmpty()) loadDirectory(dir);
}

void FileGrid::deleteSelection() {
    // 作用域 = 整个选中集:右键"删除"删的就是这批,键盘若只删一项,
    // 确认框里"N 个项目"的数字和实际落盘结果会各说各话。
    QStringList paths = selectedPaths();
    if (paths.isEmpty() && m_lastClicked >= 0
        && m_lastClicked < static_cast<int>(m_entries.size()))
        paths = { m_entries[m_lastClicked].path };
    if (paths.isEmpty()) return;
    // 确认框/回收站由 FileOps/confirmDelete + FileOps/useRecycleBin 决定(与右键菜单同一入口)
    if (deleteWithSettings(paths, this)) {
        reloadAfterDelete(paths);
    }
}

void FileGrid::newFolder() {
    bool ok;
    const QString name = QInputDialog::getText(this, "新建文件夹", "名称:",
                                        QLineEdit::Normal, "新建文件夹", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (const QString why = invalidNameReason(name); !why.isEmpty()) {
        QMessageBox::warning(this, "新建文件夹", why);
        return;
    }
    // 一直用 m_currentDir:旧写法取"第一个条目的父目录",空目录时退回 home,
    // 于是看着空白文件夹点的"新建文件夹",东西建在了用户主目录里
    const QString dir = m_currentDir;
    if (dir.isEmpty()) return;
    const QString full = QDir(dir).filePath(name);
    if (QFileInfo::exists(full)) {
        QMessageBox::warning(this, "新建文件夹", QString::fromUtf8("同名文件夹已存在:\n") + full);
        return;
    }
    if (!QDir().mkdir(full)) {
        QMessageBox::warning(this, "新建文件夹", QString::fromUtf8("创建失败:\n") + full);
        return;
    }
    m_preferPath = full;
    loadDirectory(dir);
}

// ═══════════════════════════════════════════
// 属性
// ═══════════════════════════════════════════
void FileGrid::setCardSize(int size) {
    size = qBound(THUMB_W_MIN, size, THUMB_W_MAX);
    m_cardSizeAuto = size;   // 记录 slider 设定值(自动模式用;固定列数退出时抄回这里)
    m_cardSize = size;
    // 宽度只有一个持久化键,落盘就写在唯一的 setter 里:尺寸菜单/自定义对话框/
    // Ctrl+= /滚轮全都汇到这条,交给各调用点自己决定存不存,漏一个就是"设了不保存"
    AppSettings& st = AppSettings::instance();
    if (st.get("Appearance/customThumbW", 96).toInt() != size)
        st.set("Appearance/customThumbW", size);
    // 宽度只有一个持久化键,落盘就写在唯一的 setter 里:尺寸菜单/自定义对话框/
    // Ctrl+= /滚轮全都汇到这条,交给各调用点自己决定存不存,漏一个就是"设了不保存"
    AppSettings& st = AppSettings::instance();
    if (st.get("Appearance/customThumbW", 96).toInt() != size)
        st.set("Appearance/customThumbW", size);
    if (!m_entries.empty()) {
        m_fitCache.clear();  // 盒子变了,圆角成品图作废
        updateLayout();
        requestVisibleThumbs();
    }
}

int FileGrid::fileCount() const {
    return static_cast<int>(m_entries.size());
}

int FileGrid::selectedCount() const {
    return static_cast<int>(m_selected.size());
}

int64_t FileGrid::selectedSize() const {
    int64_t total = 0;
    for (int idx : m_selected)
        if (idx >= 0 && idx < static_cast<int>(m_entries.size()))
            total += m_entries[idx].size;
    return total;
}

QStringList FileGrid::selectedPaths() const {
    QStringList paths;
    for (int idx : m_selected)
        if (idx >= 0 && idx < static_cast<int>(m_entries.size()))
            paths << m_entries[idx].path;
    return paths;
}
