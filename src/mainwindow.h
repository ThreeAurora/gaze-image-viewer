#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QSplitter>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QStringList>
#include <QTabBar>
#include <QFileInfo>
#include <QPoint>
#include <QPointer>
#include <QThreadPool>
#include <QSet>
#include <QHash>
#include <atomic>
#include <memory>

class QVBoxLayout;
class QDialog;
class QComboBox;
class QToolButton;
class FolderTree;
class FileGrid;
class PreviewPanel;
class InfoPanel;
class FavoritesPanel;
class FilterPanel;
class SortHeader;
class FilmStrip;
class SettingsDialog;      // #218 长驻工具窗(非模态单例)
class ImageSearchDialog;
class DbMaintenanceDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;   // 停掉可能在跑的后台统计线程,免拖慢退出
    Q_INVOKABLE bool navigateTo(const QString &path);  // false=目标不存在/非目录,什么都没改
    Q_INVOKABLE void openFullscreen(const QString &path);  // 右键"全屏":导航到文件并全屏
    Q_INVOKABLE void revealFile(const QString &path);      // 以文搜图结果:定位到目录并选中
    Q_INVOKABLE void addFavorite(const QString &path);     // #243 右键"添加到收藏夹"(网格/树菜单经元调用进来)
    void enterFullscreen();          // Fullscreen/dualMonitor:可选落到第二显示器
    void renameCurrent();            // F2:按 FileOps/renameDialog 决定对话框/就地改
    // #136:F2/F3 的统一入口 —— 焦点在文件树就改树里那一行,否则改文件页选中项
    void renameFocused();
    int  seekSeconds() const;        // Viewer/seekSeconds:快进/快退秒数(默认 3)
    // 查看器标签卡(Interface/multiViewerTabs / oneViewerTab)
    Q_INVOKABLE void openViewerTab(const QString& path);  // 右键"在新标签卡中打开"
    // 2026-09-02 用户令:双击预览区 = 开新标签并选中它;Ctrl+双击 = 后台开(焦点不跳走)
    Q_INVOKABLE void openTabForeground();   // 双击:进查看器 + 开新标签 + 选中
    Q_INVOKABLE void openTabBackground();   // Ctrl+双击:后台开新标签,焦点留在浏览器
    // #221(2026-09-04 用户令):预览区双击按态分流 —— 浏览器=开签(原语义),
    // 查看器=关当前签回浏览器,G 全屏=先退全屏再看身在何处
    Q_INVOKABLE void previewDoubleClicked();
    void restoreClosedViewerTab();             // Ctrl+Shift+T:恢复最近关掉的文件标签
    void pushClosedTab(const QString& path);   // 关签时记入恢复栈(去重,留 50)
    void syncViewerTab(const QString& path);
    void updateTabBarVis();   // 标签栏显隐总闸(2026-09-03:浏览器态有图签也显示)
    void closeViewerTab(int index);              // 关闭按钮/标签右键菜单/中键
    Q_INVOKABLE void toggleViewer();   // 浏览器 ↔ 查看器(单图模式)
    Q_INVOKABLE void toggleFullView();   // G(#154):全屏预览=只铺画面,不进查看器不碰标签
    void exitFullView();           // G/ESC/F11/浮动工具条退出:精确还原进前布局
    void applyFullViewChrome();    // 菜单栏/标签条随全屏形态收放
    Q_INVOKABLE void viewerBack();     // ESC:查看器退回浏览器(幂等)
    Q_INVOKABLE void refresh();        // 重载当前目录(F5/工具栏/布局菜单)
    Q_INVOKABLE void reloadAfterDelete(const QString& deletedPath);  // 删除后重载并选中下一项
    Q_INVOKABLE void releaseFileLocks(const QStringList& paths);  // #214:删/移/改名前放掉预览握着的句柄
    // 启动收尾:主窗口首帧显示后恢复上次选中文件(防 QVideoWindow 独立闪框,
    // 由 main.cpp 在 opacity 恢复同拍调用;构造期只记 m_startupRestoreFile)
    void restoreStartupPreview();
    // 预热预览媒体栈(QMediaPlayer/QVideoWidget 首建数秒,挪出点击路径),
    // 由 main.cpp 在 show 后与 restoreStartupPreview 同拍调用(实现在 .cpp,
    // 这里只有前向声明)
    void warmUpPreviewMedia();
    // 切换模式触发键(设置→交互→切换模式):"SwitchMode/doubleClick" 等
    Q_INVOKABLE void requestSwitchMode(const QString& triggerKey);
    void saveLayout(const QString& name);   // 布局保存/应用(查看→布局;退出自动存 _last)
    void applyLayout(const QString& name);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;   // WindowStateChange→全屏chrome收放(#153/#154)
    // ── 拖放(#81)──
    // 拖入:文件→导航到所在目录并选中首个文件;目录→直接进该目录。
    // 拖入目标若落在某个文件夹上(网格卡片或树节点)则按移动/复制语义落盘。
    // 可放置区域只有网格与树(#109):拖到别处 dragMove 被 ignore → 禁止光标,松开无动作。
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;   // 拖出窗口:清浮标+树白框
    void dropEvent(QDropEvent* e) override;

