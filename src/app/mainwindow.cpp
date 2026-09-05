#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "previewpanel.h"
#include "imgsearchdialog.h"
#include "imgsearch.h"     // ImgSearch::servicePid/killStartedService:退出时按设置回收自启服务
#include "printdialog.h"
#include "infopanel.h"
#include "favoritespanel.h"
#include "filterpanel.h"
#include "shelldelete.h"   // showDeleteToast:拖放复制成功的左下角提示
#include "sortheader.h"
#include "fileentry.h"
#include "livephoto.h"
#include "constants.h"
#include "theme.h"        // addChangeHandler/notifyChanged:主题即时切换广播
#include "thumbnailer.h"  // #105:查看器标签名左侧的小缩略图走同一缩略图管线
#include "validname.h"
#include "keytarget.h"
#include "logger.h"
#include "i18n.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QComboBox>
#include <QMessageBox>
#include <QDialog>
#include <QAbstractButton>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QShortcut>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QToolButton>
#include <QFrame>
#include <QStyle>
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
#include "dbmaintenance.h"
#include "settings.h"

#include "mainwindow_internal.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Gaze");
    setMinimumSize(1000, 650);
    resize(1500, 900);

    auto *central = new QWidget;
    // saveState/restoreState 用 objectName 识别中央部件:没有名字 restoreState
    // 会校验失败(dock 位置存档全废),起个稳定名
    central->setObjectName("gazeCentral");
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
    // #243:收藏夹面板同款处理 —— 新装用户不白给一块空面板,想用再开
    if (AppSettings::instance().get("Interface/favPanelSeen", false).toBool() == false
        && !mw_impl::appSettings().contains("Layout/last/panes"))
        m_panesOn.removeAll(QStringLiteral("favorites"));
    // #242:分类筛选器面板同款处理
    if (AppSettings::instance().get("Interface/filterPanelSeen", false).toBool() == false
        && !mw_impl::appSettings().contains("Layout/last/panes"))
        m_panesOn.removeAll(QStringLiteral("filter"));

    createMenubar();
    Logger::boot("ctor:menubar");

    // 查看器标签条(Edge 式):2026-09-03 起显隐由 updateTabBarVis 总闸统一管 ——
    // 查看器模式恒显示;浏览器模式只要还有图片标签就继续显示,一张不剩才收。
    // 标签的文件路径存在 tabData 里,增删/拖拽重排都带着它走,所以没有并行的
    // 路径数组需要同步(语义见文件末尾段注释)
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
        // #105:「浏览器」标签 = 回标准模式的出口(用户点它就是想退出查看器)。
        // 2026-09-03:浏览器态标签栏也显示后,这个标签在浏览器里也会被点到 ——
        // 已在浏览器时再 toggleViewer 会反向把人带进查看器,必须挡掉。
        if (i >= 0 && isBrowserTab(i)) {
            if (m_viewerMode) toggleViewer();
            return;
        }
        const QString p = tabPath(i);
        if (p.isEmpty()) return;
        // #219(2026-09-04 用户报):浏览器态点文件标签没反应 —— 旧代码只把图解进
        // 预览,人还留在浏览器(文件标签之间能互跳,是因为那已经身在查看器)。
        // 现在浏览器态点文件标签 = 直接跳进查看器看它。m_viewerNoSync:目的地就是
        // 这张已选中的标签自己,进查看器不许再激活/追加/覆写任何标签。
        if (!m_viewerMode) {
            m_currentFile = p;
            m_viewerNoSync = true;
            toggleViewer();
            m_viewerNoSync = false;
            if (!m_viewerMode) return;
        }
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
        if (img.isNull()) return;
        // 标签缩略图(#203 起:全屏胶片条的回填由 FilmStrip 自己连 Thumbnailer,
        // 这里不再代收)
        if (m_viewerTabs) {
            const int i = indexOfTabPath(path);
            if (i >= 0) m_viewerTabs->setTabIcon(i, QIcon(QPixmap::fromImage(img)));
        }
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
        if (i < 0 || isBrowserTab(i)) return;   // #105:浏览器标签无可关闭
        QMenu menu(m_viewerTabs);
        menu.addAction(gazeTr("关闭此标签卡"), this, [this, i]() { closeViewerTab(i); });
        menu.addAction(gazeTr("关闭所有标签卡"), this, [this]() {
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

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setStyleSheet(QString::fromUtf8(
        "QSplitter::handle{background:%1;width:1px;}").arg(C_SEPARATOR));
    ml->addWidget(m_splitter, 1);

    // 树面板:"文件夹"标题条 + FolderTree(标题条右侧 X 关闭)
    auto* treePane = new QWidget;
    m_treePane = treePane;
    auto* tv = new QVBoxLayout(treePane);
    treePane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(0);
    tv->addWidget(createPaneHeader(gazeTr("文件夹"), "tree"));

    m_folderTree = new FolderTree;
    m_folderTree->setMinimumWidth(160);
    // 树点击导航经包装函数:标记来源,让 navigateTo 不把焦点从树抢回网格
    connect(m_folderTree, &FolderTree::folderSelected, this, &MainWindow::onTreeFolderSelected);
    tv->addWidget(m_folderTree, 1);

    // ── #243 收藏夹面板(已 Dock 化):数据真源 = m_favPaths(Favorites/paths),
    //   面板只管显示与交互。容器保留(header 搬去当 dock 标题条,容器只装面板),
    //   停靠位置/大小/浮动形态归 saveState,显隐仍归 applyPaneVisibility ──
    m_favPane = new QWidget;
    m_favPane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    auto* fvl = new QVBoxLayout(m_favPane);
    fvl->setContentsMargins(0, 0, 0, 0);
    fvl->setSpacing(0);
    m_favs = new FavoritesPanel;
    fvl->addWidget(m_favs, 1);
    m_favPane->setMinimumHeight(120);
    m_favDock = new QDockWidget(gazeTr("收藏夹"), this);
    m_favDock->setObjectName("favorites");   // saveState 按名字匹配
    // 不给 Closable:关闭只走自绘标题条的 X(setPaneVisible 单一出口,意图不脱节);
    // 浮动成独立窗口后系统标题栏仍有关闭 X,由 visibilityChanged 守卫回写意图
    m_favDock->setFeatures(QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    m_favDock->setTitleBarWidget(createPaneHeader(gazeTr("收藏夹"), "favorites"));
    m_favDock->setWidget(m_favPane);
    addDockWidget(Qt::LeftDockWidgetArea, m_favDock);
    m_favPaths = AppSettings::instance().get("Favorites/paths", QStringList()).toStringList();
    m_favs->setPaths(m_favPaths);
    connect(m_favs, &FavoritesPanel::openRequested, this, [this](const QString& p) {
        if (QFileInfo(p).isDir()) { navigateTo(p); return; }
        if (navigateTo(QFileInfo(p).absolutePath())) m_fileGrid->selectByPath(p);
    });
    connect(m_favs, &FavoritesPanel::locateRequested, this, [this](const QString& p) {
        if (QFileInfo(p).isDir()) { navigateTo(p); return; }
        if (navigateTo(QFileInfo(p).absolutePath())) m_fileGrid->selectByPath(p);
    });
    connect(m_favs, &FavoritesPanel::removeRequested, this, [this](const QString& p) {
        m_favPaths.removeAll(p);
        saveFavorites();
        m_favs->setPaths(m_favPaths);
    });
    connect(m_favs, &FavoritesPanel::clearRequested, this, [this]() {
        if (m_favPaths.isEmpty()) return;
        // 破坏性确认:默认按钮落 No(空格/回车手滑不清库)
        if (QMessageBox::question(this, gazeTr("清空收藏夹"),
                gazeTr("确定要移除全部收藏吗?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        m_favPaths.clear();
        saveFavorites();
        m_favs->setPaths(m_favPaths);
    });

    // ── #242 分类筛选器面板(已 Dock 化):条件真源 = 面板勾选;显隐推拉在
    //   applyPaneVisibility 的 filter 分支。与收藏夹同区左停靠,竖排在下 ──
    m_filterPane = new QWidget;
    m_filterPane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    auto* kvl = new QVBoxLayout(m_filterPane);
    kvl->setContentsMargins(0, 0, 0, 0);
    kvl->setSpacing(0);
    m_filterPnl = new FilterPanel;
    kvl->addWidget(m_filterPnl);
    m_filterPane->setMinimumHeight(120);
    m_filterDock = new QDockWidget(gazeTr("分类筛选器"), this);
    m_filterDock->setObjectName("filter");
    m_filterDock->setFeatures(QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    m_filterDock->setTitleBarWidget(createPaneHeader(gazeTr("分类筛选器"), "filter"));
    m_filterDock->setWidget(m_filterPane);
    addDockWidget(Qt::LeftDockWidgetArea, m_filterDock);
    // 勾选变化 → 网格:范围不同先切范围(重扫),再压条件(重筛)。
    // 面板组装先于 m_fileGrid 创建,lambda 里经 this 取,发射时都已就位
    connect(m_filterPnl, &FilterPanel::conditionsChanged, this, [this](int scope) {
        if (!m_fileGrid) return;
        if (m_fileGrid->multiFilterScope() != scope)
            m_fileGrid->setMultiFilterScope(scope);
        m_fileGrid->setMultiFilter(m_filterPnl->spec());
    });
    // #236/#247 实测定案:挂树这笔账不可省——推迟到首帧后挂,firstPaint 无净
    // 收益(快档 749ms vs 基线 751ms),因为基线记在挂树名下的 337~635ms 并不是
    // 挂树本身(QSplitter::addWidget 推迟后实测仅 7~11ms),而是**恰好在此触发的
    // 一次性全局初始化**:QLineEdit 首次创建的 Windows 输入法(TSF)初始化
    // (实测 326~758ms)+ QSS 引擎热身等。推迟挂载只是让这笔账改记到地址栏
    // QLineEdit 头上,总账不变;且树在挂载前无父,applyPaneVisibility 的
    // setVisible(true) 会把它当独立顶层窗口 show(原生窗口创建 196~272ms,
    // 即 #207 记录过的无父闪窗病)。见 todo.md §8.ak 全套分账数据。
    m_splitter->addWidget(treePane);
    Logger::boot("ctor:tree");

    auto *centerPanel = new QWidget;
    centerPanel->setMinimumWidth(200);
    m_centerPane = centerPanel;
    auto *cl = new QVBoxLayout(centerPanel);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    createToolbar2(cl);
    Logger::boot("ctor:toolbar");

    m_sortHeader = new SortHeader;
    // 2026-09-03 夜根治启动闪窗:必须先 addWidget(收编为子控件)再 setVisible ——
    // 无父的 QWidget 被 setVisible(true) 会按独立顶层窗口 show 一帧
    //(native 396x65 黑条,title=Gaze,即用户反复报告的"启动闪过的窗口";
    // QWidget::find 实证 who=class=SortHeader)。先挂布局后设可见,帧都不闪。
    cl->addWidget(m_sortHeader);
    // #107:列标题显隐落盘(此前切掉重启又回来)
    m_sortHeader->setVisible(AppSettings::instance().get("Browser/sortHeader", true).toBool());

    m_fileGrid = new FileGrid;
    cl->addWidget(m_fileGrid, 1);
    // 注:过去这里有一条"文件页鼠标单选目录卡 → 树镜像选中"的反向联动
    // (FileGrid::dirSelected → MainWindow::onGridDirSelected),2026-09-03 用户
    // 裁决「选中文件夹时树应当留在原处,只有双击打开才同步」后已移除。
    // 树同步只发生在真正进入目录时(navigateTo 里的 FolderTree::focusPath)。
    // 拖放(#81):只在 MainWindow 上 setAcceptDrops,网格/树都不开 ——
    // 子控件若 acceptDrops 却不实现 dropEvent,会把事件吞掉,主窗口反而收不到。
    // 事件沿父链上浮到这里,落点判定在 dropEvent 里用 childAt 做
    setAcceptDrops(true);
    connect(m_sortHeader, &SortHeader::sortChanged, m_fileGrid, &FileGrid::sort);
    connect(m_fileGrid, &FileGrid::fileCountChanged, this, &MainWindow::updateStatus);
    // #242 命中计数:网格每次重筛/换目录都发 fileCountChanged,筛选面板跟着刷新
    connect(m_fileGrid, &FileGrid::fileCountChanged, this, [this]() {
        if (m_filterPnl) m_filterPnl->setHitCount(m_fileGrid->fileCount());
    });
    connect(m_fileGrid, &FileGrid::selectionChanged, this, &MainWindow::onSelectionChanged);
    // 反向同步:任何入口(筛选菜单/红标循环/键盘)改了 filterMode,下拉框跟着走。
    // 这个框只列 8 种"格式",而筛选菜单/红标三态键还会给出 图像(+目录)、
    // 红色… 框里没有对应项 —— 旧代码查不到就回落到 idx 0,于是网格只列
    // 红标、框里却写着"全部"。查不到时如实标出当前筛选名:实测(Qt 6.5.3,
    // cache/tmp/combo_placeholder_test.cpp 事实B)不可编辑 QComboBox 在
    // currentIndex(-1) 下会把 placeholderText 画进显示区。
    connect(m_fileGrid, &FileGrid::filterModeChanged,
            this, &MainWindow::syncFilterIndicators);
    // #107:启动即按落盘筛选(Browser/filterMode)对齐下拉框/红钮 —— 原先恒显"全部"。
    // 放这里而不是工具栏里:构造序是 createToolbar2 → sortHeader → FileGrid,
    // 工具栏阶段 m_fileGrid 还是空的。
    syncFilterIndicators(m_fileGrid->filterMode());

    // #266:表头与网格结对(详细列表列几何 lead/tail 由 setSortHeader 打通),
    // 查看方式变化统一走 syncViewModeUI;启动即按落盘态对齐一次
    m_fileGrid->setSortHeader(m_sortHeader);
    connect(m_fileGrid, &FileGrid::viewModeChanged,
            this, &MainWindow::syncViewModeUI);
    syncViewModeUI(m_fileGrid->viewMode());

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

    // 预览面板:"预览"标题条 + PreviewPanel(包装后才能挂标题条,X 键关闭)
    m_preview = new PreviewPanel;
    auto* previewPane = new QWidget;
    m_previewPane = previewPane;
    previewPane->setMinimumWidth(200);
    previewPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));
    auto* pv = new QVBoxLayout(previewPane);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(0);
    m_previewHdr = createPaneHeader(gazeTr("预览"), "preview");
    pv->addWidget(m_previewHdr);
    pv->addWidget(m_preview, 1);
    connect(m_preview, &PreviewPanel::navFile, m_fileGrid, &FileGrid::navigateSelection);

    // ── #80 信息面板(元数据 + 直方图,已 Dock 化)──
    // 停靠右区(原"预览栏下半"的位置感):dock 位置/大小归 saveState 存档,
    // 显隐仍归 applyPaneVisibility。不再塞进预览栏 vbox。
    m_infoPane = new QWidget;
    m_infoPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));
    auto* iv = new QVBoxLayout(m_infoPane);
    iv->setContentsMargins(0, 0, 0, 0);
    iv->setSpacing(0);
    m_info = new InfoPanel;
    m_info->setMinimumHeight(140);
    iv->addWidget(m_info, 1);
    m_infoPane->setMinimumHeight(140);
    connect(m_info, &QWidget::destroyed, this, [this]() { m_info = nullptr; });
    m_infoDock = new QDockWidget(gazeTr("信息"), this);
    m_infoDock->setObjectName("info");
    m_infoDock->setFeatures(QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
    m_infoDock->setTitleBarWidget(createPaneHeader(gazeTr("信息"), "info"));
    m_infoDock->setWidget(m_infoPane);
    addDockWidget(Qt::RightDockWidgetArea, m_infoDock);
    // 三个 dock 的默认体量:无存档时别让 Qt 平分窗口把网格压扁(有存档时
    // restoreState 覆盖这里的值)
    resizeDocks({m_favDock, m_filterDock}, {180, 180}, Qt::Horizontal);
    resizeDocks({m_favDock, m_filterDock}, {260, 220}, Qt::Vertical);
    resizeDocks({m_infoDock}, {260}, Qt::Horizontal);
    resizeDocks({m_infoDock}, {280}, Qt::Vertical);
    // 浮动成独立窗口后,系统标题栏的 X 直接 hide dock、绕过 setPaneVisible:
    // visibilityChanged 回写意图。守卫三重:与意图一致不动(防循环)、查看器/G
    // 全屏的程序性隐藏不回写(同"只是进了查看器"误存的病)、restoreState 期间
    // 不回写(存档可见性不得污染意图)
    auto dockCloseGuard = [this](QDockWidget* dock, const char* paneId) {
        const QString id = QString::fromLatin1(paneId);
        connect(dock, &QDockWidget::visibilityChanged, this, [this, id](bool vis) {
            if (vis == paneOn(id) || m_viewerMode || m_fullView || m_restoringDocks)
                return;
            setPaneVisible(id.toLatin1().constData(), vis);
        });
    };
    dockCloseGuard(m_favDock, "favorites");
    dockCloseGuard(m_filterDock, "filter");
    dockCloseGuard(m_infoDock, "info");

    m_splitter->addWidget(previewPane);
    // 伸展策略:树/预览固定,网格吃掉窗口增量大头(否则最大化后列数铺不满)
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes(mw_impl::defaultSplitterSizes());
    Logger::boot("ctor:preview");

    createStatusbar();
    applyPaneVisibility();

    // 最近文件合批写盘:连续切换只刷内存,静默 500ms 后一次落盘
    m_recentFlushTimer.setSingleShot(true);
    m_recentFlushTimer.setInterval(500);
    connect(&m_recentFlushTimer, &QTimer::timeout, this, &MainWindow::flushRecentFiles);

    // 快速幻灯片(Keyboard/space=快速幻灯片):间隔可在设置→快捷键→空格调整
    m_slideTimer.setInterval(mw_impl::slideIntervalMs());
    connect(&m_slideTimer, &QTimer::timeout, this, [this]() {
        m_fileGrid->navigateSelection(1);
    });

    // Global shortcuts via event filter
    qApp->installEventFilter(this);

    createFilmStrip();   // #203:G 全屏顶部胶片条(隐藏;光标到顶出现,交互在控件内自理)

    m_folderTree->loadDrives();
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
            if (!fi.isDir()) {
                // 先出大图再补加载:构造期就把目标文件交给预览的预读通路后台
                // 解码,目录扫描+选中推迟到 show 后补做 —— 选中落定时若解码
                // 已完成,showImage 命中预读缓存零等待直出,解码与扫描并行,
                // 大图不再排在整份目录清单后面(目录越大收益越大)。
                const QString file = fi.absoluteFilePath();
                m_preview->preloadStartup(file);
                // 布局档留在构造期:toggleViewer 对"未 show 直进查看器"有专门
                // 存档纪律(m_savedSplitter),show 后再切会先闪一帧浏览器布局。
                // 此刻 m_currentFile 还是空:查看器标签由补做选中时的
                // syncViewerTab 兜底开出(无签则 openViewerTab)
                const int mode = st.get("Start/withFile", 0).toInt();
                if (mode == 0 || mode == 1) toggleViewer();
                if (mode == 1 || mode == 3) enterFullscreen();
                // #233 新选项"全屏预览":G 全屏(只铺画面),不进查看器不碰标签
                if (mode == 4) toggleFullView();
                QTimer::singleShot(0, this, [this, dir = fi.absolutePath(), file] {
                    navigateTo(dir);
                    m_fileGrid->selectByPath(file);
                });
            } else {
                // "带文件启动"只管"带文件"这一种情况:传目录进来就是来浏览的,
                // 走查看器模式会把整个文件列表面板藏起来(网格不可见、无从滚动)。
                navigateTo(fi.absoluteFilePath());
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
            // Start/rememberFilename:上次选中文件**不在构造期恢复**——
            // 预览 loadFile 会触发 QVideoWindow(FFmpeg 后端的顶层视频输出窗)
            // 先于主窗口落位,屏幕上孤立映射一帧"启动闪框"。主窗 show 完成后
            // 由 main.cpp 调 restoreStartupPreview() 再选,见 mainwindow.h。
            if (st.get("Start/rememberFilename", true).toBool()) {
                const QString last = st.get("Browser/lastFile", QString()).toString();
                if (!last.isEmpty()) m_startupRestoreFile = last;
            }
        }
    }

    Logger::boot("ctor:startdir");

    applyShortcuts();   // 应用用户自定义快捷键(ini 覆盖默认)
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

    // 命令行启动路径已由上面的启动块统一处理(argv 优先于 Start/*)

    // 设置改动 → 标题模板/幻灯片间隔即时生效(此前 changed() 无人订阅,
    // 所有设置都要重启才起作用)
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        applyTitle();
        m_slideTimer.setInterval(mw_impl::slideIntervalMs());
    });

    // 主题切换即时生效:全局 QSS 刷新覆盖不到的内联样式/缓存色,由
    // applyThemeSurfaces() 重灌。设置页切主题 → Theme::notifyChanged() 触发。
    Theme::addChangeHandler([this]() { applyThemeSurfaces(); });
    Logger::boot("ctor:done");
}

