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
#include <QDateTime>
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
#include <QPointer>
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include "filegrid_internal.h"

FileGrid::FileGrid(QWidget* parent) : QScrollArea(parent) {
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
    case 9:  // 记住上次:开关打开才用落盘的上次排序;关=始终按启动默认(文件名升序)
             if (AppSettings::instance().get("Browser/rememberSort", false).toBool()) {
                 m_sortCol = AppSettings::instance()
                     .get("Browser/lastSortCol", SORT_NAME).toInt();
                 m_sortAsc = AppSettings::instance()
                     .get("Browser/lastSortAsc", true).toBool();
             } else {
                 m_sortCol = SORT_NAME; m_sortAsc = true;
             }
             break;
    default: m_sortCol = SORT_NAME;       m_sortAsc = true;  break;
    }

    m_canvas = new FileCanvas(this);
    // QSS 命中锚点:FileCanvas 无 Q_OBJECT,Qt 类型选择器(FileCanvas{...})对
    // 它不生效,且 QScrollArea 的 viewport 是裸 QWidget —— 两者都会落进全局
    // QWidget{background:%2}=#212126(33,33,38),把 2026-09-04 定的纯黑内容区
    // 盖回灰底(09-07 用户报"文件页背景应是 rgb(0,0,0) 却是 33,33,38")。
    // 改走 objectName 精确命中 theme.cpp 里 QWidget#fileCanvas 规则,与
    // C_CONTENT(纯黑)令牌绑定,双主题都跟主题走。
    m_canvas->setObjectName(QStringLiteral("fileCanvas"));
    setWidget(m_canvas);
    // 同上的 QSS 命中问题:QScrollArea 的 viewport 是裸 QWidget,吃全局
    // QWidget{background:%2} 灰底;内容不足一屏时视口边缘会露出来。给它
    // 自己的 objectName,theme.cpp 里 QWidget#fileGridViewport 一条规则盖掉。
    viewport()->setObjectName(QStringLiteral("fileGridViewport"));

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

    // 悬停统计驻留闸:鼠标在一枚文件夹上停稳 450ms 才发起后台统计
    m_dirSizeTimer.setSingleShot(true);
    connect(&m_dirSizeTimer, &QTimer::timeout, this, [this]() {
        if (!m_dirSizeHoverPath.isEmpty())
            emit dirSizeRequested(m_dirSizeHoverPath);
    });

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
    // 文件夹排序位置:0置顶/1参与排序/2置底。老版本只有 FileList/mixSort 布尔
    // (真=混排),没存过新键时按旧开关迁移(2026-09-09 用户令三态)
    {
        const int v = AppSettings::instance().get("FileList/folderSortPos", -1).toInt();
        if (v >= 0 && v <= 2) {
            m_folderSortPos = v;
        } else {
            m_folderSortPos = AppSettings::instance()
                                  .get("FileList/mixSort", false).toBool() ? 1 : 0;
            AppSettings::instance().setPersist("FileList/folderSortPos",
                                               m_folderSortPos);   // 迁移落盘,下次直达
        }
    }
    m_folderAlpha = AppSettings::instance().get("FileList/folderAlphabetical", true).toBool();
    m_showSubFolders = AppSettings::instance().get("FileList/showSubFolders", false).toBool();
    // Appearance/customThumbH:0=与宽同高(默认),>0=按设置值定缩略图框高
    m_thumbH      = qBound(0, AppSettings::instance()
                        .get("Appearance/customThumbH", 0).toInt(), 512);
    // Browser/thumbScrollPreview:滚动过程中要不要就出缩略图
    m_scrollPreview = AppSettings::instance()
                        .get("Browser/thumbScrollPreview", true).toBool();
    m_lastByExt = AppSettings::instance().get("FileList/recognizeByExt", true).toBool();
    // 详细列表列宽:用户拖拽落下的记忆优先,没存过用基准死表(2026-09-06 用户令:
    // 不再自动算名称列宽,列宽归用户拖)
    {
        const QStringList csv = AppSettings::instance()
            .get("Browser/detailColW", QString()).toString().split(',');
        if (csv.size() == 6) {
            bool okAll = true;
            int v[6];
            for (int i = 0; i < 6; ++i) {
                v[i] = csv[i].toInt(&okAll);
                if (!okAll) break;
            }
            if (okAll)
                for (int i = 0; i < 6; ++i) m_dynColW[i] = qBound(32, v[i], 480);
        }
    }
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
        const int pos   = qBound(0, st.get("FileList/folderSortPos", 0).toInt(), 2);
        const bool alpha  = st.get("FileList/folderAlphabetical", true).toBool();
        const bool listChanged = (hidden != m_showHidden) || (pos != m_folderSortPos)
                              || (alpha != m_folderAlpha);
        m_showHidden = hidden; m_folderSortPos = pos; m_folderAlpha = alpha;
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

