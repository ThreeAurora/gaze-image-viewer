#pragma once
#include <QString>
#include <QColor>
#include <QHash>
#include <QList>

class QSqlDatabase;

// 颜色标记存取(SQLite labels 表,共用 thumbnails.db;UI 线程专用连接)
// 颜色编号:0=无 1=红 2=橙 3=黄 4=绿 5=蓝
class LabelStore {
public:
    static LabelStore& instance();

    void        setColor(const QString& path, int color);
    int         colorFor(const QString& path);            // 无记录返回 0
    QHash<QString,int> colorsForDir(const QString& dir);  // 目录前缀批量加载
    QHash<QString,int> allColored();                      // 全表(分类筛选器"全局"范围的候选宇宙)
    void        removePaths(const QStringList& paths);    // 文件删除时清理

    static QColor colorValue(int color);

private:
    LabelStore();
    QSqlDatabase db();
};

// 格式标签颜色(文件名底色):ini "LabelColors/*" 持久化,
// 设置页"缩略图→标签颜色"可视化编辑(FileCard 渲染时查询)
class LabelColors {
public:
    static QColor colorForExt(const QString& extNoDot);   // 未命中 → fallback
    static QColor fallbackColor();                         // 未列格式的底色
    static void   setFallbackColor(const QColor& c);
    static QList<QPair<QString, QColor>> all();            // 覆盖列表(按 ext 排序)
    static void   set(const QString& extNoDot, const QColor& c);
    static void   remove(const QString& extNoDot);
    static bool   enabled();                               // 总开关 Appearance/formatColor
    static void   setEnabled(bool on);
    static void   reload();   // ini 被外部(设置页)修改后调用,下次查询重读
};
