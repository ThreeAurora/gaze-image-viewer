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
#include <QPointer>

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
// 筛选:按 m_filterMode 从 m_allEntries 生成 m_entries
// ═══════════════════════════════════════════
// 压缩档集合:单模式 FILTER_ARCHIVES 与 #242 多筛选的类型维共用(口径必须一致)
static const std::set<QString> kArchiveExts = {
    ".zip", ".rar", ".7z", ".gz", ".tar", ".tgz", ".cbz", ".cbr"
};

void FileGrid::applyFilter() {
    auto isImg = [](const FileEntry& e) { return IMAGE_EXTS.count(e.ext) > 0; };
    auto isVid = [](const FileEntry& e) { return VIDEO_EXTS.count(e.ext) > 0; };
    auto isAud = [](const FileEntry& e) { return AUDIO_EXTS.count(e.ext) > 0; };
    auto isDoc = [](const FileEntry& e) { return DOCUMENT_EXTS.count(e.ext) > 0; };
    auto isExe = [](const FileEntry& e) { return EXECUTABLE_EXTS.count(e.ext) > 0; };
    // #125 自定义筛选:ini "Browser/customExts" 是用户自己的扩展名清单,
    //   逗号/分号/空格分隔、点可带可不带、大小写不敏感。只在这里解析一次 ——
    //   逐条目读 ini 是 #75 那一类卡顿的老路。
    std::set<QString> custom;
    if (m_filterMode == FILTER_CUSTOM) {
        const QString s = AppSettings::instance()
                              .get("Browser/customExts", QString()).toString();
        const QStringList l1 = s.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& a : l1) {
            const QStringList l2 = a.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString& b : l2) {
                const QStringList l3 = b.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                for (const QString& c : l3) {
                    QString e = c.trimmed().toLower();
                    while (e.startsWith('.')) e.remove(0, 1);
                    if (!e.isEmpty()) custom.insert('.' + e);   // e.ext 带点
                }
            }
        }
    }

    m_entries.clear();
    for (const auto& e : m_allEntries) {
        // FileList/showHidden=关:隐藏属性/点开头的条目任何筛选下都不显示
        if (e.hidden && !m_showHidden) continue;
        bool ok = true;
        switch (m_filterMode) {
        case FILTER_ALL:          ok = true; break;
        case FILTER_IMAGES:       ok = !e.isDir && isImg(e); break;
        case FILTER_IMAGES_DIRS:  ok = e.isDir || isImg(e); break;
        case FILTER_VIDEOS:       ok = !e.isDir && isVid(e); break;
        case FILTER_VIDEOS_DIRS:  ok = e.isDir || isVid(e); break;
        case FILTER_AUDIO:        ok = !e.isDir && isAud(e); break;
        case FILTER_ARCHIVES:     ok = !e.isDir && kArchiveExts.count(e.ext) > 0; break;
        case FILTER_DOCUMENTS:    ok = !e.isDir && isDoc(e); break;
        case FILTER_EXECUTABLES:  ok = !e.isDir && isExe(e); break;
        case FILTER_FOLDERS:      ok = e.isDir; break;
        case FILTER_RED:          ok = e.colorLabel == 1; break;
        case FILTER_ORANGE:       ok = e.colorLabel == 2; break;
        case FILTER_YELLOW:       ok = e.colorLabel == 3; break;
        case FILTER_GREEN:        ok = e.colorLabel == 4; break;
        case FILTER_BLUE:         ok = e.colorLabel == 5; break;
        case FILTER_UNRED:        ok = e.colorLabel != 1; break;
        case FILTER_CUSTOM:       ok = !e.isDir && custom.count(e.ext) > 0; break;
        }
        // #242 第二层筛选:目录行不参与条件判定恒显示(递归范围下目录行是导航骨架)
        if (ok && m_mf.active && !e.isDir) ok = mfMatch(e);
        if (ok) m_entries.push_back(e);
    }
    m_geomDirty = true;      // 内容变了,几何待重建
}