private:
    bool dropOnValidTarget(const QPoint& pos) const;   // 落点是否在网格/树内
    // 落点是否"有意义"(2026-09-03):内部起拖(源=网格)时,落点必须是一个
    // 真正要移动进去的文件夹 —— 网格空白/非文件夹卡片(目标=当前目录)与
    // 树中源所在目录节点(自己移自己)都判无效 → dragMove 显示禁止光标。
    // 外部拖入(资源管理器)保持原语义:空白=导航,文件夹=移动/复制。
    bool dropTargetMeaningful(const QPoint& pos, const QDropEvent* e) const;

    void createMenubar();
    void createStatusbar();
    // 主题切换:全局 QSS 覆盖不到的"构造期内联样式表/填充期缓存色"在这里重灌。
    // 由 Theme::addChangeHandler 在 ctor 注册,设置页切换主题 → notifyChanged →
    // 本函数按新色重设各面板/工具栏/状态栏样式 + 让子树重建缓存色。
    void applyThemeSurfaces();
    void updateStatus();
    // ── #241 文件夹大小后台精确统计(updateStatus 单选目录时驱动) ──
    // #244:staleSeed ≥ 0 = 缓存库有旧值,本轮是静默重校验(不报中途进度,
    // 状态栏稳定显旧值,收尾悄悄替换)
    void startDirSizeRun(const QString& path, qint64 staleSeed = -1);
    // #10(2026-09-05 用户令):文件页里悬停到文件夹也要出大小 —— 悬停驱动,
    // 一次只统计一个目录(缓存/库命中即时回,否则进共享统计池串行算)
    void onGridDirSizeRequested(const QString& path);
    void cancelDirSizeRun();                     // 不再看单目录时停旧统计
    void applyDirSizeProgress(const QString& path, quint64 runId, qint64 bytes);  // 线程中途上报
    void applyDirSizeDone(const QString& path, quint64 runId, qint64 bytes);      // 线程收尾
    void onSelectionChanged(const QString &path);
    // 注:onGridDirSelected(文件页单选目录卡→树镜像)已按用户 2026-09-03 裁决移除,
    // 单击目录卡不再动树;树只在 navigateTo(双击打开/地址栏/历史/上级)里同步。
    void onSizeChanged(int value);
    void onThumbZoom(int delta);
    void goBack();
    void goForward();
    void goUp();   // 上级目录(Backspace/工具栏):跳成后定位刚离开的子文件夹
    void gotoTypedPath();   // 地址栏回车(#109②):目录进目录 / 文件定位到它 / 都不像就吭一声
    void updateNavEnabled();   // 按游标刷新"后退/前进"菜单项+工具栏按钮的可用性(#87)
    // 查看器标签:文件路径存在 QTabBar 的 tabData 里(唯一真源,
    // 拖拽重排/removeTab 都带着它走,不需要并行的路径数组保持同步)
    QString tabPath(int index) const;
    int  indexOfTabPath(const QString& path) const;
    int  addViewerTab(const QString& path);                 // 追加标签,返回索引
    void setViewerTabPath(int index, const QString& path);  // 就地换某标签指向的文件
    void installTabCloseButton(int index);  // 自绘 × (主题色,系统图标在深色下看不见)
    Q_INVOKABLE void pruneDeadViewerTabs();  // 丢掉指向已消失文件的标签(删除后网格也会叫)
    // #105:索引 0 常驻「浏览器」标签(tabData=哨兵),点它回标准模式
    bool isBrowserTab(int index) const;
    int  firstImageTab() const;             // 第一个图片标签,-1=无
    int  imageTabCount() const;
    void ensureBrowserTab();                // 常驻标签缺失/错位时补齐归位
    void requestTabThumb(const QString& path);  // 标签名左侧小缩略图(异步)

    // 模式切换/标题/幻灯片(设置→交互 & 设置→标题栏)
    void cycleMode(int spec);                 // SwitchMode/* 规格 0-4
    void applyTitle();                        // 按模板刷新窗口标题
    void toggleSlideshow();                   // Keyboard/space=快速幻灯片
    void syncFilterIndicators(int mode);      // #107:筛选指示器总同步(格式下拉框+红标钮背景+m_redFilterMode)
    // 标题模板求值(浏览器/查看器共用;空 template 时回退默认)
    QString renderTitle(const QString& templateText, const QString& filePath) const;

    // 布局方案:保存/应用窗口几何与分栏宽度;"跟随上次"=用关闭时的状态
    // (saveLayout/applyLayout 声明见 public 区,供菜单/外部调用)
    void createLayoutMenu();                   // 菜单栏"布局"(追加到当前末尾)
    void applyLastLayout();                  // 应用上次关闭时的状态
    QString splitterCsv() const;             // 可落盘的分栏宽度(查看器模式下取进入前的值)

    // 批次 2:菜单/工具栏/标记/最近文件
    QMenu* createViewModeMenu(QWidget* parent);   // 查看方式 7 种
    QMenu* createSortMenu(QWidget* parent);       // 排序子菜单
    QMenu* createFilterMenu(QWidget* parent);     // 筛选子菜单
    void   createToolbar2(QVBoxLayout* intoCenter); // 工具栏第二行
    void   createViewMenu();                    // 一级菜单"视图"(面板开关,追加在布局之后)
    QWidget* createPaneHeader(const QString& title, const char* paneId); // XnView 式面板标题条(带关闭 X)
    void   setPaneVisible(const char* paneId, bool on, bool remember = true); // 面板显隐(含记忆用户意图)
    bool   paneVisible(const char* paneId) const; // 用户意图(非查看器模式下的临时隐藏)
    void   restorePanes(const QString& csv);     // "tree,preview,.." 恢复可见面板(空=全部)
    void   restoreDocks(const QByteArray& hex);  // dock 位置存档恢复 + 意图纠正(restoreState 包装)
    QStringList paneIds() const;                  // 全部面板 id(顺序稳定)
    bool   paneOn(const QString& id) const;       // m_panesOn 查询
    void   applyPaneVisibility();                // 意图 + 查看器模式 → 实际 setVisible
    void   saveFavorites();                      // #243 收藏夹落盘(Favorites/paths)
    void   addRecentFile(const QString& path);   // 内存操作 + 防抖合批写盘
    void   flushRecentFiles();                   // 把内存列表写回 ini(closeEvent 也调用)
    void   ensureRecentLoaded();                 // 懒加载内存副本(ini 只读一次)
    void   trimRecentList();                     // 按 Interface/maxRecent 截断内存副本
    void   rebuildRecentMenu(QMenu* menu);
    void   openWithSystem(const QString& path);
    void   cycleRedFilter();                      // 红标筛选三态循环
    void   applyColorLabel(int color);            // 快捷键入口
    void   editCustomFilter();                    // #125:自定义筛选 = 编辑扩展名清单
    void applyShortcuts();                      // 应用自定义快捷键(ini)
    void collectMenuActions(QMenu* menu, QList<QAction*>& out);
    // ── 2026-09-02 全屏胶片条(filmstrip)──
    // G 全屏预览时光标移到顶部浮现缩略图条;#203 重做为 FilmStrip 独立控件
    //(QListView 虚拟化:全目录可滚、当前项居中、悬停反馈、题注行)。
    void createFilmStrip();                    // ctor:建隐藏的顶部条
    void updateFilmStrip(const QPoint* cursor); // 光标到顶显示、离开条与触发区隐藏
    void updateFullNavButtons(const QPoint* cursor); // #220:G 全屏左右浮动钮显隐+几何
    void refreshFilmStrip();                   // 目录/当前文件变化时重建或跟随
    // 2026-09-02 拖放提示:拖动时更新光标旁"复制/移动"浮标,并高亮落点文件夹
    void updateDragHint(const QPoint& pos, bool valid);  // valid=落在可放置区
    void hideDragHint();
    void updateFolderDropTarget(const QPoint& pos, bool highlight); // 树落点白框

    QSplitter *m_splitter = nullptr;
    FolderTree *m_folderTree = nullptr;
    SortHeader *m_sortHeader = nullptr;
    FileGrid *m_fileGrid = nullptr;
    QComboBox *m_formatFilterCombo = nullptr;
    PreviewPanel *m_preview = nullptr;
    QLineEdit *m_addrBar = nullptr;
    // 地址栏单击全选(#109①③):按下前是否已整条选中 + 按下点(用来分清单击和拖选)
    QPoint  m_addrPressPt;
    bool    m_addrWasAllSelected = false;
    // #127:一次焦点期内只自动全选一次。旧写法每次"未全选→点击"都 selectAll,
    // 于是第三次点击又变全选,用户没法在路径中间改字 —— 第二次起就该只放光标。
    bool    m_addrSelectedOnce = false;
    // #128②:地址栏跳转后的一次宽限。跳完焦点落网格且自动选中第一项,
    // 同一个 Enter 的后续事件会再被"回车=切换查看器"吃一次(用户实测)。
    qint64  m_lastAddrJumpMs = 0;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_pathLabel = nullptr;
    // ── #241 文件夹大小:后台精确统计的状态(全部 GUI 线程独占,stop 旗除外) ──
    QString m_dirSizeTarget;                          // 正在统计/已有精确值的目录
    std::shared_ptr<std::atomic_bool> m_dirSizeStop;  // 本轮取消旗(线程协同停)
    quint64 m_dirSizeRunId   = 0;    // 轮次;旧线程的迟到上报按代作废
    // 专用单线程池(2026-09-05):全树递归统计是大 I/O 长任务,过去丢进全局池,
    // 与缩略图缓存清扫等任务互抢,大目录一统计磁盘就满载(用户报"硬盘异响");
    // 独占 1 线程即满足"同一时刻只跑一个统计",也不再挤压别处。
    QThreadPool* m_dirSizePool = nullptr;
    QHash<QString,qint64> m_gridDirSizes;   // #10 悬停统计的会话缓存(path→字节)
    QSet<QString>         m_gridDirPending; // 在途去重(网格侧同样有,双保险)
    bool    m_dirSizeRunning = false;
    bool    m_dirSizeDone    = false;  // target 已有精确值(会话内缓存)
    qint64  m_dirSizeValue   = 0;    // 精确总字节
    qint64  m_dirSizePartial = 0;    // 统计中的累计值(状态栏随之增长)
    // #244 缓存库重校验态:库里有旧值,先显旧值、后台静默重算,算完悄悄替换
    bool    m_dirSizeStale      = false;
    qint64  m_dirSizeStaleValue = 0;
    QStringList m_history;   // 目录导航历史
    int m_histIdx = -1;
    bool m_histNav = false;  // 历史跳转中,不再入栈
    // 树点击引起的导航:from tree 的 navigateTo 不再把焦点抢回网格 —— 否则
    // 用户刚点完树,文件页又"自认为正被操作",两边的选中亮/暗色全跟着错位
    bool m_navFromTree = false;
    void onTreeFolderSelected(const QString& path);   // folderSelected 包装:标记来源再导航
    // 历史的四个"出口"都要随游标禁用，不然到头时按下去静默无事(#87)
    QAction* m_actBack = nullptr;
    QAction* m_actFwd = nullptr;
    QToolButton* m_btnBack = nullptr;
    QToolButton* m_btnFwd = nullptr;
    QList<int> m_savedSplitter;  // 进查看器前的分栏宽度(退回时原样恢复)
    bool m_fullView = false;     // #154 全屏预览中(独立于查看器模式,不碰标签页)
    QList<int> m_fullViewSplitter;  // 进全屏预览前的分栏宽度(退出时原样恢复)
    Qt::WindowStates m_preFullViewState = Qt::WindowNoState; // 进全屏预览前的窗口状态(2026-09-02:退出时恢复最大化,不再被 showNormal 打回普通)
    int  m_redFilterMode = 0; // 红标筛选三态:0全部 1仅红标 2仅非红标
    QToolButton* m_redBtn = nullptr; // 红标三态钮,蓝色背景指示器由 syncFilterIndicators 独家维护(#107)
    QToolButton* m_btnViewToggle = nullptr; // #266:缩略图↔详细信息两态切换钮(勾选态由 syncViewModeUI 维护)
    QList<QAction*> m_viewModeActions;      // #266:两份"查看方式"菜单的条目集合,勾选统一重勾
    void syncViewModeUI(int mode);          // #266:查看方式变化的唯一 UI 同步口(菜单勾选+切换钮)
    QTabBar*    m_viewerTabs = nullptr;  // 查看器标签条(浏览器态有图签也显示,见 updateTabBarVis)
    QStringList m_closedTabs;            // #221:最近关闭的文件标签路径(新→旧,Ctrl+Shift+T 恢复)
    // ── 2026-09-02 全屏胶片条 ──
    FilmStrip*          m_filmStrip = nullptr;  // 顶部缩略图条(全屏预览,光标到顶显示)
    bool                m_filmDirty = false;    // 目录列表变了,胶片条下次显示要重建
    QString             m_filmDir;              // 胶片条上次装载的目录(判目录切换)
    // #220 G 全屏左右浮动钮(上一个/下一个;光标挪到屏幕边缘才显示,挪走即藏)
    QToolButton*        m_fullNavPrev = nullptr;
    QToolButton*        m_fullNavNext = nullptr;
    // 2026-09-02 拖放:光标旁"复制/移动"浮标 + 落点文件夹高亮
    QLabel*             m_dragHint = nullptr;   // 拖动时跟随光标的动作提示(隐藏态)
    bool   m_viewerNoSync = false;       // 进查看器时不要就地改标签(由"开新标签"自己追加)
    bool m_viewerMode = false; // 查看器(单图)模式
    QDockWidget* m_treeDock = nullptr;  // objectName="tree" → 左停靠区(收藏夹/筛选器/信息可拖其正下方自由拼列)
    QWidget* m_centerPane = nullptr;  // 网格面板(查看器模式隐藏)
    QWidget* m_previewPane = nullptr; // 预览面板包装(标题条 + PreviewPanel)
    QWidget* m_previewHdr = nullptr;  // 预览标题条(查看器模式下隐藏,单图不需要)
    QWidget* m_infoPane  = nullptr;  // #80 信息面板内容(dock "info" 的 widget)
    InfoPanel* m_info    = nullptr;  // #80 元数据表 + 直方图
    QWidget* m_favPane   = nullptr;  // #243 收藏夹内容(dock "favorites" 的 widget)
    FavoritesPanel* m_favs = nullptr;  // #243 收藏夹列表(数据真源=m_favPaths)
    QWidget* m_filterPane = nullptr; // #242 分类筛选器内容(dock "filter" 的 widget)
    FilterPanel* m_filterPnl = nullptr; // #242 条件真源(勾选即落盘 Filter/*)
    // Dock 化:三辅面板外壳,可拖动/合并成标签组/浮动;树/预览/网格仍归 splitter
    // (查看器模式、G 全屏、Layout/* 三段存档全部不碰)。可见性唯一出口仍是
    // applyPaneVisibility,saveState/restoreState 只管位置/大小/浮动形态
    QDockWidget* m_infoDock   = nullptr;  // objectName="info"      → 右停靠区
    QDockWidget* m_favDock    = nullptr;  // objectName="favorites" → 左停靠区
    QDockWidget* m_filterDock = nullptr;  // objectName="filter"    → 左停靠区(收藏夹之下)
    bool m_restoringDocks = false;        // restoreState 期间抑制 visibilityChanged 回写
    QWidget* m_addrRow = nullptr;     // 地址栏行(视图菜单可隐藏)
    QWidget* m_toolRow = nullptr;     // 工具栏第二行(视图菜单可隐藏)
    // 面板开关 action(视图菜单),与 m_panesOn 同步 ✓
    QAction* m_paneActs[8] = {};   // 容量须 >= kPaneCount(新增 filter 面板后为 8)
    // 用户意图:当前应显示的面板 id 列表(顺序同 paneIds)。
    // 查看器模式的临时隐藏不改这里,避免污染持久化状态
    QStringList m_panesOn;
    QStringList m_favPaths;           // #243 收藏夹真源(canonical 形,启动从 ini 读,改动即写)
    // 最近文件:内存列表为唯一真源,定时合批写盘(连续切换不再每次同步落盘)
    QStringList m_recentList;         // 内存副本(首次用到时从 ini 懒加载)
    bool        m_recentLoaded = false;
    int         m_recentMax = 20;    // 上限(懒加载时读一次,选中切换不再逐次读 ini)
    QTimer      m_recentFlushTimer;  // 单发 500ms,超时统一写盘
    QString     m_currentFile;       // 当前预览文件(标题模板 {文件名…} 求值用)
    QString     m_currentDir;        // 当前目录规范形('/' 无尾斜杠);地址栏只负责显示
    // Start/rememberFilename 的延迟恢复:构造期只记路径,主窗首帧后由
    // restoreStartupPreview() 选中(避免视频窗先于主窗显示造成启动闪框)
    QString     m_startupRestoreFile;
    QTimer      m_slideTimer;        // 快速幻灯片(Keyboard/space=快速幻灯片)
    bool        m_slideshow = false;
    // ── #218 长驻工具窗(非模态单例)──
    // 弹窗不得锁主窗:主窗右上角 X 与任务栏关闭在弹窗开着时必须仍可点。
    // exec()=应用级模态正是"打开设置后连 Gaze 都关不掉"的根因。
    // 侧挂 finished→deleteLater:窗一关(含 取消/Esc 的 reject)对象即析构,
    // QPointer 槽位自动落空,再按入口重建全新实例(设置重读 ini)。
    QPointer<QDialog> m_settingsDlg;
    QPointer<QDialog> m_imgSearchDlg;
    QPointer<QDialog> m_dbMaintDlg;
};
