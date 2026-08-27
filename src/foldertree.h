#pragma once
#include <QTreeWidget>
#include <QProxyStyle>
#include <QIcon>
#include <QPainter>
#include <QMouseEvent>

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

signals:
    void folderSelected(const QString& path);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void makeIcons();

    QIcon m_folderIcon;
    QIcon m_folderIconDim;   // 隐藏文件夹：半透明弱化图标
    QIcon m_driveIcon;
    QIcon m_desktopIcon;
    bool  m_showDesktop = true;   // Browser/showDesktopInTree 当前已应用值
    bool  m_showDesktop = true;   // Browser/showDesktopInTree 当前已应用值
};
