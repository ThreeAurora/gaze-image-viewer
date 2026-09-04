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

FileGrid::FileGrid(QWidget* parent) : QScrollArea(parent) {
    setStyleSheet(QString::fromUtf8("QScrollArea{background:%1;border:none;}")
                      .arg(C_CONTENT));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 与 XnView 一致：即使内容少于一页也保留竖向滚动条，无法拖动时显示为整条长拇指
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setWidgetResizable(false);

    // 启动默认排序(#150,设置→文件列表):0=文件名(升,默认) 1=修改日期(降)
    // 2=创建日期(降) 3=EXIF 拍摄日期(降) 4=类型 5=大小(降) 6=扩展名 7=路径
    // 8=颜色标签 9=记住上次(读 lastSortCol/lastSortAsc,由 sort() 随时落盘)
    switch (AppSettings::instance().get("Browser/startupSort", 0).toInt()) {
    case 1:  m_sortCol = SORT_MDATE;      m_sortAsc = false; break;
    case 2:  m_sortCol = SORT_CDATE;      m_sortAsc = false; break;
    case 3:  m_sortCol = SORT_EXIF;       m_sortAsc = false; break;
    case 4:  m_sortCol = SORT_TYPE;       m_sortAsc = true;  break;
    case 5:  m_sortCol = SORT_SIZE;       m_sortAsc = false; break;
    case 6:  m_sortCol = SORT_EXT;        m_sortAsc = true;  break;
    case 7:  m_sortCol = SORT_PATH;       m_sortAsc = true;  break;
    case 8:  m_sortCol = SORT_COLORLABEL; m_sortAsc = true;  break;
    case 9:  m_sortCol = AppSettings::instance().get("Browser/lastSortCol", SORT_NAME).toInt();
             m_sortAsc = AppSettings::instance().get("Browser/lastSortAsc", true).toBool();
             break;
    default: m_sortCol = SORT_NAME;       m_sortAsc = true;  break;
    }

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
    viewport()->installEventFilter(this);   // #267:滚动条显隐改变视口宽 → 重排+重推表头列
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
    m_filterMode = qBound(int(FILTER_ALL), AppSettings::instance().get("Browser/filterMode", int(FILTER_ALL)).toInt(), int(FILTER_CUSTOM));
    m_spacing   = qBound(0, AppSettings::instance().get("Appearance/spacing", 6).toInt(), 40);
    m_showHidden  = AppSettings::instance().get("FileList/showHidden", true).toBool();
    m_mixSort     = AppSettings::instance().get("FileList/mixSort", false).toBool();
    m_folderAlpha = AppSettings::instance().get("FileList/folderAlphabetical", true).toBool();
    m_showSubFolders = AppSettings::instance().get("FileList/showSubFolders", false).toBool();
    // Appearance/customThumbH:0=与宽同高(默认),>0=按设置值定缩略图框高
    m_thumbH      = qBound(0, AppSettings::instance()
                        .get("Appearance/customThumbH", 0).toInt(), 512);
    // Browser/thumbScrollPreview:滚动过程中要不要就出缩略图
    m_scrollPreview = AppSettings::instance()
                        .get("Browser/thumbScrollPreview", true).toBool();
    m_lastByExt = AppSettings::instance().get("FileList/recognizeByExt", true).toBool();
    m_lastScanHeader = AppSettings::instance().get("FileList/scanHeader", 0).toInt();
    applyAppearance();   // 逐条目绘制路径只读缓存,这里先灌一次
    // 自定义缩略图宽度:启动即生效(原先只记初值不应用,于是设置页/自定义对话框
    // 里存的宽度要等下一次任意设置变更才落地,启动时总是回落到硬编码 160)。
    // 构造期没有 viewport,不能走 setCardSize(它会 updateLayout),直接灌字段
    m_lastCustomW = qBound(THUMB_W_MIN,
        AppSettings::instance().get("Appearance/customThumbW", 96).toInt(), THUMB_W_MAX);
    m_cardSize = m_cardSizeAuto = m_lastCustomW;

    // 设置活应用:标签颜色 + 外观间距 + 文件列表规则在设置页改动后立即生效
    // (此前只重涂标签底色,其余设置都要重启)
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        LabelColors::reload();
        applyAppearance();
        AppSettings& st = AppSettings::instance();
        const int sp = qBound(0, st.get("Appearance/spacing", 6).toInt(), 40);
        const bool spacingChanged = (sp != m_spacing);
        m_spacing = sp;
        const bool hidden = st.get("FileList/showHidden", true).toBool();
        const bool mix    = st.get("FileList/mixSort", false).toBool();
        const bool alpha  = st.get("FileList/folderAlphabetical", true).toBool();
        const bool listChanged = (hidden != m_showHidden) || (mix != m_mixSort)
                              || (alpha != m_folderAlpha);
        m_showHidden = hidden; m_mixSort = mix; m_folderAlpha = alpha;
        // 外观页"自定义缩略图尺寸 - 宽":值变了才应用(setCardSize 也写这个键,
        // 回到这里时 cw == m_cardSize,不会二次重排)
        const int cw = qBound(THUMB_W_MIN,
                              st.get("Appearance/customThumbW", 96).toInt(), THUMB_W_MAX);
        if (cw != m_lastCustomW) {
            m_lastCustomW = cw;
            if (cw != m_cardSize) setCardSize(cw);
        }
        // 外观页"自定义缩略图尺寸 - 高":改了才重排(0=与宽同高)。
        // 注意不能提前 return —— 用户一次可能连改多项,后面的键也都要应用
        bool needRescan = false;
        const int ch = qBound(0, st.get("Appearance/customThumbH", 0).toInt(), 512);
        if (ch != m_thumbH) {
            m_thumbH = ch;
            relayoutNow();
            requestVisibleThumbs();
        }
        // Browser/thumbScrollPreview:改的是滚动补图时机,重设合批间隔即可
        const bool sp2 = st.get("Browser/thumbScrollPreview", true).toBool();
        if (sp2 != m_scrollPreview) {
            m_scrollPreview = sp2;
            m_scrollCoalesce.setInterval(sp2 ? 0 : 120);
        }
        // FileList/recognizeByExt 或 scanHeader 变了 → 按新判定重扫当前目录
        const bool byExt = st.get("FileList/recognizeByExt", true).toBool();
        const int  hdr   = st.get("FileList/scanHeader", 0).toInt();
        if (byExt != m_lastByExt || hdr != m_lastScanHeader) {
            m_lastByExt = byExt; m_lastScanHeader = hdr;
            needRescan = true;
        }
        if (needRescan) {
            refreshCurrentDir();
            return;
        }
        if (listChanged) {
            applyFilter();
            sort(m_sortCol, m_sortAsc);
        } else if (spacingChanged) {
            updateLayout();
        }
        refreshView();   // 外观(边框粗细/对齐/颜色标记圈)变化只影响绘制
    });
}

