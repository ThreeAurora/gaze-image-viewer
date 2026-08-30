#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>

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
    SORT_RATING    = 17,  // 评级
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

signals:
    void sortChanged(int column, bool ascending);

private:
    void onColumnClicked(int colId);
    void updateArrows();

    int m_currentCol = SORT_MDATE;
    bool m_ascending = false;

    QHBoxLayout* m_layout = nullptr;
    struct ColInfo {
        int id;
        QPushButton* btn;
    };
    QList<ColInfo> m_columns;
};
