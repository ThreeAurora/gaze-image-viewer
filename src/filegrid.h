#pragma once
#include <QScrollArea>
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <vector>
#include "fileentry.h"
#include "sortheader.h"

class FileCard;
class SortHeader;

// 筛选模式(查看菜单/工具栏)
enum FilterMode {
    FILTER_ALL = 0,
    FILTER_IMAGES, FILTER_IMAGES_DIRS,
    FILTER_VIDEOS, FILTER_VIDEOS_DIRS,
    FILTER_AUDIO, FILTER_ARCHIVES,
    FILTER_MARKED, FILTER_UNMARKED,
    FILTER_RED, FILTER_ORANGE, FILTER_YELLOW, FILTER_GREEN, FILTER_BLUE,
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

class FileGrid : public QScrollArea {
    Q_OBJECT
public:
    explicit FileGrid(QWidget* parent = nullptr);

    void loadDirectory(const QString& dirPath);
    void refreshCurrentDir();    // 重新加载当前目录(文件操作后)
    void setCardSize(int size);
    void setFixedCols(int n);    // n=0 自动;1-16 手动列数(缩放时缩略图贴边缩放但列数不变)
    int  fixedCols() const { return m_fixedCols; }
    void setViewMode(int mode);  // ViewMode
    int  viewMode() const { return m_viewMode; }
    void sort(int column, bool ascending);
    void toggleFilter();
    void setFilterMode(int mode);            // FilterMode
    int  filterMode() const { return m_filterMode; }
    void navigateSelection(int delta);
    void selectIndex(int idx, bool scrollToVisible = true);  // 滚动联动时传 false 防反馈回路
    void scrollToRow(int idx);  // 首排贴顶/末排贴底/其余可见不动的定位规则
    bool selectByPath(const QString& path);  // 按路径选中(最近文件定位用)
    QString neighborOf(const QString& path, int delta) const;  // 相邻文件路径(预读用)

    // 选择扩展(编辑菜单)
    void selectAllEntries();
    void selectInvert();
    enum SelectKind { KindMarked, KindFiles, KindDirs, KindImages, KindVideos, KindAudio };
    void selectByKind(int kind);

    // 颜色标记:对当前选中(单选时该项;多选时全部)设置
    void applyColorLabelToSelection(int color);
    int  firstSelectedIndex() const;

    int     fileCount()     const;
    int     selectedCount() const;
    int64_t selectedSize()  const;
    QStringList selectedPaths() const;
    int  currentSortCol() const { return m_sortCol; }
    bool sortAscending()  const { return m_sortAsc; }
    int  cardSizeValue()  const { return m_cardSizeAuto; }

signals:
    void fileCountChanged();
    void selectionChanged(const QString& currentPath);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // 布局
    void updateLayout();
    void layoutCards();
    void layoutRows();        // 列表/详细信息:单列全宽行
    void layoutWaterfall();   // 瀑布流:按宽高比放最短列
    void applyFilter();     // 按 m_filterMode 从 m_allEntries 生成 m_entries
    int  colsForWidth(int w) const;
    int  cardW() const;       // 卡片宽度(按查看方式)
    int  cardH(int idx) const;// 卡片高度(瀑布流按宽高比)

    // 卡片管理
    FileCard* acquireCard();
    void      recycleCards();
    void      recycleInvisible(const QSet<int>& needed);
    QWidget*  m_canvas = nullptr;

    // 事件处理
    void onCardClicked(FileCard* card);
    void onCardDoubleClicked(FileCard* card);
    void onThumbReady(const QString& filePath, const QImage& img);
    void beginInlineRename();     // FileOps/renameDialog=关:就地改名
    void endInlineRename(bool commit);
    // FileOps/renameDialog=关 时的就地改名(画布上压一个编辑器)
    void beginInlineRename();
    void endInlineRename(bool commit);

    // 键盘操作
    void toggleMark(int index);
    void clearAllMarks();
    void deleteFile(int index);
    void newFolder();

    // 数据
    std::vector<FileEntry> m_allEntries;
    std::vector<FileEntry> m_entries;    // 筛选/排序后的显示列表
    QString m_currentDir;                // 当前加载的目录
    int    m_cardSize     = 160;
    int    m_cardSizeAuto = 160;   // 自动模式的卡片尺寸(slider 值;固定列数时按宽度重算)
    int    m_cols         = 0;
    int    m_fixedCols    = 0;
    int    m_viewMode     = VIEW_THUMBS_NAME;
    int    m_waterfallColW = 220;   // 瀑布流列宽
    int    m_viewMode     = VM_THUMBS_NAME;
    int    m_waterfallColW = 220;   // 瀑布流列宽
    int    m_sortCol      = SORT_MDATE;
    bool   m_sortAsc      = false;




    int    m_filterMode   = FILTER_ALL;
    QHash<QString, int> m_colorLabels;  // path → 颜色标记(目录加载时批量读入)

    // 选择状态
    QSet<int>            m_selected;
    int                  m_lastClicked = -1;
    QSet<QString>        m_marked;

    // 对象池
    std::vector<FileCard*> m_pool;
    QHash<int, FileCard*>  m_active; // row-major index → card

    // 缩略图(FIFO 上限防内存无限膨胀:800 条 × ~300px pixmap ≈ 260MB 封顶)
    QHash<QString, QPixmap> m_thumbCache;
    QQueue<QString>         m_thumbOrder;

    QTimer m_resizeTimer;
    QTimer m_reEnqueueTimer;  // 尺寸停止变化后重新生成高清缩略图(防抖)
    QTimer m_scrollTimer;     // 滚轮防抖:滚动中推迟缩略图提交,停止后批量补齐
    bool   m_scrollSettled = true;
    bool   m_loading = false;

    static constexpr int SPACING = 6;
    static constexpr int MARGIN  = 8;
};