MainWindow::~MainWindow() {
    cancelDirSizeRun();   // #241:文件夹大小统计还在后台跑的话让它立刻收手,
                          // 免得线程池收尾等它数完几十万条目才放行退出
}

// 启动收尾:主窗口首帧显示后恢复上次选中文件(Start/rememberFilename)。
// 构造期做这件事会闪框:预览 loadFile → QVideoWindow(独立顶层 HWND)在主窗
// show 之前落位,屏幕上孤立映射视频第一帧、随即消失 —— 就是用户看到的
// "启动先弹一个框再出现 Gaze"。这里由 main.cpp 在 opacity 恢复同拍调用。
void MainWindow::restoreStartupPreview() {
    if (m_startupRestoreFile.isEmpty()) return;
    const QString last = m_startupRestoreFile;
    m_startupRestoreFile.clear();   // 只恢复一次:后续 grid 信号不再走这条路径
    if (m_fileGrid->fileCount() > 0)
        m_fileGrid->selectByPath(last);
}

// 预热预览媒体栈:QMediaPlayer/QVideoWidget 首次创建同步且重(日志实测 3~4 秒),
// main.cpp 在主窗 show 后、恢复上次选中文件之前调用,把这笔开销挪出点击路径
void MainWindow::warmUpPreviewMedia() {
    m_preview->warmUp();
}

