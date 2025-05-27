#pragma once
#include <QString>
#include <QColor>
#include <QHash>

class QSqlDatabase;

// 颜色标记存取(SQLite labels 表,共用 thumbnails.db;UI 线程专用连接)
// 颜色编号:0=无 1=红 2=橙 3=黄 4=绿 5=蓝
class LabelStore {
public:
    static LabelStore& instance();

    void        setColor(const QString& path, int color);
    int         colorFor(const QString& path);            // 无记录返回 0
    QHash<QString,int> colorsForDir(const QString& dir);  // 目录前缀批量加载
    void        removePaths(const QStringList& paths);    // 文件删除时清理

    static QColor colorValue(int color);

private:
    LabelStore();
    QSqlDatabase db();
};
