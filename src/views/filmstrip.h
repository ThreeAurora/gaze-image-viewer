#pragma once
#include <QListView>
#include <QAbstractListModel>
#include <QCache>
#include <QHash>
#include <QSet>
#include <QStringList>

class QLabel;
class QWheelEvent;
class QMouseEvent;

// ═══════════════════════════════════════════
// #203 全屏胶片条(2026-09-03 用户令:全面重做交互) —— G 全屏预览顶部浮层
//
// 旧实现:以当前文件为中心 ±8 张 QLabel,每拍全量销毁重建,永远只能看到
// 临近 17 张。新实现:QListView 虚拟化,数据源 = FileGrid 当前目录的完整
// 显示列表 —— 整个目录随便滚,滚到哪缩略图加载到哪(可见区 → Thumbnailer)。
//
// 交互总则(数据可回溯,行为要与 XnView 的 filmstrip 观感对齐):
//   滚轮     = 横向滚动条目(不再"滚轮=切文件";想切点图)
//   按住拖动 = 平移,越过 6px 阈值判为拖 —— 松手不吃点击
//   单击     = 跳到那张(jumpRequested → MainWindow::selectByPath 同一条路)
//   当前项   = 蓝框 #0078D7;其余描边;悬停亮描边
//   底部     = 题注"文件名 · i / n"
// #220(2026-09-04 用户令):条加高;右端只留"退出全屏"一键(上一个/下一个
// 改挂屏幕左右浮动钮,见 MainWindow::m_fullNavPrev/Next);当前项强制居中,
// 首尾张允许滚出边界外留白(不再贴边钳制,见 centerRow/updateGeometries)。
// 缩略图缓存 QCache LRU(上限 2000 张),跨目录来回滚不重解。
// #225/#226(2026-09-04 用户令):数据源改灌目录全部文件(FileGrid::
// allFilePaths,目录行除外,不再跟随网格筛选);出不了缩略图的格子
// (音频/文本/可执行/RAW…)由 delegate 直接画文件名;右端加 图片/视频/音频
// 三个勾选钮(勾哪类显示哪类,ini 持久化),其余类型没有按钮管、始终显示。
// ═══════════════════════════════════════════

class FilmStripModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum { ThumbRole = Qt::UserRole + 1 };
    explicit FilmStripModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
        m_thumbs.setMaxCost(2000);
    }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_paths.size();
    }
    QVariant data(const QModelIndex& idx, int role) const override;
    void reset(const QStringList& paths);
    void setThumb(const QString& path, const QImage& img);
    bool hasThumb(const QString& path) const { return m_thumbs.contains(path); }
    QString pathAt(int row) const {
        return (row >= 0 && row < m_paths.size()) ? m_paths.at(row) : QString();
    }
private:
    QStringList m_paths;
    QHash<QString, int> m_rowOf;
    mutable QCache<QString, QImage> m_thumbs;   // object() 会更新 LRU,故 mutable
};

class FilmStrip : public QListView {
    Q_OBJECT
public:
    explicit FilmStrip(QWidget* parent = nullptr);
    static int preferredHeight();                // 6 + 缩略图 64 + 题注行 20
    void setEntries(const QStringList& paths, const QString& current);
    void syncCurrent(const QString& path);       // 蓝框/居中/题注跟随,不动数据
    int  count() const { return m_model->rowCount(); }
    QString currentPath() const { return m_model->pathAt(m_currentRow); }
    int  currentRow() const { return m_currentRow; }   // delegate 画蓝框用
    int  hoverRow()   const { return m_hoverRow; }     // delegate 画悬停描边用

signals:
    void jumpRequested(const QString& path);
    void exitRequested();          // 退出全屏(右端唯一按钮,#220 起条上只留这一个)

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void updateGeometries() override;   // #220:拉宽滚动范围,首尾张允许居中

private:
    void refilter();                     // #226:按类别勾选从全量表重建显示列表
    void locateCurrent();                // 在显示列表里定位当前文件(找不到=收蓝框)
    void requestVisibleThumbs();
    void applyCurrent(int row, bool center);
    void centerRow(int row);                     // #220:手工算滚动值,首尾也真居中
    void updateCaption();

    FilmStripModel* m_model;
    QLabel* m_caption = nullptr;
    QWidget* m_btnBar = nullptr;                 // 右端按钮区(#226:三类勾选钮+退出)
    int     m_currentRow = -1;
    int     m_hoverRow   = -1;
    QPoint  m_pressPos;
    int     m_pressRow   = -1;
    bool    m_panning    = false;
    QSet<QString> m_requested;                   // 已 enqueue 的路径(防重复入队)
    // #225/#226:全量数据源与当前文件记账;类别勾选(ini FilmStrip/show*)
    QStringList m_allPaths;
    QString     m_currentPath;
    bool        m_showImg = true;
    bool        m_showVid = true;
    bool        m_showAud = true;
};
