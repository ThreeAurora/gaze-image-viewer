#pragma once
#include <QScrollArea>
#include <QWidget>
#include <QTimer>
#include <QHash>
#include <QSet>
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
    void setCardSize(int size);
    void sort(int column, bool ascending);
    void toggleFilter();
    void navigateSelection(int delta);
    void selectIndex(int idx);   // 单选指定项并滚动到可见(滚动联动用)

    int     fileCount()     const;
    int     selectedCount() const;
    int64_t selectedSize()  const;
    QStringList selectedPaths() const;

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
    int  colsForWidth(int w) const;

    // 卡片管理
    FileCard* acquireCard();
    void      recycleCards();
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
    int    m_cardSize     = 160;
    int    m_cols         = 0;
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

    // 缩略图
    QHash<QString, QPixmap> m_thumbCache;

    QTimer m_resizeTimer;
    bool   m_loading = false;

    static constexpr int SPACING = 6;
    static constexpr int MARGIN  = 8;
};
