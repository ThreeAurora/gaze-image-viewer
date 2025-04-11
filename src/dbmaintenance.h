#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QLabel>

// 缩略图数据库维护:统计/筛选/删除/重建(批次 6)
class DbMaintenanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit DbMaintenanceDialog(QWidget* parent = nullptr);

private:
    void reload();          // 重新统计并填表
    void deleteAll();       // 清空 thumbs 表
    void rebuildThumbs();   // 重建(清空后下次浏览自动重建)

    QTableWidget* m_table;
    QLabel*       m_summary;
    QStringList   m_allPaths;          // 全部 path(聚合用)
    QHash<QString, QPair<int, qint64>> m_byDir; // 目录 → (文件数, 字节)
};