// ── 主题切换:重灌"构造期内联样式表 + 填充期缓存色"──
// 设置页切换主题协议见 theme.h。这里补齐全局 QSS 刷不到的部分:
//   · 各面板/分割条/状态栏/工具栏/地址栏的内联 setStyleSheet(构造期
//     .arg(C_*) 求值一次,之后不再变)
//   · FileGrid 画布背景、PreviewPanel 文字色、FolderTree 行前景色
//     (加载时缓存进 item,必须重灌)
void MainWindow::applyThemeSurfaces() {
    if (m_splitter)
        m_splitter->setStyleSheet(QString::fromUtf8(
            "QSplitter::handle{background:%1;width:1px;}").arg(C_SEPARATOR));
    if (m_treePane)
        m_treePane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    if (m_previewPane)
        m_previewPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));
    if (m_infoPane)
        m_infoPane->setStyleSheet(QString("background:%1;border:none;").arg(C_PREVIEW_BG));

    const QString barQss =
        QString::fromUtf8("QWidget{background:%1;border-bottom:1px solid %2;}"
        "QToolButton{background:transparent;border:none;border-radius:4px;"
        "padding:3px 6px;color:%3;font-size:11px;}"
        "QToolButton:hover{background:%4;}"
        "QToolButton::menu-indicator{image:none;}").arg(C_TOOLBAR, C_SEPARATOR, C_TEXT, C_CARD_HOVER);
    if (m_addrRow) m_addrRow->setStyleSheet(barQss);
    if (m_toolRow) m_toolRow->setStyleSheet(barQss);
    if (m_addrBar)
        m_addrBar->setStyleSheet(QString::fromUtf8(
            "QLineEdit{background:%1;color:%2;"
            "border:1px solid %3;"
            "border-radius:4px;padding:2px 8px;font-size:11px;}")
            .arg(C_CONTENT, C_TEXT, C_CARD_BORDER));

    if (QStatusBar* sb = statusBar()) {
        sb->setStyleSheet(QString::fromUtf8(
            "QStatusBar{background:%1;border-top:1px solid %2;"
            "color:%3;font-size:11px;padding:2px 10px;}"
            "QStatusBar::item{border:none;}")
            .arg(C_STATUSBAR, C_SEPARATOR, C_TEXT));
        if (m_statusLabel)
            m_statusLabel->setStyleSheet(QString("color:%1;background:transparent;").arg(C_TEXT));
        if (m_pathLabel)
            m_pathLabel->setStyleSheet(QString("color:%1;background:transparent;").arg(C_TEXT));
    }

    // 面板标题条("文件夹"/"预览"/"信息")与其标题文字
    for (QWidget* h : m_paneHdrs) {
        if (!h) continue;
        h->setStyleSheet(QString::fromUtf8(
            "QWidget{background:%1;border-bottom:1px solid %2;}"
            "QToolButton{background:transparent;border:none;border-radius:4px;"
            "color:%3;font-size:13px;}"
            "QToolButton:hover{background:%4;}")
            .arg(C_PANE_HDR, C_SEPARATOR, C_TEXT, C_CARD_HOVER));
        if (QLabel* lbl = h->findChild<QLabel*>())
            lbl->setStyleSheet(QString::fromUtf8(
                "background:transparent;color:%1;font-size:12px;").arg(C_TEXT));
    }

    // 子树缓存色(各自的重灌入口)
    if (m_fileGrid)   m_fileGrid->refreshThemeColors();
    if (m_preview)    m_preview->refreshThemeColors();
    if (m_folderTree) m_folderTree->refreshThemeColors();
    if (m_info)       m_info->applyTheme();   // #248:信息面板此前无刷新钩子
    if (m_favPane)
        m_favPane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    if (m_favs)       m_favs->applyTheme();   // #243:收藏夹列表同款重灌
    if (m_filterPane)
        m_filterPane->setStyleSheet(QString("background:%1;border:none;").arg(C_SIDEBAR));
    if (m_filterPnl)  m_filterPnl->applyTheme();   // #242:筛选面板同款重灌
}

