#pragma once
#include <QScrollArea>
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <QShortcut>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QLabel>
#include <QToolButton>
#include <vector>
#include "fileentry.h"
#include "sortheader.h"

class FileCanvas;
class QPainter;
class SortHeader;

// 筛选模式(查看菜单/工具栏)
enum FilterMode {
    FILTER_ALL = 0,
    FILTER_IMAGES, FILTER_IMAGES_DIRS,
    FILTER_VIDEOS, FILTER_VIDEOS_DIRS,
    FILTER_AUDIO, FILTER_ARCHIVES,
    FILTER_DOCUMENTS, FILTER_EXECUTABLES, FILTER_FOLDERS,
    FILTER_RED, FILTER_ORANGE, FILTER_YELLOW, FILTER_GREEN, FILTER_BLUE,
    FILTER_UNRED,                       // 非红色标记(红标三态按钮的第 3 态)
    // #125:自定义扩展名集合筛选。追加在末尾而不是插在中间 ——
    // Browser/filterMode 存的是这个枚举的**数值**,插中间会把红标等档位的存档
    // 全部错位(用户重启后筛选悄悄变成别的)。
    FILTER_CUSTOM,
};

// 查看方式(查看菜单/工具栏)
enum ViewMode {
    VM_THUMBS = 0,      // 缩略图(纯图)
    VM_THUMBS_NAME,     // 缩略图 + 文件名(默认)
    VM_THUMBS_LABEL,    // 缩略图 + 标签
    VM_THUMBS_DETAIL,   // 缩略图 + 详细
    VM_ICONS,           // 图标(小图 + 名称)
    VM_LIST,            // 列表
    VM_DETAILS,         // 详细信息
    VM_WATERFALL,       // 瀑布流
};

// 文件名排序方式(排序菜单:数字顺序/字母顺序/正常顺序)
enum NameOrder {
    NameNatural = 0,   // 数字感知:img2 < img10(资源管理器风格,默认)
    NameAlpha,         // 纯字母序:img10 < img2
    NameNormal,        // 系统排序规则(区域设置 collator,不启用数字模式)
};

// 文件名排序方式(排序菜单:数字顺序/字母顺序/正常顺序)
enum NameOrder {
    NameNatural = 0,   // 数字感知:img2 < img10(资源管理器风格,默认)
    NameAlpha,         // 纯字母序:img10 < img2
    NameNormal,        // 系统排序规则(区域设置 collator,不启用数字模式)
};

class FileGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit FileGrid(QWidget* parent = nullptr);

    void loadDirectory(const QString& dirPath);
    void refreshCurrentDir();    // 重新加载当前目录(文件操作后)
    // 删除后重载:选中被删项的下一项(末项则上一项),对齐 XnView
    void reloadAfterDelete(const QStringList& deleted);
    // 删除后重载:选中被删项的下一项(末项则上一项),对齐 XnView
    void reloadAfterDelete(const QStringList& deleted);
    // 删除后重载:选中被删项的下一项(末项则上一项),对齐 XnView
    void reloadAfterDelete(const QStringList& deleted);
    // 删除后重载:选中被删项的下一项(末项则上一项),对齐 XnView
    void reloadAfterDelete(const QStringList& deleted);
    void setCardSize(int size);
    void setFixedCols(int n);    // n=0 自动;1-16 手动列数(缩放时缩略图贴边缩放但列数不变)
    int  fixedCols() const { return m_fixedCols; }
    void setViewMode(int mode);  // ViewMode
    int  viewMode() const { return m_viewMode; }
    int  cardW() const;       // 卡片宽度(按查看方式;#107 缩略图尺寸菜单重勾要用)
    int  cardW() const;       // 卡片宽度(按查看方式;#107 缩略图尺寸菜单重勾要用)
    void sort(int column, bool ascending);
    void setNameOrder(int order);   // NameOrder;持久化到 Browser/nameOrder 并重排
    int  nameOrder() const { return m_nameOrder; }
    void setNameOrder(int order);   // NameOrder;持久化到 Browser/nameOrder 并重排
    int  nameOrder() const { return m_nameOrder; }
    void setFilterMode(int mode);            // FilterMode
    int  filterMode() const { return m_filterMode; }
    // 文件夹树右键"显示子文件夹中的文件":目录行仍只列本层,文件向下递归展开。
    // 真源在这里,FolderTree 只持有镜像用于画 ✓。持久化 FileList/showSubFolders。
    void setShowSubFolders(bool on);
    bool showSubFolders() const { return m_showSubFolders; }
    // 文件夹树右键"显示子文件夹中的文件":目录行仍只列本层,文件向下递归展开。
    // 真源在这里,FolderTree 只持有镜像用于画 ✓。持久化 FileList/showSubFolders。
    void setShowSubFolders(bool on);
    bool showSubFolders() const { return m_showSubFolders; }
    void navigateSelection(int delta);
    // #107 内联搜索条:Ctrl+F 落在文件列表上(不弹窗;输入即搜,Enter/Shift+Enter 翻页)
    void startFind();
    void selectIndex(int idx, bool scrollToVisible = true);  // 滚动联动时传 false 防反馈回路
    void selftestFastScroll();   // 临时诊断:进程内模拟快速拖动,查完删
    void selftestFastScroll();   // 临时诊断:进程内模拟快速拖动,查完删
    void scrollToRow(int idx);  // 首排贴顶/末排贴底/半截贴边/不可见就近贴边(不居中)/可见不动
    bool selectByPath(const QString& path);  // 按路径选中(最近文件定位用)
    // 拖放(#81):追加选中(不清空已有选中),用于一次拖进多个文件时全选
    void selectPathAdditive(const QString& path);
    QString pathAt(int idx) const;           // 条目序号 → 路径(越界/空白返回空)
    int    hitTest(const QPoint& canvasPos);  // 画布坐标 → 条目序号(拖放落点判定)
    QString neighborOf(const QString& path, int delta) const;  // 相邻文件路径(预读用)

    // 选择扩展(编辑菜单)
    void selectAllEntries();
    void selectInvert();
    enum SelectKind { KindMarked, KindFiles, KindDirs, KindImages, KindVideos, KindAudio };
    void selectByKind(int kind);
    void clearAllMarks();   // 清空 ★ 标记集(编辑菜单/Shift+M)
    void toggleMarkOnSelection();   // ★ 标记:切换选中项(多选时以当前项为准整批加/去)
    void clearAllMarks();   // 清空 ★ 标记集(编辑菜单/Shift+M)
    void toggleMarkOnSelection();   // ★ 标记:切换选中项(多选时以当前项为准整批加/去)

    // 颜色标记:对当前选中(单选时该项;多选时全部)设置
    void applyColorLabelToSelection(int color);
    int  firstSelectedIndex() const;

    QString pathOf(int index) const {   // 按序号取当前列表条目路径(越界返回空)
        return (index >= 0 && index < static_cast<int>(m_entries.size()))
            ? m_entries[index].path : QString();
    }
    QString pathOf(int index) const {   // 按序号取当前列表条目路径(越界返回空)
        return (index >= 0 && index < static_cast<int>(m_entries.size()))
            ? m_entries[index].path : QString();
    }
    QString pathOf(int index) const {   // 按序号取当前列表条目路径(越界返回空)
        return (index >= 0 && index < static_cast<int>(m_entries.size()))
            ? m_entries[index].path : QString();
    }
    QString pathOf(int index) const {   // 按序号取当前列表条目路径(越界返回空)
        return (index >= 0 && index < static_cast<int>(m_entries.size()))
            ? m_entries[index].path : QString();
    }
    int     fileCount()     const;
    int     selectedCount() const;
    int64_t selectedSize()  const;
    QStringList selectedPaths() const;
    int  currentSortCol() const { return m_sortCol; }
    bool sortAscending()  const { return m_sortAsc; }
    int  cardSizeValue()  const { return m_cardSizeAuto; }
    // 缩略图框高度(Appearance/customThumbH;0=沿用"与宽同高"的旧行为)
    int  thumbBoxH() const;
    // FileList/scanHeader:当前卷是否允许读文件头判格式
    bool headerScanAllowed(const QString& dirPath) const;
    // 各模式缩略图框以外的固定高度(文件名/详细行/内边距),boxesFor 与 cardH 共用
    static int chromeFor(int mode, int labelGap);
    int  colorLabelOf(const QString& path) const;   // 标题模板 {颜色标签}
    // 一次性落点:下次 loadDirectory 完成后选中该路径(创建副本后选中新文件用)
    void setPreferPath(const QString& p) { m_preferPath = p; }
    // FileOps/renameDialog=关:在卡片上就地改名(F2 / 右键"重命名"入口)
    void beginInlineRename();
    void endInlineRename(bool commit);
    int  colorLabelOf(const QString& path) const;   // 标题模板 {颜色标签}

