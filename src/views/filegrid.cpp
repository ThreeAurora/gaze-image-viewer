#include "filegrid.h"
#include "contextmenu.h"
#include "thumbnailer.h"
#include "livephoto.h"
#include "labelstore.h"
#include "settings.h"
#include "constants.h"
#include "shelldelete.h"
#include "exifdate.h"
#include "perflog.h"

#include <set>
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
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QApplication>
#include <QProcess>
#include <QPainter>
#include <QPainterPath>
#include <QCollator>
#include <algorithm>
#include <cmath>
#include "filegrid_internal.h"

FileGrid::FileGrid(QWidget* parent) : QScrollArea(parent) {
    setStyleSheet("QScrollArea{background:" C_CONTENT ";border:none;}");
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 与 XnView 一致：即使内容少于一页也保留竖向滚动条，无法拖动时显示为整条长拇指
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setWidgetResizable(false);

    m_canvas = new QWidget;
    m_canvas->setStyleSheet("background:" C_CONTENT ";");
    setWidget(m_canvas);

    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]() {
        // 滚动中(滚轮/拖动滚动条):推迟缩略图解码防洪流卡顿;
        // 停止 120ms 后由 m_scrollTimer 批量补齐(拖动滚动条原未防抖,卡顿主因)
        m_scrollSettled = false;
        m_scrollTimer.start();
        layoutCards();
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
    // 注意是 0ms(合并)而不是防抖等待——等就是拖尾。
    m_scrollCoalesce.setSingleShot(true);
    m_scrollCoalesce.setInterval(0);
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

// 设置改动后的重排:重算列宽 + 重排可见卡片
void FileGrid::relayoutNow() {
    m_cols = 0;
    updateLayout();
    layoutCards();
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
}

// ═══════════════════════════════════════════
// 目录加载
// ═══════════════════════════════════════════
void FileGrid::loadDirectory(const QString& dirPath) {
    if (m_loading) return;
    m_loading = true;
    m_currentDir = dirPath;

    // 清掉上一目录的缩略图任务
    Thumbnailer::instance().clearQueue();

    m_allEntries = fastScanDir(dirPath);
    m_selected.clear();
    m_lastClicked = -1;

    // 颜色标记批量加载(目录前缀查询,一次 SQL)
    m_colorLabels = LabelStore::instance().colorsForDir(dirPath);
    for (auto& e : m_allEntries)
        e.colorLabel = m_colorLabels.value(e.path, 0);

    applyFilter();

    sort(m_sortCol, m_sortAsc);
    updateLayout();

    // 清除缩略图缓存
    m_thumbCache.clear();
    recycleCards();

    m_loading = false;
    layoutCards();

    emit fileCountChanged();
    if (!m_entries.empty()) {
        // 默认选中第一个;reloadAfterDelete 可用 m_preferPath 指定落点
        int idx = 0;
        if (!m_preferPath.isEmpty()) {
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
                if (m_entries[i].path == m_preferPath) { idx = i; break; }
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

void FileGrid::toggleMark(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    QString path = m_entries[index].path;
    if (m_marked.contains(path))
        m_marked.remove(path);
    else
        m_marked.insert(path);
    for (auto it = m_active.begin(); it != m_active.end(); ++it)
        if (it.key() == index)
            it.value()->setMarked(m_marked.contains(path));
}

void FileGrid::clearAllMarks() {
    m_marked.clear();
    for (auto* card : m_active)
        card->setMarked(false);
    if (m_filterMarked) toggleFilter();
}

void FileGrid::toggleMark(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    QString path = m_entries[index].path;
    if (m_marked.contains(path))
        m_marked.remove(path);
    else
        m_marked.insert(path);
    for (auto it = m_active.begin(); it != m_active.end(); ++it)
        if (it.key() == index)
            it.value()->setMarked(m_marked.contains(path));
}

void FileGrid::clearAllMarks() {
    m_marked.clear();
    for (auto* card : m_active)
        card->setMarked(false);
    if (m_filterMarked) toggleFilter();
}

void FileGrid::deleteFile(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    QString path = m_entries[index].path;
    // 确认框/回收站由 FileOps/confirmDelete + FileOps/useRecycleBin 决定(与右键菜单同一入口)
    QFileInfo fi(path);
    QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (deleteWithSettings({path}, this)) {
        m_selected.remove(index);
        m_marked.remove(path);
        loadDirectory(dir);
    }
}

void FileGrid::newFolder() {
    bool ok;
    QString name = QInputDialog::getText(this, "新建文件夹", "名称:",
                                         QLineEdit::Normal, "新建文件夹", &ok);
    if (!ok || name.isEmpty()) return;
    QString dir = m_entries.empty() ? QDir::homePath()
                  : QFileInfo(m_entries[0].path).absolutePath();
    QString full = QDir(dir).filePath(name);
    if (QDir().mkdir(full))
        loadDirectory(dir);
}

// ═══════════════════════════════════════════
// 属性
// ═══════════════════════════════════════════
void FileGrid::setCardSize(int size) {
    m_cardSizeAuto = size;   // 记录 slider 设定值(自动模式用)
    m_cardSize = std::max(80, std::min(300, size));
    if (!m_entries.empty()) {
        updateLayout();
        recycleCards();
        layoutCards();
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
