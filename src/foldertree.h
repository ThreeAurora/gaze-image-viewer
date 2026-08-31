#pragma once
#include <QTreeWidget>
#include <QProxyStyle>
#include <QIcon>
#include <QPainter>
#include <QMouseEvent>
#include <QStringList>
#include <QStringList>

// ── 自定义展开箭头 ──
class ArrowStyle : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override;
};

// ── 文件夹树 ──
class FolderTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit FolderTree(QWidget* parent = nullptr);

    void loadDrives();
    void loadChildren(QTreeWidgetItem* item);
    void focusPath(const QString& dirPath);

    // 树右键"显示子文件夹中的文件"的镜像状态:真源在 FileGrid(它才做递归扫描),
    // 这里只用于画 ✓,由主窗口在启动时灌入、此后跟随菜单开关同步。
    void setSubFoldersShown(bool on) { m_subFoldersShown = on; }

    // 拖放(#81):落点坐标 → 该行代表的目录路径(空白/非目录返回空)
    QString pathAt(const QPoint& pos) const;
    // 结构变化后刷新当前行(拖入复制完成后用)
    void refreshCurrent();

signals:
    void folderSelected(const QString& path);
    // 树内文件操作(新建/粘贴/删除/改名/复制到/移动到)造成的结构变化。
    // changedDirs=内容变了的目录(可多个),removed=已消失的原路径(未删则为空)。
    void foldersChanged(const QStringList& changedDirs, const QStringList& removed);
    // 改名单独发一条:当前目录正是被改名那一层时,应当跟着进新路径,
    // 而不是被 foldersChanged 的"被删→退回父目录"规则甩出去
    void folderRenamed(const QString& oldPath, const QString& newPath);
    void subFoldersToggled(bool on);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // #117:左键按住"扫过"文件夹 = 逐个切入(默认,FolderTree/leftDragSweep=0);
    //      取 1 时退回原来的"拖动多选",行为一个字节都不改
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // #117:左键按住"扫过"文件夹 = 逐个切入(默认,FolderTree/leftDragSweep=0);
    //      取 1 时退回原来的"拖动多选",行为一个字节都不改
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void makeIcons();

    // ── 右键菜单 ──
    void showContextMenu(const QPoint& pos);
    static QString pathOf(const QTreeWidgetItem* item);   // UserRole 里的目录路径
    static bool    isVolumeRoot(const QString& path);     // 盘符根:禁止剪切/删除/改名
    QTreeWidgetItem* itemForPath(const QString& path);
    QStringList selectedPaths() const;                    // 当前操作对象(资源管理器语义)
    void refreshNode(const QString& dirPath);             // 结构变化后同步该层
    void removeNodes(const QStringList& paths);           // 删除后摘掉节点(连子树)
    void newFolderInto(QTreeWidgetItem* base);
    void pasteInto(QTreeWidgetItem* base);
    void renameItem(QTreeWidgetItem* item);
    void reportErrors(const QStringList& errors, const QString& title);

    QIcon m_folderIcon;
    QIcon m_folderIconDim;   // 隐藏文件夹：半透明弱化图标
    QIcon m_driveIcon;
    QIcon m_desktopIcon;
    bool  m_showDesktop = true;   // Browser/showDesktopInTree 当前已应用值
    bool  m_subFoldersShown = false;  // FileGrid::showSubFolders 的镜像
    bool  m_sweepSwitch = true;   // FolderTree/leftDragSweep==0 → 扫过即切换
    QTreeWidgetItem* m_sweepCur = nullptr;  // 本次手势里最后切入的那一行
    bool  m_sweepSwitch = true;   // FolderTree/leftDragSweep==0 → 扫过即切换
    QTreeWidgetItem* m_sweepCur = nullptr;  // 本次手势里最后切入的那一行
    bool  m_showDesktop = true;   // Browser/showDesktopInTree 当前已应用值
};
