#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>

// 排序字段 ID（保持与 Python 一致;11+ 为查看菜单扩展字段）
enum SortCol {
    SORT_NAME    = 0,
    SORT_SIZE    = 1,
    SORT_TYPE    = 2,
    SORT_EXT     = 3,
    SORT_CDATE   = 4,
    SORT_MDATE   = 5,
    SORT_EXIF    = 6,
    SORT_PATH    = 7,
    SORT_WIDTH   = 9,
    SORT_HEIGHT  = 10,
    // ── 查看菜单扩展 ──
    SORT_EXIFMOD   = 11,  // EXIF 修改日期
    SORT_IMGSIZE   = 12,  // 图像大小(宽×高)
    SORT_RATIO     = 13,  // 图像比例
    SORT_ORIENTATION = 14,// 图像方向(横/竖)
    SORT_PRINTSIZE = 15,  // 打印尺寸
    SORT_COMMENT   = 16,  // 注释
    SORT_COLORLABEL= 18,  // 颜色标签
    SORT_CUSTOM    = 19,  // 自定义
};

class SortHeader : public QWidget {
    Q_OBJECT
public:
    explicit SortHeader(QWidget* parent = nullptr);

    void setColumnWidths(const QList<int>& widths);
    int  currentColumn() const { return m_currentCol; }
    bool ascending()    const { return m_ascending; }

    // ── #267 详细列表形态:lead 垫片 + 名称弹性 + 定宽列 + 尾垫片,与网格行
    // 同源对齐(定宽真源在此,FileGrid 绘制引用同一张表) ──
    static int detailColWidth(int i);        // 第 i 列(i=0 大小 … 5 EXIF)基准宽
    void setDetailMode(bool on);             // 结构切换:弹性均分 ↔ 定宽列布局
    void applySharedStretch();               // 非详细态:各列等权起宽
    void setSortIndicator(int colId, bool ascending);   // 程序侧排序后回填方向箭头
    bool detailMode() const { return m_detailMode; }
    void setDetailLead(int w);               // 头垫片宽(名称文字起点对齐)
    void setDetailTail(int w);               // 尾垫片宽(列右缘与网格行右缘对齐)
    void setDetailWidths(const int w[6]);    // 表头列钮同步动态宽(名称仍弹性)
    bool detailColumnVisible(int i) const;   // 第 i 定宽列是否显示(右键配置)

signals:
    void sortChanged(int column, bool ascending);
    void detailColumnsEdited();              // 列显隐/表头尺寸变化 → FileGrid 重推几何
    void columnWidthDragged(int index, int width);   // 用户拖列边界 → FileGrid 落宽并记忆

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void onColumnClicked(int colId);
    void updateArrows();

    int m_currentCol = SORT_MDATE;
    bool m_ascending = false;
    bool m_detailMode = false;

    QHBoxLayout* m_layout = nullptr;
    QWidget* m_leadSpacer = nullptr;   // 详细态:名称列前的对齐垫片
    QWidget* m_tailSpacer = nullptr;   // 详细态:列区右端的对齐垫片
    int m_leadW = 0;
    int m_tailW = 0;
    struct ColInfo {
        int id;
        QPushButton* btn;
        QLabel* arrow = nullptr;   // 排序方向小箭头(灰,独立于文字;2026-09-06 用户令)
    };
    QList<ColInfo> m_columns;
    int m_dragCol    = -1;   // 正在拖的列边界(-1=无)
    int m_dragPressX = 0;    // 按下点(钮内坐标)
    int m_dragStartW = 0;    // 拖动起始列宽

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};
