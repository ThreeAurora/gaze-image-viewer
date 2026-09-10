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
    // #125 自定义扩展名集合筛选。**追加在末尾**,不插在中间:Browser/filterMode
    // 存的是枚举数值,插中间会让用户已存的"红标/文件夹"等档位错位。
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

// #242 分类筛选器面板:第二层筛选(单模式 filter 之后再收一道,两道相与)。
// 颜色与类型两个维度;维度内多选=任一命中,维度间按 andMode 与/或。
// 目录行不参与条件判定恒显示(递归范围下目录行是导航骨架,筛没了没法下钻)。
struct MultiFilterSpec {
    QSet<int> colors;    // 颜色标记 1红~5蓝,空=该维不参与
    QSet<int> cats;      // 类型 0图像(含RAW) 1视频 2音频 3文档 4可执行 5压缩,空=该维不参与
    bool andMode = false;   // false=任一维度命中即显示(OR) true=两个维度都要命中(AND)
    bool active  = false;   // colors+cats 全空=不生效
};

class FileGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit FileGrid(QWidget* parent = nullptr);

    void loadDirectory(const QString& dirPath);   // 枚举在池线程跑(#6 启动灰块/挂死),就绪后 GUI 应用
    void refreshCurrentDir();    // 重新加载当前目录(文件操作后)
    // 主题切换:重灌画布背景色(构造期内联样式表,QSS 刷新覆盖不到)
    void refreshThemeColors();
    // 删除后重载:选中被删项的下一项(末项则上一项),对齐 XnView
    void reloadAfterDelete(const QStringList& deleted);
    void setCardSize(int size);
    void setFixedCols(int n);    // n=0 自动;1-16 手动列数(缩放时缩略图贴边缩放但列数不变)
    int  fixedCols() const { return m_fixedCols; }
    void setViewMode(int mode);  // ViewMode
    int  viewMode() const { return m_viewMode; }
    // #267:排序表头挂接(详细列表的定宽列布局与显隐由网格同源驱动)
    void setSortHeader(SortHeader* h);
    int  cardW() const;       // 卡片宽度(按查看方式;#107 缩略图尺寸菜单重勾要用)
    void sort(int column, bool ascending);
    void setNameOrder(int order);   // NameOrder;持久化到 Browser/nameOrder 并重排
    int  nameOrder() const { return m_nameOrder; }
    void setFilterMode(int mode);            // FilterMode
    int  filterMode() const { return m_filterMode; }
    // #242 分类筛选器:第二层筛选(与单模式 filter 相与)。条件变化=只重筛;
    // clearMultiFilter=面板关闭,清条件并把范围复位 0(需要时重扫当前目录)
    void setMultiFilter(const MultiFilterSpec& spec);
    void clearMultiFilter();
    // 范围 0=当前目录 1=当前目录(递归) 2=全局标记库;改变条目宇宙,需重扫
    void setMultiFilterScope(int scope);
    int  multiFilterScope() const { return m_mfScope; }
    // 文件夹树右键"显示子文件夹中的文件":目录行仍只列本层,文件向下递归展开。
    // 真源在这里,FolderTree 只持有镜像用于画 ✓。持久化 FileList/showSubFolders。
    void setShowSubFolders(bool on);
    bool showSubFolders() const { return m_showSubFolders; }
    void navigateSelection(int delta);
    // #107 内联搜索条:Ctrl+F 落在文件列表上(不弹窗;输入即搜,Enter/Shift+Enter 翻页)
    void startFind();
    void selectIndex(int idx, bool scrollToVisible = true);  // 滚动联动时传 false 防反馈回路
    void scrollToRow(int idx);  // 首排贴顶/末排贴底/半截贴边/不可见就近贴边(不居中)/可见不动
    bool selectByPath(const QString& path);  // 按路径选中(最近文件定位用)
    // 拖放(#81):追加选中(不清空已有选中),用于一次拖进多个文件时全选
    void selectPathAdditive(const QString& path);
    QString pathAt(int idx) const;           // 条目序号 → 路径(越界/空白返回空)
    // #10 悬停文件夹大小:主窗统计完成回填 → 定点重绘该行
    void setDirSize(const QString& dirPath, qint64 bytes);
    void retryDirSize(const QString& dirPath);   // 统计被中断:解除"已问",下次悬停重发
    QString currentDir() const { return m_currentDir; }   // #203 胶片条对账数据来源
    QStringList allFilePaths() const;   // #225 胶片条数据源:目录全部文件(跳目录行,按 showHidden)
    int    hitTest(const QPoint& canvasPos);  // 画布坐标 → 条目序号(拖放落点判定)
    // 任意祖先控件坐标(如主窗口) → 条目序号:内部换算到画布内容坐标,自动含
    // 滚动补偿。2026-09-09 拖放禁止光标回归的根因——调用方用 mapFrom(滚动容器)
    // 得到视口坐标,列表滚动后与内容坐标差一个滚动量,落点解析整体钉在列表顶部。
    int    hitTestFrom(const QPoint& ancestorPos, const QWidget* from);
    QString neighborOf(const QString& path, int delta) const;  // 相邻文件路径(预读用)

    // 选择扩展(编辑菜单)
    void selectAllEntries();
    void selectInvert();
    enum SelectKind { KindFiles, KindDirs, KindImages, KindVideos, KindAudio };
    void selectByKind(int kind);

    // 颜色标记:对当前选中(单选时该项;多选时全部)设置
    void applyColorLabelToSelection(int color);
    int  firstSelectedIndex() const;

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

    // #10 悬停文件夹大小:网格向主窗要值,值到了 setDirSize 定点重绘
    QString entrySizeText(const FileEntry& e) const;   // 目录=统计值/统计中…,文件=常规
    QHash<QString,qint64> m_dirSizes;      // 已知目录大小(path→字节)
    QSet<QString>         m_dirSizeAsked;  // 已请求过(防重复发信号)
    QTimer                m_dirSizeTimer;  // 悬停驻留闸(450ms 后才发起统计)
    QTimer                m_dirResortTimer; // #248:大小排序下补值到达 → 30ms 合并重排
    QString               m_dirSizeHoverPath;

