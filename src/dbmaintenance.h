#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QHash>
#include <QSet>
#include <QStringList>

// 缩略图数据库维护(仿 XnView):按媒体文件路径分组统计五列
// (目录/文件数/标记/缩略图/体积);支持按目录删除/同步清失效/重新定位/导出清单。
class DbMaintenanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit DbMaintenanceDialog(QWidget* parent = nullptr);

private:
    struct DirStat {
        QSet<QString> files;  // 媒体文件路径(thumbs ∪ labels,去重)
        int    thumbs = 0;    // 缩略图条目数(同文件多档位各计一条)
        int    labels = 0;    // 颜色标记数
        qint64 bytes  = 0;    // 缩略图字节合计
    };
    void reload();
    QStringList selectedDirs() const;
    void deleteSelected();
    void syncSelected();
    void relocateSelected();
    void exportCsv();
    void deleteAll();
    void rebuildThumbs();

    QTableWidget* m_table   = nullptr;
    QLabel*       m_summary = nullptr;
    QHash<QString, DirStat> m_byDir;
};

// 文件夹大小缓存维护(2026-09-05 用户令:工具菜单里贴着"缩略图数据库维护"):
// dirsize 表(path/size/basis/computed)是文件夹大小统计的落库缓存 ——
// 列出每条:目录路径 / 缓存大小 / 记账时间 / 失效键;支持删除选中、清空、
// 同步(移除目录已不存在的条目)。
class DirSizeMaintenanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit DirSizeMaintenanceDialog(QWidget* parent = nullptr);

private:
    void reload();
    QStringList selectedPaths() const;

    QTableWidget* m_table   = nullptr;
    QLabel*       m_summary = nullptr;
};