void FileGrid::setFilterMode(int mode) {
    if (m_filterMode == mode) return;
    m_filterMode = mode;
    AppSettings::instance().setPersist("Browser/filterMode", mode);   // #106:筛选模式落盘(#107排查缺口)
    m_selected.clear();
    m_lastClicked = -1;
    applyFilter();
    sort(m_sortCol, m_sortAsc);
    m_thumbCache.clear();
    m_thumbOrder.clear();
    m_fitCache.clear();
    m_hoverIdx = -1;
    updateLayout();
    requestVisibleThumbs();
    emit fileCountChanged();
    if (!m_entries.empty()) {
        m_selected.insert(0);
        m_lastClicked = 0;
        emit selectionChanged(m_entries[0].path);
    } else {
        emit selectionChanged({});
    }
    emit filterModeChanged(mode);
}

// ═══════════════════════════════════════════
// #242 分类筛选器:第二层筛选(与单模式 filter 相与)
// ═══════════════════════════════════════════

// 单条目判命中。颜色维/类型维各自"任一勾选命中";两维全空不会走到这
// (active=false 时 applyFilter 不调)。OR=任一维命中;AND=每个有勾选的维都要命中。
bool FileGrid::mfMatch(const FileEntry& e) const {
    auto catOf = [](const FileEntry& fe) -> int {
        // 0=图像(含 RAW:筛选器面向"找图",RAW 也是图,有意与单模式 FILTER_IMAGES 不同)
        if (IMAGE_EXTS.count(fe.ext) || RAW_EXTS.count(fe.ext)) return 0;
        if (VIDEO_EXTS.count(fe.ext))      return 1;
        if (AUDIO_EXTS.count(fe.ext))      return 2;
        if (DOCUMENT_EXTS.count(fe.ext))   return 3;
        if (EXECUTABLE_EXTS.count(fe.ext)) return 4;
        if (kArchiveExts.count(fe.ext))    return 5;
        return -1;
    };
    const bool colorHit = m_mf.colors.contains(e.colorLabel);
    bool catHit = false;
    if (!m_mf.cats.isEmpty()) {
        const int c = catOf(e);
        catHit = (c >= 0 && m_mf.cats.contains(c));
    }
    if (m_mf.andMode) {
        if (!m_mf.colors.isEmpty() && !colorHit) return false;
        if (!m_mf.cats.isEmpty() && !catHit) return false;
        return true;
    }
    return colorHit || catHit;
}

// 条件变化后的重筛收尾 —— 与 setFilterMode 尾段同款(清选中→筛→排→清缓存→
// 重排→发计数→选首项),但不落盘、不发 filterModeChanged(单模式没有动)
void FileGrid::refilterForMulti() {
    m_selected.clear();
    m_lastClicked = -1;
    applyFilter();
    sort(m_sortCol, m_sortAsc);
    m_thumbCache.clear();
    m_thumbOrder.clear();
    m_fitCache.clear();
    m_hoverIdx = -1;
    updateLayout();
    requestVisibleThumbs();
    emit fileCountChanged();
    if (!m_entries.empty()) {
        m_selected.insert(0);
        m_lastClicked = 0;
        emit selectionChanged(m_entries[0].path);
    } else {
        emit selectionChanged({});
    }
}

void FileGrid::setMultiFilter(const MultiFilterSpec& spec) {
    MultiFilterSpec s = spec;
    s.active = !s.colors.isEmpty() || !s.cats.isEmpty();
    if (s.colors == m_mf.colors && s.cats == m_mf.cats
        && s.andMode == m_mf.andMode && s.active == m_mf.active) return;
    m_mf = s;
    refilterForMulti();
}

void FileGrid::clearMultiFilter() {
    if (m_mf.colors.isEmpty() && m_mf.cats.isEmpty() && m_mfScope == 0) return;
    m_mf = MultiFilterSpec{};
    if (m_mfScope == 0) { refilterForMulti(); return; }
    m_mfScope = 0;   // 范围复位:全局/递归注入的条目要重扫掉
    const QString dir = m_currentDir;
    m_currentDir.clear();
    if (!dir.isEmpty()) loadDirectory(dir);
}

