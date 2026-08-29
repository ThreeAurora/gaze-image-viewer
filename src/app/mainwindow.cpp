#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "previewpanel.h"
#include "imgsearchdialog.h"
#include "printdialog.h"
#include "infopanel.h"
#include "shelldelete.h"   // showDeleteToast:拖放复制成功的左下角提示
#include "sortheader.h"
#include "fileentry.h"
#include "livephoto.h"
#include "constants.h"
#include "validname.h"
#include "keytarget.h"
#include "logger.h"
#include "logger.h"
#include "validname.h"
#include "keytarget.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QComboBox>
#include <QMessageBox>
#include <QDialog>
#include <QAbstractButton>
#include <QDialog>
#include <QAbstractButton>
#include <QDialog>
#include <QAbstractButton>
#include <QDialog>
#include <QAbstractButton>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStandardPaths>
#include <QDateTime>
#include <QShortcut>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QToolButton>
#include <QFrame>
#include <QStyle>
#include <QMenu>
#include <QInputDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QClipboard>
#include <QPair>
#include <QPair>
#include "iconlib.h"
#include "labelstore.h"
#include "settings_dialog.h"
#include "dbmaintenance.h"
#include "settings.h"
#include "settings.h"
#include <QMenu>
#include <QActionGroup>
#include <QInputDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QClipboard>
#include <QPair>
#include "iconlib.h"
#include "labelstore.h"
#include "settings_dialog.h"

#include "mainwindow_internal.h"

// 路径显示/内部规范形:
//   内部一律用 '/' 且不带尾斜杠(历史栈 / lastDir / Browser/lastFile 比较都用它)
//   地址栏按 Windows 习惯显示:反斜杠 + 末尾 "\"(XnView 同款)
static QString canonicalPath(const QString& raw) {
    QString p = QDir::fromNativeSeparators(raw.trimmed());
    while (p.size() > 3 && p.endsWith('/')) p.chop(1);   // "E:/" 根保留斜杠
    if (p.size() == 2 && p.endsWith(':')) p += '/';
    return p;
}

static QString displayPath(const QString& canonical) {
    QString d = QDir::toNativeSeparators(canonical);
    if (!d.endsWith('\\')) d += '\\';
    return d;
}

// 路径显示/内部规范形:
//   内部一律用 '/' 且不带尾斜杠(历史栈 / lastDir / Browser/lastFile 比较都用它)
//   地址栏按 Windows 习惯显示:反斜杠 + 末尾 "\"(XnView 同款)
static QString canonicalPath(const QString& raw) {
    QString p = QDir::fromNativeSeparators(raw.trimmed());
    while (p.size() > 3 && p.endsWith('/')) p.chop(1);   // "E:/" 根保留斜杠
    if (p.size() == 2 && p.endsWith(':')) p += '/';
    return p;
}

static QString displayPath(const QString& canonical) {
    QString d = QDir::toNativeSeparators(canonical);
    if (!d.endsWith('\\')) d += '\\';
    return d;
}

// 路径显示/内部规范形:
//   内部一律用 '/' 且不带尾斜杠(历史栈 / lastDir / Browser/lastFile 比较都用它)
//   地址栏按 Windows 习惯显示:反斜杠 + 末尾 "\"(XnView 同款)
static QString canonicalPath(const QString& raw) {
    QString p = QDir::fromNativeSeparators(raw.trimmed());
    while (p.size() > 3 && p.endsWith('/')) p.chop(1);   // "E:/" 根保留斜杠
    if (p.size() == 2 && p.endsWith(':')) p += '/';
    return p;
}

static QString displayPath(const QString& canonical) {
    QString d = QDir::toNativeSeparators(canonical);
    if (!d.endsWith('\\')) d += '\\';
    return d;
}

// 路径显示/内部规范形:
//   内部一律用 '/' 且不带尾斜杠(历史栈 / lastDir / Browser/lastFile 比较都用它)
//   地址栏按 Windows 习惯显示:反斜杠 + 末尾 "\"(XnView 同款)
static QString canonicalPath(const QString& raw) {
    QString p = QDir::fromNativeSeparators(raw.trimmed());
    while (p.size() > 3 && p.endsWith('/')) p.chop(1);   // "E:/" 根保留斜杠
    if (p.size() == 2 && p.endsWith(':')) p += '/';
    return p;
}

