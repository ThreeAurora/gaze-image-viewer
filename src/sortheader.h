#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>

// 排序字段 ID（保持与 Python 一致）
enum SortCol {
    SORT_NAME    = 0,
    SORT_SIZE    = 1,
    SORT_TYPE    = 2,
    SORT_EXT     = 3,
    SORT_CDATE   = 4,
    SORT_MDATE   = 5,
    SORT_EXIF    = 6,
    SORT_PATH    = 7,
    SORT_MARKED  = 8,
    SORT_WIDTH   = 9,
    SORT_HEIGHT  = 10,
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