void FileGrid::setMultiFilterScope(int scope) {
    scope = qBound(0, scope, 2);
    if (scope == m_mfScope) return;
    m_mfScope = scope;   // 0本层 1递归 2全局:条目宇宙变了,按换目录重扫
    const QString dir = m_currentDir;
    m_currentDir.clear();
    if (!dir.isEmpty()) loadDirectory(dir);
}

// ═══════════════════════════════════════════
// 排序
// ═══════════════════════════════════════════

// EXIF 拍摄日期(缓存真源 m_exifCache,0=已知无 EXIF 不再重试;无值时 mtime 回退)。
// 比较器 O(n log n) 次调用,绝不能每次读盘 64KB;#267 详细列表绘制同查此缓存。
// 封顶:进程内跨目录累积不清理会一路涨。上限远大于任一正常单目录条目数,
// 故一次 sort 内不会中途清空(不触发重复读盘),只在长期逛很多目录后回收
double FileGrid::exifDateOf(const FileEntry& e) {
    if (e.isDir) return 0;
    auto it = m_exifCache.find(e.path);
    if (it != m_exifCache.end()) return *it > 0 ? *it : e.mtime;
    const double t = ExifDate::dateTimeOriginal(e.path);
    if (m_exifCache.size() > 50000) m_exifCache.clear();
    m_exifCache.insert(e.path, t > 0 ? t : 0);
    return t > 0 ? t : e.mtime;
}

// ── #267 详细列表 EXIF 列:后台预填视口内缺失项 ──
// 绘制路径只查 m_exifCache(零 IO 铁律);这里排 QThreadPool 读盘,到达后
// 按代作废(过期只清 pending 不重绘),命中行定点重绘。防悬垂同 infopanel
// 先例:QPointer 自捕 + QueuedConnection 编组回 GUI 线程
void FileGrid::exifPrefillVisible() {
    if (!m_canvas || m_entries.empty()) return;
    ensureGeometry();
    const QRect vis(0, verticalScrollBar()->value(),
                    viewport()->width(), viewport()->height());
    ++m_exifGen;
    const quint64 gen = m_exifGen;
    int queued = 0;
    for (int k = lowerBoundRow(vis.top() - m_maxCardH);
         k < static_cast<int>(m_byY.size()); ++k) {
        const int i = m_byY[k];
        const QRect& r = m_geom[i];
        if (r.top() > vis.bottom()) break;
        if (r.isNull() || !r.intersects(vis)) continue;
        const FileEntry& e = m_entries[i];
        if (e.isDir || m_exifCache.contains(e.path)
            || m_exifPending.contains(e.path)) continue;
        if (++queued > 200) break;   // 单轮上限:滚动中不追读全目录,停稳后下轮补
        m_exifPending.insert(e.path);
        const QString path = e.path;
        QPointer<FileGrid> self(this);
        QThreadPool::globalInstance()->start([self, path, gen]() {
            const double t = ExifDate::dateTimeOriginal(path);
            // context 传 QPointer:self 已析构时 invokeMethod 直接丢弃回调;
            // 传裸 this 会在池线程解引用悬垂指针(Qt 内部要取 context->thread())
            QMetaObject::invokeMethod(self, [this, self, path, gen, t]() {
                m_exifPending.remove(path);
                if (!self || gen != m_exifGen) return;   // 期间换了目录/又重排了一轮
                if (m_exifCache.size() > 50000) m_exifCache.clear();
                m_exifCache.insert(path, t > 0 ? t : 0);
                const int row = m_pathRow.value(path, -1);
                if (row < 0) return;
                ensureGeometry();   // 排序后异步重绘未 flush 前到达,别按旧几何算脏矩形
                m_canvas->update(cardRect(row));
            }, Qt::QueuedConnection);
        });
    }
}