// ── #243 收藏夹:数据真源 = m_favPaths,改动即写 Favorites/paths ──
void MainWindow::saveFavorites() {
    AppSettings::instance().set("Favorites/paths", m_favPaths);
}

void MainWindow::addFavorite(const QString& path) {
    if (path.isEmpty()) return;
    // 落库前归一到 canonical 形:网格条目在盘根是 "X://name" 连体形,树行
    // 可能是反斜杠形 —— 不归一,同一文件夹会被收成两条
    const QString cp = mw_impl::canonicalPath(path);
    if (m_favPaths.contains(cp)) {
        showDeleteToast(this, gazeTr("已在收藏夹中"));
        return;
    }
    m_favPaths.append(cp);
    saveFavorites();
    m_favs->setPaths(m_favPaths);
    // 面板若关着不代开(免得"加个收藏还自己弹面板"),左下角 toast 给反馈
    showDeleteToast(this, gazeTr("已添加到收藏夹"));
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
        saveMode = QMessageBox::question(this, gazeTr("退出 Gaze"),
            gazeTr("保存当前会话?\n\n保存后下次启动会回到:\n%1")
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
    // dock(信息/收藏夹/筛选器)的位置/大小/浮动形态:saveState 全量序列化。
    // 存档里的 dock 可见性不作数 —— 启动时 restorePanes→applyPaneVisibility
    // 按意图统一纠正,查看器模式下退出也不会把"临时藏起"存成"用户关掉"
    s.setValue("Layout/last/docks", saveState().toHex());
    // 由 Gaze 拉起的万象图搜服务:按设置决定是否随 Gaze 退出一起结束
    if (ImgSearch::servicePid() > 0
        && AppSettings::instance().get("ImgSearch/killOnExit", false).toBool())
        ImgSearch::killStartedService();
    QMainWindow::closeEvent(event);
}