// 主题切换:静态底色已收敛进应用级 QSS,这里只重绘自绘缓存色(卡片笔刷等);
// 查找条图标是按主题染色的位图,换主题要重染一遍
void FileGrid::refreshThemeColors() {
    refreshView();
    if (m_findBar) {
        m_findPrev->setIcon(fg_impl::paintedArrow(QStyle::SP_ArrowUp));
        m_findNext->setIcon(fg_impl::paintedArrow(QStyle::SP_ArrowDown));
    }
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
// 目录加载(#6 异步化,2026-09-05)
//   旧实现整段跑在 GUI 线程:启动时主线程同步枚举 G 盘,盘响应慢时窗口灰着
//   挂几秒(用户报"启动有个灰色四边形,等几秒才出内容"),浏览中途进大目录
//   也一样卡。现在枚举/递归/嗅探全在池线程,旧内容保持显示;就绪后经
//   onDirScanDone 回 GUI 应用,代次对不上(期间又换了目录)的结果整批丢弃。
// ═══════════════════════════════════════════
void FileGrid::loadDirectory(const QString& dirPath) {
    m_loading = true;
    const quint64 gen = ++m_loadGen;   // 作废任何在途扫描

    // 同一目录重载(refresh/删除后)才谈得上"新增文件":换目录时全部条目都是新的,
    // 若按 newAtEnd/autoSelectNew 处理会把整列表打乱、并抢走正常导航的选中项
    const bool sameDir = (m_currentDir == dirPath);
    QSet<QString> prevPaths;
    if (sameDir)
        for (const auto& e : m_allEntries) prevPaths.insert(e.path);

    m_currentDir = dirPath;

    // 清掉上一目录的缩略图任务
    Thumbnailer::instance().clearQueue();

    // 工作线程捕获的全是值拷贝(设置项此刻读好),不碰任何 GUI 侧状态
    const bool expandSub = (m_showSubFolders || m_mfScope == 1);
    const bool skipHidden = !m_showHidden;
    const bool allowHdr = m_mfScope != 2
        && !AppSettings::instance().get("FileList/recognizeByExt", true).toBool()
        && headerScanAllowed(dirPath);
    m_dirScanInFlight = true;
    QPointer<FileGrid> self(this);
    QThreadPool::globalInstance()->start(
        [self, gen, dirPath, sameDir, prevPaths, expandSub, skipHidden, allowHdr]() {
        std::vector<FileEntry> scanned = fastScanDir(dirPath);
        // FileList/showSubFolders(树右键"显示子文件夹中的文件")/#242 范围1=
        // 当前目录(递归):目录行只列本层,文件向下递归铺开
        if (expandSub && !scanned.empty()) {
            QStringList subDirs;
            subDirs.reserve(static_cast<int>(scanned.size()));
            for (const auto& e : scanned)
                if (e.isDir) subDirs << e.path;
            for (const QString& d : subDirs)
                fastScanSubFiles(d, scanned, skipHidden);
        }
        // FileList/recognizeByExt 关=按文件头魔数判定真实格式(逐文件开门读,
        // 只在设置允许的卷上做);GUI 侧零阻塞
        if (allowHdr) {
            for (auto& e : scanned) {
                if (e.isDir) continue;
                const QString real = sniffExtByHeader(e.path);
                if (!real.isEmpty()) e.ext = real;
            }
        }
        QMetaObject::invokeMethod(self, [self, gen, dirPath, sameDir, prevPaths,
                                         scanned = std::move(scanned), allowHdr]() {
            if (self)
                self->onDirScanDone(gen, dirPath, sameDir, prevPaths,
                                    std::move(scanned), allowHdr);
        }, Qt::QueuedConnection);
    });

    // 扫描期间旧目录内容保持显示(首次启动为空白画布,paintCanvas 会写
    // "正在读取目录…");选中集在此不动,避免旧画面整行失去高亮
}

// 池线程扫描完成 → GUI 应用(代次不匹配=期间又换了目录,整批丢弃)
void FileGrid::onDirScanDone(quint64 gen, const QString& dirPath, bool sameDir,
                             const QSet<QString>& prevPaths,
                             std::vector<FileEntry> scanned, bool allowHdr) {
    if (gen != m_loadGen) return;   // 迟到的旧扫描:丢弃
    m_loading = false;
    m_dirScanInFlight = false;
    // 灰四边形排查:装载完成时刻+条目数(show 到这里之间文件页是空白画布/
    // "正在读取目录…"占位,这段越长用户看到的"灰块期"越久)
    Logger::event(QStringLiteral("dir-scan done: %1 entries=%2 age=%3ms")
                      .arg(dirPath).arg(scanned.size())
                      .arg(Logger::processAgeMs()));
    m_firstThumbLogged = false;   // 本次装载的首个缩略图打点待命

    m_allEntries = std::move(scanned);

    // ── #242 分类筛选器范围 2=全局:候选宇宙=标签库全表,只列其中仍存在、
    //   非目录的文件。本层扫描结果整个丢弃 —— 全局语义就是"跨目录找标记过的
    //   文件",混进本层未标记文件会让三档范围的边界含糊。已删除路径静默忽略。
    QHash<QString,int> globalColored;   // 全表色(下方直接复用,免二次查询)
    if (m_mfScope == 2) {
        globalColored = LabelStore::instance().allColored();
        m_allEntries.clear();
        m_allEntries.reserve(globalColored.size());
        QSet<QString> seen;
        seen.reserve(globalColored.size());
        for (auto it = globalColored.constBegin(); it != globalColored.constEnd(); ++it) {
            const QString clean = QDir::cleanPath(it.key());
            if (seen.contains(clean)) continue;
            seen.insert(clean);
            QFileInfo fi(clean);
            if (!fi.exists() || fi.isDir()) continue;
            FileEntry fe;
            fe.name = fi.fileName();
            fe.path = clean;
            const int dot = fe.name.lastIndexOf(QLatin1Char('.'));
            fe.ext = (dot > 0) ? fe.name.mid(dot).toLower() : QString();
            fe.hidden = fi.isHidden();
            fe.size = fi.size();
            const QDateTime lm = fi.lastModified();
            const QDateTime bt = fi.birthTime();
            fe.mtime = lm.isValid() ? double(lm.toSecsSinceEpoch()) : 0.0;
            fe.ctime = bt.isValid() ? double(bt.toSecsSinceEpoch()) : 0.0;
            fe.colorLabel = it.value();
            m_allEntries.push_back(fe);
        }
    }
    Q_UNUSED(allowHdr);   // 嗅探已在池线程做完(留参备将来 GUI 侧复查)

    m_selected.clear();
    m_lastClicked = -1;

    // 颜色标记批量加载(目录前缀查询,一次 SQL);#242 范围2的全表色已在上文取好
    m_colorLabels = (m_mfScope == 2)
        ? globalColored : LabelStore::instance().colorsForDir(dirPath);
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
    // 目录装载期间来的选中请求(启动恢复/双击打开/单实例转交):并入
    // preferPath,由下方默认首选块一次性选中 —— 不另发一次 selectionChanged。
    // 此前先 selectByPath 再默认选第一项,两次 emit 打架:双击打开的图片
    // 被第一项抢走预览,状态栏还剩两项选中(实测实锤)
    if (!m_pendingSelectPath.isEmpty()) {
        m_preferPath = m_pendingSelectPath;
        m_pendingSelectPath.clear();
    }
    if (!m_entries.empty()) {
        // 默认选中第一个;reloadAfterDelete 可用 m_preferPath 指定落点
        int idx = 0;
        if (!m_preferPath.isEmpty()) {
            // 盘根条目是 "X://name" 形,外部来源的落点(重命名/移动/新文件,
            // "X:/new")精确 == 失配 → 同 selectByPath 的 cleanPath 归一兜底
            const QString want = QDir::cleanPath(m_preferPath);
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
                if (m_entries[i].path == m_preferPath
                    || QDir::cleanPath(m_entries[i].path) == want) { idx = i; break; }
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