// 通用三路比较:相等返回 0。旧实现 `return ascending ? result : !result`
// 在相等元素上恒返回 true,破坏 strict weak ordering(std::stable_sort UB),
// 且默认路径(修改日期+降序)相等元素极多,一直在踩
void FileGrid::sort(int column, bool ascending) {
    PerfLog::Scope _perf("FileGrid::sort", 100);
    m_sortCol = column;
    m_sortAsc = ascending;
    // 记住上次(#150):排序随时落盘(setPersist 不广播),启动默认排序=
    // "记住上次"时下次启动由构造函数读回。QSettings 写的是内存缓冲,逐目录
    // 调用无落盘代价(#75 的教训只针对同步刷盘/逐条目读)
    AppSettings::instance().setPersist("Browser/lastSortCol", column);
    AppSettings::instance().setPersist("Browser/lastSortAsc", ascending);
    // 表头方向箭头同步:程序侧排序(启动默认/排序菜单/名称顺序)也走这里,
    // 不再让表头停在构造默认的"修改日期"(2026-09-06 用户报)
    if (m_header) m_header->setSortIndicator(column, ascending);

    QCollator collNormal;
    collNormal.setCaseSensitivity(Qt::CaseInsensitive);

    auto cmp3 = [](auto x, auto y) -> int {
        if (x < y) return -1;
        if (y < x) return 1;
        return 0;
    };
    auto nameCmp = [&](const QString& an, const QString& bn) -> int {
        switch (m_nameOrder) {
        case NameNatural: return naturalNameCompare(an, bn);   // 1 < 2 < 10(#98)
        case NameAlpha:   return an.compare(bn, Qt::CaseInsensitive);
        default:          return collNormal.compare(an, bn);
        }
    };

    auto cmp = [&](const FileEntry& a, const FileEntry& b) -> bool {
        // 目录位置策略:FileList/folderSortPos
        // 0=置顶:目录恒在前;1=参与排序:目录与文件按当前列混排;2=置底:目录恒在后。
        if (a.isDir != b.isDir) {
            if (m_folderSortPos != 1)           // 参与排序时目录不做特别处理,落 "按列排"
                return m_folderSortPos == 0 ? a.isDir : !a.isDir;
        } else if (a.isDir && m_folderAlpha && m_folderSortPos != 1) {
            // 目录间恒按名称排(置顶/置底两组内部保持 folderAlphabetical 语义)
            int dc = nameCmp(a.name, b.name);
            return ascending ? (dc < 0) : (dc > 0);
        }

        int c;
        switch (column) {
        case SORT_SIZE: {
            // #248(2026-09-10):目录按精确体积参与大小排序 —— 优先吃 Everything
            // 秒查/统计回填的 m_dirSizes,没有则先按条目自带 size 兜底;补值到达后
            // setDirSize 会启动合并重排,文件夹自动归位到真实位次
            const auto szOf = [this](const FileEntry& e) -> qint64 {
                if (!e.isDir) return e.size;
                const auto it = m_dirSizes.constFind(e.path);
                return it != m_dirSizes.constEnd() ? *it : e.size;
            };
            c = cmp3(szOf(a), szOf(b));
            break;
        }
        case SORT_TYPE: c = mimeType(a.ext).compare(mimeType(b.ext)); break;
        case SORT_EXT:  c = a.ext.compare(b.ext); break;
        case SORT_CDATE: c = cmp3(a.ctime, b.ctime); break;
        case SORT_MDATE: c = cmp3(a.mtime, b.mtime); break;
        case SORT_PATH: c = a.path.compare(b.path, Qt::CaseInsensitive); break;
        case SORT_EXIF:
        case SORT_EXIFMOD:
            c = cmp3(exifDateOf(a), exifDateOf(b)); break;
        case SORT_IMGSIZE: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            c = cmp3(qint64(sa.width()) * sa.height(), qint64(sb.width()) * sb.height());
            break;
        }
        case SORT_WIDTH: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            c = cmp3(sa.width(), sb.width());
            break;
        }
        case SORT_HEIGHT: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            c = cmp3(sa.height(), sb.height());
            break;
        }
        case SORT_RATIO: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            double ra = sa.height() > 0 ? double(sa.width()) / sa.height() : 0;
            double rb = sb.height() > 0 ? double(sb.width()) / sb.height() : 0;
            c = cmp3(ra, rb);
            break;
        }
        case SORT_ORIENTATION: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            c = cmp3(int(sa.width() >= sa.height()), int(sb.width() >= sb.height()));
            break;
        }
        case SORT_PRINTSIZE: {
            QSize sa = a.isDir ? QSize() : imageSize(a.path);
            QSize sb = b.isDir ? QSize() : imageSize(b.path);
            c = cmp3(sa.width(), sb.width());   // 打印尺寸暂按宽度
            break;
        }
        case SORT_COLORLABEL:
            c = cmp3(a.colorLabel, b.colorLabel); break;
        case SORT_COMMENT: case SORT_CUSTOM:
        case SORT_NAME:
        default:
            c = nameCmp(a.name, b.name); break;
        }
        // 同值必须有确定次序:fastScanDir 拿的是 NTFS 目录序(纯字典序 1,10,2),
        // stable_sort 会把它原样露出来 —— 默认"修改日期降序"下批量复制/下载的
        // 一批文件 mtime 完全相同,整屏就按 1,10,2 排(#98 的实测症状)。
        // 副键恒为升序:翻转主键方向时不应把同值组的名字顺序也倒过来。
        if (c == 0) return naturalNameCompare(a.name, b.name) < 0;
        return ascending ? (c < 0) : (c > 0);
    };

    // 重排前抓路径:m_selected/m_lastClicked/m_hoverIdx 存的是"行索引",
    // stable_sort 后同一索引指向别的文件 → 高亮错位、Delete/Copy/上色命中错文件
    QStringList selPaths;
    for (int idx : m_selected)
        if (idx >= 0 && idx < static_cast<int>(m_entries.size()))
            selPaths << m_entries[idx].path;
    const QString lastPath = (m_lastClicked >= 0 && m_lastClicked < static_cast<int>(m_entries.size()))
                             ? m_entries[m_lastClicked].path : QString();
    const QString hoverPath = (m_hoverIdx >= 0 && m_hoverIdx < static_cast<int>(m_entries.size()))
                              ? m_entries[m_hoverIdx].path : QString();

    std::stable_sort(m_entries.begin(), m_entries.end(), cmp);

    // 顺序变了:重建 path→行号(缩略图回调按它定点重绘)+ 标脏几何
    m_pathRow.clear();
    m_pathRow.reserve(static_cast<int>(m_entries.size()));
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        m_pathRow.insert(m_entries[i].path, i);

    // 选中标号按路径映射回新行号:文件本身不变,高亮/操作仍指向原那批
    m_selected.clear();
    for (const QString& p : selPaths) {
        const int r = m_pathRow.value(p, -1);
        if (r >= 0) m_selected.insert(r);
    }
    if (!lastPath.isEmpty()) m_lastClicked = m_pathRow.value(lastPath, -1);
    m_hoverIdx = hoverPath.isEmpty() ? -1 : m_pathRow.value(hoverPath, -1);

    m_geomDirty = true;
    refreshView();

    // #248:大小排序下未吃过精确体积的目录一次性请求(可发几百个),主窗 pending
    // 去重 + 持久库 mtime 快筛兜底;值回来 setDirSize → m_dirResortTimer 合并重排
    if (column == SORT_SIZE) {
        for (const FileEntry& e : m_entries) {
            if (e.isDir && !m_dirSizes.contains(e.path)
                        && !m_dirSizeAsked.contains(e.path)) {
                m_dirSizeAsked.insert(e.path);
                emit dirSizeRequested(e.path);
            }
        }
    }
}

void FileGrid::setNameOrder(int order) {
    if (m_nameOrder == order) return;
    m_nameOrder = qBound(0, order, int(NameNormal));
    AppSettings::instance().set("Browser/nameOrder", m_nameOrder);
    sort(SORT_NAME, m_sortAsc);
}