// 外观缓存刷入(绘制路径逐项读取,禁在逐条目路径读 ini)
void FileGrid::applyAppearance() {
    AppSettings& st = AppSettings::instance();
    m_border     = qBound(0, st.get("Appearance/borderSize", 0).toInt(), 10);
    m_imageAlign = qBound(0, st.get("Appearance/imageAlign", 1).toInt(), 2);
    m_labelAlign = qBound(0, st.get("Appearance/labelAlign", 1).toInt(), 2);
    m_labelGap   = st.get("Appearance/labelSpacing", true).toBool() ? 6 : 0;
    m_showRating = st.get("Browser/showRating", true).toBool();
    m_sizeBytes  = st.get("FileList/sizeInBytes", false).toBool();
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

void FileGrid::refreshCurrentDir() {
    if (!m_currentDir.isEmpty()) loadDirectory(m_currentDir);
}

// 主题切换:画布背景是构造期内联样式表(全局 QSS 刷新覆盖不到),按新色重灌 + 重绘
void FileGrid::refreshThemeColors() {
    if (m_canvas)
        m_canvas->setStyleSheet(QString::fromUtf8("background:%1;").arg(C_CONTENT));
    refreshView();
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
    const QString name = QInputDialog::getText(this, gazeTr("新建文件夹"), gazeTr("名称:"),
                                        QLineEdit::Normal, gazeTr("新建文件夹"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (const QString why = invalidNameReason(name); !why.isEmpty()) {
        QMessageBox::warning(this, gazeTr("新建文件夹"), why);
        return;
    }
    // 一直用 m_currentDir:旧写法取"第一个条目的父目录",空目录时退回 home,
    // 于是看着空白文件夹点的"新建文件夹",东西建在了用户主目录里
    const QString dir = m_currentDir;
    if (dir.isEmpty()) return;
    const QString full = QDir(dir).filePath(name);
    if (QFileInfo::exists(full)) {
        QMessageBox::warning(this, gazeTr("新建文件夹"), gazeTr("同名文件夹已存在:\n") + full);
        return;
    }
    if (!QDir().mkdir(full)) {
        QMessageBox::warning(this, gazeTr("新建文件夹"), gazeTr("创建失败:\n") + full);
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