static QString displayPath(const QString& canonical) {
    QString d = QDir::toNativeSeparators(canonical);
    if (!d.endsWith('\\')) d += '\\';
    return d;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Gaze");
    setMinimumSize(1000, 650);
    resize(1500, 900);

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *ml = new QVBoxLayout(central);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);

    // 面板显隐:默认全部可见(与旧行为一致);启动布局块/视图菜单再按需覆盖
    // 必须在 createMenubar 之前初始化 —— "视图"菜单建立时要读 paneOn() 打勾
    m_panesOn = paneIds();
    // #80:信息面板默认不打开。首次启动(ini 里还没有 Layout/last/panes)才剔除,
    // 之后一律以用户上次的选择为准 —— 开过一次就会记着
    if (AppSettings::instance().get("Interface/infoPanelSeen", false).toBool() == false
        && !mw_impl::appSettings().contains("Layout/last/panes"))
        m_panesOn.removeAll(QStringLiteral("info"));

    createMenubar();
    Logger::boot("ctor:menubar");
    Logger::boot("ctor:menubar");

    // 查看器标签条(Edge 式,只在查看器模式显示):标签的文件路径存在 tabData 里,
    // 增删/拖拽重排都带着它走,所以没有并行的路径数组需要同步(语义见文件末尾段注释)
    m_viewerTabs = new QTabBar;
    m_viewerTabs->setExpanding(false);
    m_viewerTabs->setMovable(true);
    m_viewerTabs->setElideMode(Qt::ElideRight);
    m_viewerTabs->setDrawBase(false);
    m_viewerTabs->setFocusPolicy(Qt::NoFocus);   // 焦点留预览区:方向键切图不能被标签条吃掉
    m_viewerTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    m_viewerTabs->hide();
    m_viewerTabs->installEventFilter(this);      // 中键关标签:QTabBar 没有这个信号
    connect(m_viewerTabs, &QTabBar::currentChanged, this, [this](int i) {
        // #105:「浏览器」标签 = 回标准模式的出口(用户点它就是想退出查看器)
        if (i >= 0 && isBrowserTab(i)) { toggleViewer(); return; }
        const QString p = tabPath(i);
        if (p.isEmpty()) return;
        // PROBE97 临时诊断,查完删
        Logger::event(QStringLiteral("PROBE97 tabChanged i=%1 path='%2'").arg(i).arg(p));
        // Interface/syncBrowser:切标签时把浏览器选中项挪过去(它会一路 loadFile)
        if (AppSettings::instance().get("Interface/syncBrowser", false).toBool())
            m_fileGrid->selectByPath(p);
        // 屏上已经是它就别再解:启动走"选中→loadFile→建标签→currentChanged→loadFile",
        // 同一张图白解两遍(日志里相隔约 140ms 的两条 loadFile);切回一张
        // 恰好还在屏上的标签同理。路径比对用的是预览真正显示的那张,不会漏掉
        // 同步失败/浏览器没这张行的情况。
        if (m_preview->filePath() != p) m_preview->loadFile(p);
        m_currentFile = p;
        applyTitle();
    });
    // #105:标签名左侧的小缩略图。Thumbnailer 全管线(内存/DB 命中即回,worker
    // 线程出 QImage);这里按路径回查标签索引,只给还开着的标签落图标。
    connect(&Thumbnailer::instance(), &Thumbnailer::thumbnailReady, this,
            [this](const QString& path, const QImage& img) {
        if (!m_viewerTabs || img.isNull()) return;
        const int i = indexOfTabPath(path);
        if (i >= 0) m_viewerTabs->setTabIcon(i, QIcon(QPixmap::fromImage(img)));
    });
    // #105:「浏览器」标签钉死在索引 0 —— 拖拽重排只允许发生在图片标签之间
    connect(m_viewerTabs, &QTabBar::tabMoved, this, [this](int, int) {
        const int b = indexOfTabPath(mw_impl::kBrowserTabData);
        if (b > 0) {
            m_viewerTabs->blockSignals(true);
            m_viewerTabs->moveTab(b, 0);
            m_viewerTabs->blockSignals(false);
        }
    });
    connect(m_viewerTabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int i = m_viewerTabs->tabAt(pos);
        if (i < 0) return;
        QMenu menu(m_viewerTabs);
        menu.addAction(QString::fromUtf8("关闭此标签卡"), this, [this, i]() { closeViewerTab(i); });
        menu.addAction(QString::fromUtf8("关闭所有标签卡"), this, [this]() {
            // QTabBar 没有 clear():一张一张摘。摘的过程中不发 currentChanged,
            // 否则每摘一张预览区就重解码下一张,白白解到底
            m_viewerTabs->blockSignals(true);
            while (m_viewerTabs->count() > 0) m_viewerTabs->removeTab(0);
            m_viewerTabs->blockSignals(false);
            if (m_viewerMode) toggleViewer();
        });
        menu.exec(m_viewerTabs->mapToGlobal(pos));
    });
    ml->addWidget(m_viewerTabs);
    Logger::boot("ctor:tabbar");
    Logger::boot("ctor:tabbar");

    // 查看器标签条(Interface/multiViewerTabs / oneViewerTab,语义见 syncViewerTab 段注释):
    // 只在查看器模式显示,浏览器模式隐藏
    m_viewerTabs = new QTabBar;
    m_viewerTabs->setDocumentMode(true);
    m_viewerTabs->setExpanding(false);
    m_viewerTabs->setTabsClosable(true);
    m_viewerTabs->setMovable(true);
    m_viewerTabs->hide();
    connect(m_viewerTabs, &QTabBar::currentChanged, this, [this](int i) {
        if (i < 0 || i >= m_tabPaths.size()) return;
        const QString p = m_tabPaths[i];
        m_preview->loadFile(p);
        // Interface/syncBrowser:切标签/关视图时把浏览器选中项同步过去
        if (AppSettings::instance().get("Interface/syncBrowser", false).toBool())
            m_fileGrid->selectByPath(p);
        m_currentFile = p;
        applyTitle();
    });
    connect(m_viewerTabs, &QTabBar::tabCloseRequested, this, [this](int i) {
        if (i < 0 || i >= m_tabPaths.size()) return;
        m_tabPaths.removeAt(i);
        m_viewerTabs->removeTab(i);
        if (m_tabPaths.isEmpty()) { toggleViewer(); return; }   // 关掉最后一个退回浏览器
        // removeTab 已把当前索引改指到相邻/平移后的存活标签(关非当前标签时焦点
        // 本就不该动)。旧写法无条件 setCurrentIndex(qMin(i,...)) 会把焦点从用户
        // 正在看的那张硬拽到被关位置 — 多标签此前不可达,这条错路一直没人走过。
        const int cur = m_viewerTabs->currentIndex();
        if (cur < 0 || cur >= m_tabPaths.size())
            m_viewerTabs->setCurrentIndex(qMin(qMax(i, 0), m_tabPaths.size() - 1));
    });
    ml->addWidget(m_viewerTabs);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setStyleSheet("QSplitter::handle{background:" C_SEPARATOR ";width:1px;}");
    ml->addWidget(m_splitter, 1);

    // 树面板:"文件夹"标题条 + FolderTree(标题条右侧 X 关闭)
    auto* treePane = new QWidget;
    m_treePane = treePane;
    treePane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    auto* tv = new QVBoxLayout(treePane);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(0);
    tv->addWidget(createPaneHeader(QString::fromUtf8("文件夹"), "tree"));

    m_folderTree = new FolderTree;
    m_folderTree->setMinimumWidth(160);
    connect(m_folderTree, &FolderTree::folderSelected, this, &MainWindow::navigateTo);
    tv->addWidget(m_folderTree, 1);
    m_splitter->addWidget(treePane);
    Logger::boot("ctor:tree");
    Logger::boot("ctor:tree");

    auto *centerPanel = new QWidget;
    centerPanel->setMinimumWidth(200);
    m_centerPane = centerPanel;
    auto *cl = new QVBoxLayout(centerPanel);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    createToolbar2(cl);
    Logger::boot("ctor:toolbar");
    Logger::boot("ctor:toolbar");

    m_sortHeader = new SortHeader;
    cl->addWidget(m_sortHeader);

    m_fileGrid = new FileGrid;
    cl->addWidget(m_fileGrid, 1);
    // 拖放(#81):只在 MainWindow 上 setAcceptDrops,网格/树都不开 ——
    // 子控件若 acceptDrops 却不实现 dropEvent,会把事件吞掉,主窗口反而收不到。
    // 事件沿父链上浮到这里,落点判定在 dropEvent 里用 childAt 做
    setAcceptDrops(true);
    connect(m_sortHeader, &SortHeader::sortChanged, m_fileGrid, &FileGrid::sort);
    connect(m_fileGrid, &FileGrid::fileCountChanged, this, &MainWindow::updateStatus);
    connect(m_fileGrid, &FileGrid::selectionChanged, this, &MainWindow::onSelectionChanged);
    // 反向同步:任何入口(筛选菜单/红标循环/下拉框/键盘)改了 filterMode,下拉框跟着走
    connect(m_fileGrid, &FileGrid::filterModeChanged, this, [this](int mode) {
        if (!m_formatFilterCombo) return;
        m_formatFilterCombo->blockSignals(true);
        int idx = 0;
        for (int i = 0; i < m_formatFilterCombo->count(); ++i)
            if (m_formatFilterCombo->itemData(i).toInt() == mode) { idx = i; break; }
        m_formatFilterCombo->setCurrentIndex(idx);
        m_formatFilterCombo->blockSignals(false);
    });
    // 反向同步:任何入口(筛选菜单/红标循环/下拉框/键盘)改了 filterMode,下拉框跟着走
    connect(m_fileGrid, &FileGrid::filterModeChanged, this, [this](int mode) {
        if (!m_formatFilterCombo) return;
        m_formatFilterCombo->blockSignals(true);
        int idx = 0;
        for (int i = 0; i < m_formatFilterCombo->count(); ++i)
            if (m_formatFilterCombo->itemData(i).toInt() == mode) { idx = i; break; }
        m_formatFilterCombo->setCurrentIndex(idx);
        m_formatFilterCombo->blockSignals(false);
    });
    // 反向同步:任何入口(筛选菜单/红标循环/键盘)改了 filterMode,下拉框跟着走。
    // 这个框只列 8 种"格式",而筛选菜单/红标三态键/M 键还会给出 图像(+目录)、
    // 已★标记、红色… 框里没有对应项 —— 旧代码查不到就回落到 idx 0,于是网格只列
    // 红标、框里却写着"全部"。查不到时如实标出当前筛选名:实测(Qt 6.5.3,
    // cache/tmp/combo_placeholder_test.cpp 事实B)不可编辑 QComboBox 在
    // currentIndex(-1) 下会把 placeholderText 画进显示区。
    connect(m_fileGrid, &FileGrid::filterModeChanged, this, [this](int mode) {
        if (!m_formatFilterCombo) return;
        QComboBox* cb = m_formatFilterCombo;
        cb->blockSignals(true);
        int idx = -1;
        for (int i = 0; i < cb->count(); ++i)
            if (cb->itemData(i).toInt() == mode) { idx = i; break; }
        if (idx >= 0) {
            // 先定位再清占位:index 还是 -1 时清空占位文本会把 index 顶回 0
            cb->setCurrentIndex(idx);
            cb->setPlaceholderText(QString());
        } else {
            cb->setCurrentIndex(-1);
            cb->setPlaceholderText(QString::fromUtf8("筛选：") + filterModeName(mode));
        }
        cb->blockSignals(false);
    });

    // 树右键的文件系统操作要落到网格上:removed 表示"这个目录已经没了",
    // 只有这种情况才把用户请出去,其余一律原地刷新。
    connect(m_folderTree, &FolderTree::foldersChanged, this,
            [this](const QStringList& dirs, const QStringList& removed) {
        const bool rec = m_fileGrid->showSubFolders();
        for (const QString& r : removed) {
            const QString rp = mw_impl::canonicalPath(r);
            if (m_currentDir == rp || m_currentDir.startsWith(rp + QLatin1Char('/'))) {
                navigateTo(QFileInfo(rp).dir().absolutePath());
                return;
            }
            if (rec && rp == m_currentDir) { m_fileGrid->refreshCurrentDir(); return; }
        }
        for (const QString& d : dirs) {
            const QString dp = mw_impl::canonicalPath(d);
            // 递归列表时,当前目录下任意一层变了都要重扫
            if (dp == m_currentDir || (rec && dp.startsWith(m_currentDir + QLatin1Char('/')))) {
                m_fileGrid->refreshCurrentDir();
                break;
            }
        }
    });
    connect(m_folderTree, &FolderTree::folderRenamed, this, [this](const QString& o, const QString& n) {
        const QString op = mw_impl::canonicalPath(o);
        if (m_currentDir == op) { navigateTo(n); return; }
        if (m_currentDir.startsWith(op + QLatin1Char('/')))
            navigateTo(n + m_currentDir.mid(op.size()));
    });
    connect(m_folderTree, &FolderTree::subFoldersToggled, m_fileGrid, &FileGrid::setShowSubFolders);
    m_folderTree->setSubFoldersShown(m_fileGrid->showSubFolders());

    m_splitter->addWidget(centerPanel);
    Logger::boot("ctor:grid");
    Logger::boot("ctor:grid");

    // 预览面板:"预览"标题条 + PreviewPanel(包装后才能挂标题条,X 键关闭)
    m_preview = new PreviewPanel;
    auto* previewPane = new QWidget;
    m_previewPane = previewPane;
    previewPane->setMinimumWidth(200);
    previewPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));
    auto* pv = new QVBoxLayout(previewPane);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);
    m_previewHdr = createPaneHeader(QString::fromUtf8("预览"), "preview");
    pv->addWidget(m_previewHdr);
    pv->addWidget(m_preview, 1);
    connect(m_preview, &PreviewPanel::navFile, m_fileGrid, &FileGrid::navigateSelection);

    // ── #80 信息面板(元数据 + 直方图)──
    // 刻意塞进"预览"这一栏的内部,而不是新增第 4 个分栏:分栏存档是 3 段宽度,
    // 加一段会让用户保存过的布局全部失效(splitterArchiveUsable 要求 size()==3)。
    // 放在预览栏下半部分,默认隐藏,视图菜单勾选才出来。
    m_infoPane = new QWidget;
    m_infoPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));
    auto* iv = new QVBoxLayout(m_infoPane);
    iv->setContentsMargins(0, 0, 0, 0);
    iv->setSpacing(0);
    iv->addWidget(createPaneHeader(QString::fromUtf8("信息"), "info"));
    m_info = new InfoPanel;
    m_info->setMinimumHeight(140);
    iv->addWidget(m_info, 1);
    m_infoPane->setMinimumHeight(140);
    pv->addWidget(m_infoPane);
    connect(m_info, &QWidget::destroyed, this, [this]() { m_info = nullptr; });

    m_splitter->addWidget(previewPane);
    // 伸展策略:树/预览固定,网格吃掉窗口增量大头(否则最大化后列数铺不满)
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes(mw_impl::defaultSplitterSizes());
    Logger::boot("ctor:preview");

    createStatusbar();
    applyPaneVisibility();
    applyPaneVisibility();
    applyPaneVisibility();
    applyPaneVisibility();

    // 最近文件合批写盘:连续切换只刷内存,静默 500ms 后一次落盘
    m_recentFlushTimer.setSingleShot(true);
    m_recentFlushTimer.setInterval(500);
    connect(&m_recentFlushTimer, &QTimer::timeout, this, &MainWindow::flushRecentFiles);

    // 快速幻灯片(Keyboard/space=快速幻灯片):间隔可在设置→快捷键→空格调整
    m_slideTimer.setInterval(slideIntervalMs());
    connect(&m_slideTimer, &QTimer::timeout, this, [this]() {
        m_fileGrid->navigateSelection(1);
    });

    // 快速幻灯片(Keyboard/space=快速幻灯片):间隔可在设置→快捷键→空格调整
    m_slideTimer.setInterval(slideIntervalMs());
    connect(&m_slideTimer, &QTimer::timeout, this, [this]() {
        m_fileGrid->navigateSelection(1);
    });

    // 快速幻灯片(Keyboard/space=快速幻灯片):间隔可在设置→快捷键→空格调整
    m_slideTimer.setInterval(mw_impl::slideIntervalMs());
    connect(&m_slideTimer, &QTimer::timeout, this, [this]() {
        m_fileGrid->navigateSelection(1);
    });

    // Tooltip
    m_tooltip = new QLabel(nullptr, Qt::ToolTip);
    m_tooltip->setStyleSheet(
        "background:rgba(0,0,0,240);color:#FFFFFF;font-size:11px;"
        "padding:8px 12px;border-radius:6px;border:1px solid #444;");
    m_tooltip->hide();

    // Tooltip
    m_tooltip = new QLabel(nullptr, Qt::ToolTip);
    m_tooltip->setStyleSheet(
        "background:rgba(0,0,0,240);color:#FFFFFF;font-size:11px;"
        "padding:8px 12px;border-radius:6px;border:1px solid #444;");
    m_tooltip->hide();

    // Global shortcuts via event filter
    qApp->installEventFilter(this);

    m_folderTree->loadDrives();
    Logger::boot("ctor:drives");
    Logger::boot("ctor:drives");

    // 启动目录/文件:此前 Start/withFile、Start/withoutFile、Start/rememberFilename
    // 三键只被设置页写入、无人读取(永远打开桌面)。argv 优先,其次按设置恢复。
    {
        AppSettings& st = AppSettings::instance();
        const QString fallbackDir =
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);

        const QStringList args = QCoreApplication::arguments();
        QString cliPath;
        for (int i = 1; i < args.size(); ++i) {
            const QString a = args[i].trimmed();
            if (a.isEmpty() || a.startsWith(QLatin1Char('-'))) continue;
            if (QFileInfo::exists(QDir::fromNativeSeparators(a))) { cliPath = a; break; }
        }

        if (!cliPath.isEmpty()) {
            QFileInfo fi(QDir::fromNativeSeparators(cliPath));
            navigateTo(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
            // "带文件启动"只管"带文件"这一种情况:传目录进来就是来浏览的,
            // 走查看器模式会把整个文件列表面板藏起来(网格不可见、无从滚动)。
            if (!fi.isDir()) {
                const int mode = st.get("Start/withFile", 0).toInt();
                m_fileGrid->selectByPath(fi.absoluteFilePath());
                if (mode == 0 || mode == 1) toggleViewer();
                if (mode == 1 || mode == 3) enterFullscreen();
            }
        } else {
            QString dir;
            switch (st.get("Start/withoutFile", 1).toInt()) {
            case 1: dir = st.get("Browser/lastDir", QString()).toString(); break;
            case 2: dir = st.get("Browser/startDir", QString()).toString(); break;
            default: break;   // 0 无:不恢复目录(下面退回桌面兜底)
            }
            if (dir.isEmpty() || !QFileInfo(dir).isDir()) dir = fallbackDir;
            navigateTo(dir);
            if (st.get("Start/rememberFilename", true).toBool()) {
                const QString last = st.get("Browser/lastFile", QString()).toString();
                if (!last.isEmpty()) m_fileGrid->selectByPath(last);
            }
        }
    }

    Logger::boot("ctor:startdir");

    Logger::boot("ctor:startdir");

    applyShortcuts();   // 应用用户自定义快捷键(ini 覆盖默认)
    Logger::boot("ctor:shortcuts");
    Logger::boot("ctor:shortcuts");

    // 启动布局:"跟随上次窗口状态"开 → 用上次关闭状态;否则用最后应用的命名布局
    // (active 指向的布局已被删除/不在名称列表时退回"跟随上次",不再引用孤儿布局)
    {
        QSettings s = mw_impl::appSettings();
        bool followLast = s.value("Layout/followLast", true).toBool();
        QString active = s.value("Layout/active").toString();
        QStringList names = s.value("Layout/names").toStringList();
        if (!followLast && !active.isEmpty() && names.contains(active)
            && s.contains("Layout/" + active + "/geometry"))
            applyLayout(active);
        else
            applyLastLayout();
    }
    Logger::boot("ctor:layout");

    // 设置→界面→启动时打开文件列表和预览框:
    //   勾选 = 无视布局状态,强制显示树与预览(未保存过布局时的默认行为)
    //   未勾选 = 沿用上面恢复出的面板状态(关闭前被关掉的面板下次启动仍然没有)
    if (AppSettings::instance().get("Interface/showPanesOnStart", true).toBool()) {
        setPaneVisible("tree", true);
        setPaneVisible("preview", true);
    }

    // 设置→界面→启动时打开文件列表和预览框:
    //   勾选 = 无视布局状态,强制显示树与预览(未保存过布局时的默认行为)
    //   未勾选 = 沿用上面恢复出的面板状态(关闭前被关掉的面板下次启动仍然没有)
    if (AppSettings::instance().get("Interface/showPanesOnStart", true).toBool()) {
        setPaneVisible("tree", true);
        setPaneVisible("preview", true);
    }

    // 设置→界面→启动时打开文件列表和预览框:
    //   勾选 = 无视布局状态,强制显示树与预览(未保存过布局时的默认行为)
    //   未勾选 = 沿用上面恢复出的面板状态(关闭前被关掉的面板下次启动仍然没有)
    if (AppSettings::instance().get("Interface/showPanesOnStart", true).toBool()) {
        setPaneVisible("tree", true);
        setPaneVisible("preview", true);
    }

    // 设置→界面→启动时打开文件列表和预览框:
    //   勾选 = 无视布局状态,强制显示树与预览(未保存过布局时的默认行为)
    //   未勾选 = 沿用上面恢复出的面板状态(关闭前被关掉的面板下次启动仍然没有)
    if (AppSettings::instance().get("Interface/showPanesOnStart", true).toBool()) {
        setPaneVisible("tree", true);
        setPaneVisible("preview", true);
    }

    // 命令行启动路径已由上面的启动块统一处理(argv 优先于 Start/*)

    // 设置改动 → 标题模板/幻灯片间隔即时生效(此前 changed() 无人订阅,
    // 所有设置都要重启才起作用)
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        applyTitle();
        m_slideTimer.setInterval(mw_impl::slideIntervalMs());
    });
    Logger::boot("ctor:done");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (AppSettings::instance().get("Interface/clearRecentOnExit", false).toBool()) {
        // 退出时清理"最近的文件":不落盘,直接把内存 + ini 一起清空
        m_recentFlushTimer.stop();
        m_recentList.clear();
        m_recentLoaded = true;
        mw_impl::appSettings().remove("recent/files");
    } else {
        flushRecentFiles();   // 防抖窗口内未落盘的最近文件列表在此统一写入
    }
    // General/saveSession:退出时是否保留"会话"(当前目录 + 选中文件),
    // 只影响这两个键 —— 窗口几何/分栏属于"布局",由 Layout/* 独立控制
    //   0 从不(不写回,下次启动不恢复位置)  1 询问  2 始终(默认)
    int saveMode = AppSettings::instance().get("General/saveSession", 2).toInt();
    if (saveMode == 1) {
        saveMode = QMessageBox::question(this, QString::fromUtf8("退出 Gaze"),
            QString::fromUtf8("保存当前会话?\n\n保存后下次启动会回到:\n%1")
                .arg(m_currentDir),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes ? 2 : 0;
    }
    if (saveMode != 2) {
        AppSettings::instance().set("Browser/lastDir", QString());
        AppSettings::instance().set("Browser/lastFile", QString());
    }

    // 总是记录"上次窗口状态"(跟随模式下下次启动用)
    QSettings s = mw_impl::appSettings();
    s.setValue("Layout/last/geometry", saveGeometry().toHex());
    s.setValue("Layout/last/splitter", splitterCsv());
    // 面板意图(m_panesOn)而非控件实时可见性:查看器模式临时藏了树/网格,
    // 用实时可见性落盘会把"只是进了查看器"误存成"用户关掉了面板"
    s.setValue("Layout/last/panes", m_panesOn.join(','));
    QMainWindow::closeEvent(event);
}
