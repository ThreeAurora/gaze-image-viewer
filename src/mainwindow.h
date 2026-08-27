#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QStringList>

class QVBoxLayout;
class FolderTree;
class FileGrid;
class PreviewPanel;
class InfoPanel;
class SortHeader;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    Q_INVOKABLE void navigateTo(const QString &path);
    Q_INVOKABLE void openFullscreen(const QString &path);  // 右键"全屏":导航到文件并全屏
    Q_INVOKABLE void revealFile(const QString &path);      // 以文搜图结果:定位到目录并选中
    Q_INVOKABLE void toggleViewer();   // 浏览器 ↔ 查看器(单图模式)
    Q_INVOKABLE void viewerBack();     // ESC:查看器退回浏览器(幂等)
    void saveLayout(const QString& name);   // 布局保存/应用(查看→布局;退出自动存 _last)
    void applyLayout(const QString& name);





protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    // ── 拖放(#81)──
    // 拖入:文件→导航到所在目录并选中首个文件;目录→直接进该目录。
    // 拖入目标若落在某个文件夹上(网格卡片或树节点)则按复制语义拷过去。
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void createMenubar();
    void createStatusbar();
    void updateStatus();
    void onSelectionChanged(const QString &path);
    void onSizeChanged(int value);
    void onThumbZoom(int delta);
    void goBack();
    void goForward();
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
    // 标题模板求值(浏览器/查看器共用;空 template 时回退默认)
    QString renderTitle(const QString& templateText, const QString& filePath) const;

    // 布局方案:保存/应用窗口几何与分栏宽度;"跟随上次"=用关闭时的状态
    // (saveLayout/applyLayout 声明见 public 区,供菜单/外部调用)
    void createLayoutMenu(QAction* before);  // 菜单栏"布局"(插在指定项前)
    void applyLastLayout();                  // 应用上次关闭时的状态

    // 批次 2:菜单/工具栏/标记/最近文件
    QMenu* createViewModeMenu(QWidget* parent);   // 查看方式 7 种
    QMenu* createSortMenu(QWidget* parent);       // 排序子菜单
    QMenu* createFilterMenu(QWidget* parent);     // 筛选子菜单
    void   createToolbar2(QVBoxLayout* intoCenter); // 工具栏第二行
    void   addRecentFile(const QString& path);   // 内存操作 + 防抖合批写盘
    void   flushRecentFiles();                   // 把内存列表写回 ini(closeEvent 也调用)
    void   rebuildRecentMenu(QMenu* menu);
    void   openWithSystem(const QString& path);
    void   cycleRedFilter();                      // 红标筛选三态循环
    void   applyColorLabel(int color);            // 快捷键入口
    void   applyShortcuts();                      // 应用自定义快捷键(ini)
    void   collectMenuActions(QMenu* menu, QList<QAction*>& out);

    QSplitter *m_splitter = nullptr;
    FolderTree *m_folderTree = nullptr;
    SortHeader *m_sortHeader = nullptr;
    FileGrid *m_fileGrid = nullptr;
    PreviewPanel *m_preview = nullptr;
    QLineEdit *m_addrBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_tooltip = nullptr;
    bool m_fullscreenMode = false;
    QStringList m_history;   // 目录导航历史
    int m_histIdx = -1;
    bool m_histNav = false;  // 历史跳转中,不再入栈
    QList<int> m_savedSplitter;  // 进查看器前的分栏宽度(退回时原样恢复)
    bool m_fullView = false;     // #154 全屏查看中(独立于查看器模式,不碰标签页)
    QList<int> m_fullViewSplitter;  // 进全屏预览前的分栏宽度(退出时原样恢复)
    int  m_redFilterMode = 0; // 红标筛选三态:0全部 1仅红标 2仅非红标
    bool m_viewerMode = false; // 查看器(单图)模式
    QWidget* m_treePane = nullptr;    // 树面板(查看器模式隐藏)
    QWidget* m_centerPane = nullptr;  // 网格面板(查看器模式隐藏)
    // 最近文件:内存列表为唯一真源,定时合批写盘(连续切换不再每次同步落盘)
    QStringList m_recentList;         // 内存副本(首次用到时从 ini 懒加载)
    bool        m_recentLoaded = false;
    int         m_recentMax = 20;    // 上限(懒加载时读一次,选中切换不再逐次读 ini)
    QTimer      m_recentFlushTimer;  // 单发 500ms,超时统一写盘
    QWidget* m_previewPane = nullptr; // 预览面板包装(标题条 + PreviewPanel)
    QWidget* m_infoPane  = nullptr;  // #80 信息面板容器(含标题条,挂在预览栏内)
    InfoPanel* m_info    = nullptr;  // #80 元数据表 + 直方图
    QWidget* m_addrRow = nullptr;     // 地址栏行(视图菜单可隐藏)
    QWidget* m_toolRow = nullptr;     // 工具栏第二行(视图菜单可隐藏)
    // 面板开关 action(视图菜单),与 m_panesOn 同步 ✓
    QAction* m_paneActs[6] = {};   // 容量须 >= kPaneCount(新增 info 面板后为 6)
    // 用户意图:当前应显示的面板 id 列表(顺序同 paneIds)。
    // 查看器模式的临时隐藏不改这里,避免污染持久化状态
    QStringList m_panesOn;
    // 最近文件:内存列表为唯一真源,定时合批写盘(连续切换不再每次同步落盘)
    QStringList m_recentList;         // 内存副本(首次用到时从 ini 懒加载)
    bool        m_recentLoaded = false;
    int         m_recentMax = 20;    // 上限(懒加载时读一次,选中切换不再逐次读 ini)
    QTimer      m_recentFlushTimer;  // 单发 500ms,超时统一写盘
public:
    Q_INVOKABLE void applyLayoutByName(const QString& name) { applyLayout(name); }
};