signals:
    void fileCountChanged();
    void selectionChanged(const QString& currentPath);
    void filterModeChanged(int mode);
    void filterModeChanged(int mode);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    // 焦点变化必须重绘:多选落点的焦点线按 hasFocus() 画,不重绘就会留过期指示器
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // 焦点变化必须重绘:多选落点的焦点线按 hasFocus() 画,不重绘就会留过期指示器
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // 焦点变化必须重绘:多选落点的焦点线按 hasFocus() 画,不重绘就会留过期指示器
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // 焦点变化必须重绘:多选落点的焦点线按 hasFocus() 画,不重绘就会留过期指示器
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // 布局
    void updateLayout();
    void layoutCards();
    void layoutRows();        // 列表/详细信息:单列全宽行
    void layoutWaterfall();   // 瀑布流:按宽高比放最短列
    void applyFilter();     // 按 m_filterMode 从 m_allEntries 生成 m_entries
    int  colsForWidth(int w) const;
    int  cardH(int idx) const;// 卡片高度(瀑布流按宽高比)

    // 卡片管理
    FileCard* acquireCard();
    void      recycleCards();
    void      recycleInvisible(const QSet<int>& needed);
    QWidget*  m_canvas = nullptr;

    // 事件处理
    void onCardClicked(FileCard* card);
    void onCardDoubleClicked(FileCard* card);
    void onCardMiddleClicked(FileCard* card);
    void onCardMiddleClicked(FileCard* card);
    void onThumbReady(const QString& filePath, const QImage& img);
    void enqueueVisibleThumbs();  // 滚动停止后批量补齐可见卡片缩略图(防抖配套)
    void beginInlineRename();     // FileOps/renameDialog=关:就地改名
    void endInlineRename(bool commit);

    // 键盘操作
    void deleteSelection();   // 删除整个选中集(无选中时删当前项),与右键菜单同一作用域
    void newFolder();

    // 数据
    std::vector<FileEntry> m_allEntries;
    std::vector<FileEntry> m_entries;    // 筛选/排序后的显示列表
    QString m_currentDir;                // 当前加载的目录
    int    m_thumbH       = 0;     // Appearance/customThumbH:0=与宽同高(旧行为)
    bool   m_scrollPreview = true; // Browser/thumbScrollPreview:滚动中就出缩略图
    QPoint   m_dragOrigin;             // 拖出起点(画布坐标)
    int      m_dragOriginIdx = -1;     // 按下时命中的条目
    bool     m_dragStarted   = false;  // 本次按下已发起过拖拽
    QLineEdit* m_renameEdit = nullptr; // 就地改名编辑器(存在时表示正在改名)
    int        m_renameIdx  = -1;
    QString    m_renamePath;
    bool   m_lastByExt    = true;  // FileList/recognizeByExt 上次已应用值
    int    m_lastScanHeader = 0;   // FileList/scanHeader 上次已应用值
    int    m_cardSize     = 160;
    int    m_cardSizeAuto = 160;   // 自动模式的卡片尺寸(slider 值;固定列数时按宽度重算)
    int    m_lastCustomW  = 96;    // 上次看到的 Appearance/customThumbW(仅值变化才改尺寸)
    int    m_lastCustomW  = 96;    // 上次看到的 Appearance/customThumbW(仅值变化才改尺寸)
    int    m_cols         = 0;
    int    m_fixedCols    = 0;
    int    m_viewMode     = VIEW_THUMBS_NAME;
    int    m_waterfallColW = 220;   // 瀑布流列宽
    int    m_viewMode     = VM_THUMBS_NAME;
    int    m_waterfallColW = 220;   // 瀑布流列宽
    int    m_sortCol      = SORT_MDATE;
    bool   m_sortAsc      = false;
    int    m_nameOrder    = NameNatural;
    int    m_nameOrder    = NameNatural;
    int    m_filterMode   = FILTER_ALL;
    QHash<QString, int> m_colorLabels;  // path → 颜色标记(目录加载时批量读入)
    // 文件列表规则(FileList/*;设置改动时刷新,逐条目路径不再读 ini)
    bool m_showHidden  = true;
    bool m_mixSort     = false;   // 混合文件/文件夹排序(关=目录恒在最前)
    bool m_folderAlpha = true;    // 文件夹总是按字母序排列
    bool m_showSubFolders = false;// FileList/showSubFolders 递归展开子文件夹文件
    bool m_showSubFolders = false;// FileList/showSubFolders 递归展开子文件夹文件
    // 文件列表规则(FileList/*;设置改动时刷新,逐条目路径不再读 ini)
    bool m_showHidden  = true;
    bool m_mixSort     = false;   // 混合文件/文件夹排序(关=目录恒在最前)
    bool m_folderAlpha = true;    // 文件夹总是按字母序排列

    // 选择状态
    QSet<int>            m_selected;
    int                  m_lastClicked = -1;
    QString              m_preferPath;  // 一次性:本次 loadDirectory 完成后要选中的路径

    // 内联搜索条(#107):叠在视口右上角,不占布局;命中数与按钮置灰每次按键/翻页重算
    QWidget*     m_findBar  = nullptr;
    QLineEdit*   m_findEdit = nullptr;
    QLabel*      m_findInfo = nullptr;
    QToolButton* m_findPrev = nullptr;
    QToolButton* m_findNext = nullptr;
    int          m_findHitCount = 0;   // 当前查询的命中总数
    int          m_findOrdinal  = -1;  // 当前选中项是第几个命中(0 基;-1=当前项不命中)
    void buildFindBar();
    void placeFindBar();               // 视口尺寸变化后重新贴角
    void findRefresh();                // 重算命中数/序号/按钮置灰(轻量,O(n) 只在交互时跑)
    void findStep(int delta);          // +1 下一个 / -1 上一个;到边界不动(按钮已置灰)
    void closeFind();                  // Esc/✕:收条,焦点还给列表

    // 内联搜索条(#107):叠在视口右上角,不占布局;命中数与按钮置灰每次按键/翻页重算
    QWidget*     m_findBar  = nullptr;
    QLineEdit*   m_findEdit = nullptr;
    QLabel*      m_findInfo = nullptr;
    QToolButton* m_findPrev = nullptr;
    QToolButton* m_findNext = nullptr;
    int          m_findHitCount = 0;   // 当前查询的命中总数
    int          m_findOrdinal  = -1;  // 当前选中项是第几个命中(0 基;-1=当前项不命中)
    void buildFindBar();
    void placeFindBar();               // 视口尺寸变化后重新贴角
    void findRefresh();                // 重算命中数/序号/按钮置灰(轻量,O(n) 只在交互时跑)
    void findStep(int delta);          // +1 下一个 / -1 上一个;到边界不动(按钮已置灰)
    void closeFind();                  // Esc/✕:收条,焦点还给列表
    QString              m_preferPath;  // 一次性:本次 loadDirectory 完成后要选中的路径
    QString              m_preferPath;  // 一次性:本次 loadDirectory 完成后要选中的路径
    QString              m_preferPath;  // 一次性:本次 loadDirectory 完成后要选中的路径

    // 画布(唯一子控件)+ 几何缓存:结构变化时一次算完,滚动/绘制只查表
    FileCanvas* m_canvas = nullptr;
    std::vector<QRect> m_geom;
    QHash<QString, int> m_pathRow;   // path → m_entries 序号(缩略图回调 O(1) 定位)

    // 缩略图(FIFO 上限防内存无限膨胀:800 条 × ~300px pixmap ≈ 260MB 封顶)
    QHash<QString, QPixmap> m_thumbCache;   // 解码原图(按请求宽)
    QQueue<QString>         m_thumbOrder;
    // 成品图:已按当前卡片盒缩放+圆角,绘制稳态 = 一次 blit
    struct FitThumb { QRect box; QPixmap pix; };
    QHash<QString, FitThumb> m_fitCache;
    // 占位图标(文件夹/类型图标)按尺寸缓存,避免每卡片重复平滑缩放
    QHash<QString, QPixmap> m_iconCache;
    QPixmap iconPixmap(const FileEntry& e, int side);
    // 成品图:绘制路径只查缓存(命中即一次 blit);缩放/圆角在预建阶段完成
    QPixmap fitFor(const QString& path, const QRect& box) const;
    void    buildFit(const QString& path, const QRect& box, bool cover);

    // 绘制/命中窗口:m_geom 按顶边排序的序号 + 二分定位,
    // 让每帧成本只与"视口内条目数"有关,与目录总条目数无关
    std::vector<int> m_byY;
    int  m_maxCardH   = 0;
    int  lowerBoundRow(int y) const;

    QTimer m_resizeTimer;
    QTimer m_reEnqueueTimer;  // 尺寸停止变化后重新生成高清缩略图(防抖)
    QTimer m_scrollCoalesce;  // 滚动中合并为"每轮事件循环一次"请求可见缩略图
    int    m_lastScrollVal = -1;   // 跨屏跳转时清掉离屏解码队列
    bool   m_loading = false;
    int    m_hoverIdx = -1;   // 悬停条目(自绘高亮)

    // 外观:间距由设置驱动(Appearance/spacing);MARGIN 画布留边固定
    int    m_spacing = 6;
    static constexpr int MARGIN  = 8;
    // 卡片外观缓存(逐条目绘制路径只读内存,禁读 ini —— 项目铁律)
    int  m_border     = 0;    // Appearance/borderSize
    int  m_imageAlign = 1;    // Appearance/imageAlign 0左 1中 2右
    int  m_labelAlign = 1;    // Appearance/labelAlign
    int  m_labelGap   = 6;    // Appearance/labelSpacing 真=6px 假=0
    bool m_showRating = true; // Browser/showRating 颜色标记圈
    bool m_sizeBytes  = false;// FileList/sizeInBytes
    void applyAppearance();   // 启动时 + 设置变更后各刷一次

    void relayoutNow();     // 设置改动后重算列宽并重排(间距/尺寸类)
};