signals:
    void fileCountChanged();
    void dirSizeRequested(const QString& dirPath);
    void selectionChanged(const QString& currentPath);
    // 注:2026-09-03 曾加过 dirSelected(鼠标单选目录卡→文件树镜像选中),
    // 用户裁决「选中文件夹时树应当留在原处,只有双击打开才同步」后整条链路已移除。
    // 树同步的唯一落点是 MainWindow::navigateTo 里的 FolderTree::focusPath。
    void filterModeChanged(int mode);
    // #266:查看方式变化 → MainWindow 统一重勾两份查看方式菜单 + 切换按钮图标
    void viewModeChanged(int mode);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    // 焦点变化必须重绘:多选落点的焦点线按 hasFocus() 画,不重绘就会留过期指示器
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

    friend class FileCanvas;

private:
    // 布局
    void updateLayout();
    void rebuildGeometry();   // 全量算好每张卡片的矩形(仅结构变化时,滚动不再碰它)
    void ensureGeometry();    // m_geomDirty 时补一次重建(绘制/命中前兜底)
    bool m_geomDirty = false;
    void applyFilter();     // 按 m_filterMode 从 m_allEntries 生成 m_entries
    // 异步目录装载(#6):枚举+递归+嗅探在池线程,完成后 GUI 侧应用
    void onDirScanDone(quint64 gen, const QString& dirPath, bool sameDir,
                       const QSet<QString>& prevPaths,
                       std::vector<FileEntry> scanned, bool allowHdr);
    quint64 m_loadGen = 0;          // 装载代次:换目录即作废在途扫描
    QString m_pendingSelectPath;    // 目录装载期间来的选中请求(启动恢复),就绪后兑现
    bool    m_dirScanInFlight = false;
    bool    m_firstThumbLogged = true;  // 本次装载首图打点开关(onDirScanDone 重置)
    int  colsForWidth(int w) const;
    int  cardH(int idx) const;// 卡片高度(瀑布流按宽高比)

    // 自绘:整个列表只有 FileCanvas 一个控件
    void  paintCanvas(QPainter& p, const QRect& clip);
    void  paintCard(QPainter& p, int idx, const QRect& rect);
    void  paintDetailsRow(QPainter& p, int idx, const QRect& r);   // #267 详细列表行
    // 详细列表列宽:真源=m_dynColW(基准死表或用户拖拽落下的值,ini 记忆)
    int  detailColW(int i) const;
    void setDetailColWidth(int i, int w);   // 表头拖边界回调:落宽+记忆+重推                    // 第 i 列宽(隐藏=0)
    int  detailColX(const QRect& r, int i) const;    // 第 i 列左缘(自右向左锚定)
    void updateDetailColumns();    // 把 lead/尾垫片+当前列宽推给表头(进入详细态/缩放/列配置变化)
    int  m_dynColW[6] = { 72, 96, 56, 112, 112, 112 };   // 动态列宽缓存(基准=死表)
    void  refreshView();                    // 数据/外观变化后重绘(取代"重排卡片")
    int   indexAt(const QPoint& canvasPos);         // 画布坐标 → 条目序号;-1=空白(命中前补建几何)
    QRect cardRect(int idx) const;
    QString tipFor(int idx) const;          // 悬停提示(按需生成,不再逐卡片预建)

    // 画布事件转发
    void onCanvasPressStart(const QPoint& pos);   // 拖出起点(#81)
    bool maybeStartDrag(const QPoint& pos);       // 移动够距离才起拖;已起拖返回 true
    void onCanvasRelease(int idx);
    void onCanvasDblClick(int idx);
    void onCanvasMiddle(int idx);
    void onCanvasMenu(int idx, const QPoint& globalPos);
    // #268 框选:光标从空白处按住拖出一个矩形,松开即选中框内全部条目。
    // 单个空白点击(框退化为点) = 取消选择 —— 与"点击右上角空白清空选中"同义
    void beginRubber(const QPoint& pos);
    void updateRubber(const QPoint& pos);
    void endRubber(const QPoint& pos);
    bool m_rubberActive = false;   // 框选进行中
    QPoint m_rubberStart, m_rubberCur;
    void setHovered(int idx);
    void onThumbReady(const QString& filePath, const QImage& img);
    void requestVisibleThumbs();  // 可见行缩略图入队(滚动停止/尺寸稳定后)
    void requestAllThumbs();      // Thumbs/wholeFolder:整目录缩略图入队(不限视口)

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
    // 双击已定案,它收尾的那次"松开左键"不再参与选择(2026-09-10 用户报:
    // 双击进入子文件夹 B 后,左上角第一个先被选中,随后又被补选成"光标位置那一个")。
    // 成因:双击处理里 navigateTo 同步换好了新目录,而双击的 release 被这次
    // 扫描/布局堵在事件队列里、晚几十毫秒才处理——那时 indexAt() 已经按新目录
    // 算坐标,命中项恰好又等于旧目录里被双击那一项的索引,于是覆盖掉 selInit 的选中。
    bool     m_swallowNextRelease = false;
    QLineEdit* m_renameEdit = nullptr; // 就地改名编辑器(存在时表示正在改名)
    int        m_renameIdx  = -1;
    QString    m_renamePath;
    bool   m_lastByExt    = true;  // FileList/recognizeByExt 上次已应用值
    int    m_lastScanHeader = 0;   // FileList/scanHeader 上次已应用值
    int    m_cardSize     = 160;
    int    m_cardSizeAuto = 160;   // 自动模式的卡片尺寸(slider 值;固定列数时按宽度重算)
    int    m_lastCustomW  = 96;    // 上次看到的 Appearance/customThumbW(仅值变化才改尺寸)
    int    m_cols         = 0;
    int    m_fixedCols    = 0;
    bool   m_layoutReady  = false;   // #216:false=构造期,updateLayout 只标脏不真算;showEvent 放行
    int    m_viewMode     = VM_THUMBS_NAME;
    SortHeader* m_header  = nullptr;   // #267:排序表头(详细态列布局同源驱动)
    int    m_waterfallColW = 220;   // 瀑布流列宽
    int    m_sortCol      = SORT_NAME;   // 构造函数会按 Browser/startupSort 重设(#150)
    bool   m_sortAsc      = true;
    int    m_nameOrder    = NameNatural;
    int    m_filterMode   = FILTER_ALL;
    // #242 第二层筛选:条件 + 范围(0本层 1递归 2全局标记库)
    MultiFilterSpec m_mf;
    int    m_mfScope      = 0;
    bool   mfMatch(const FileEntry& e) const;   // 单条目判命中(类型维含目录行的豁免在调用侧)
    void   refilterForMulti();                  // 条件/范围变化后的重筛收尾(对齐 setFilterMode 尾段)
    QHash<QString, int> m_colorLabels;  // path → 颜色标记(目录加载时批量读入)
    // 文件列表规则(FileList/*;设置改动时刷新,逐条目路径不再读 ini)
    bool m_showHidden  = true;
    // 文件夹在排序结果中的位置:0=置顶(默认,目录恒在前) 1=参与排序(与文件按列混排)
    // 2=置底(目录恒在最后)。2026-09-09 用户令三态;旧 FileList/mixSort 布尔迁移为 0/1
    int  m_folderSortPos = 0;
    bool m_folderAlpha = true;    // 文件夹总是按字母序排列
    bool m_showSubFolders = false;// FileList/showSubFolders 递归展开子文件夹文件

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

    // EXIF 拍摄日期:缓存真源(0=已知无 EXIF 不重试);排序路径可同步读盘,
    // 绘制路径只查缓存(零 IO 铁律),缺失项由 exifPrefillVisible 后台补齐
    double exifDateOf(const FileEntry& e);   // 拍摄日期(缓存命中否则读盘,mtime 回退)
    void   exifPrefillVisible();             // 视口内缺失项后台预读,到达定点重绘
    QHash<QString, double> m_exifCache;
    QSet<QString>          m_exifPending;    // 已排队读取(防重复排)
    quint64                m_exifGen = 0;    // 轮次;过期回调只清 pending 不重绘

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
