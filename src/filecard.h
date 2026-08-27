#pragma once
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include "fileentry.h"
#include "filegrid.h"

class FileCard : public QFrame {
    Q_OBJECT
public:
    explicit FileCard(QWidget* parent = nullptr);

    void setup(const FileEntry& entry, int size, int viewMode, int height = 0);
    void setThumbnail(const QPixmap& pixmap);
    void setCover(bool on) { m_cover = on; }   // 瀑布流:缩略图按比例填满(cover)
    void setSelected(bool sel, bool multi = false);
    void setMarked(bool marked);
    void setColorLabel(int color);   // 颜色标记:0无 1红 2橙 3黄 4绿 5蓝
    void refreshLabelBg();           // 标签颜色设置变更后重涂文件名底色
    void deactivate();

    QString filePath()     const { return m_filePath; }
    bool    isActive()     const { return m_active; }
    bool    isLivePhoto()  const { return m_isLive; }

    void detectLivePhoto();

    // 外观设置缓存:FileGrid 启动时 + 设置变更后刷新。setup()/paintEvent() 只读
    // 内存缓存 —— 逐条目路径读 ini 违反项目铁律。
    static void applyAppearance();

    // 外观设置缓存:FileGrid 启动时 + 设置变更后刷新。setup()/paintEvent() 只读
    // 内存缓存 —— 逐条目路径读 ini 违反项目铁律。
    static void applyAppearance();
    static bool sizeBytesMode() { return s_sizeBytes; }   // FileList/sizeInBytes

signals:
    void clicked(FileCard* card);
    void doubleClicked(FileCard* card);
    void middleClicked(FileCard* card);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void applyLabelBg();

    int     m_cardSize = 160;
    QString m_filePath;
    bool    m_active   = false;
    bool    m_selected = false;
    bool    m_hovered  = false;
    bool    m_marked   = false;
    int     m_colorLabel = 0;
    bool    m_cover    = false;   // true=缩略图 cover 填满(瀑布流)
    bool    m_isLive   = false;
    bool    m_isDir    = false;
    bool    m_hidden   = false;   // 隐藏文件/文件夹：名称显示淡灰
    QRect   m_thumbRect;              // 缩略图实际显示区域(选中框贴此绘制)
    QString m_selColor = "#3B82F6";
    QString m_nameBg   = "#26262B";
    QString m_appliedBg;      // 已应用到 nameLabel 的底色(防重复 setStyleSheet)

    QLabel* m_thumbLabel = nullptr;
    QLabel* m_nameLabel  = nullptr;
    QLabel* m_detailLabel = nullptr;   // 第二行/详细列(大小 日期)
    QLabel* m_liveBadge  = nullptr;
    QLabel* m_starLabel  = nullptr;
    QLabel* m_starLabel  = nullptr;

    static int  s_border;        // Appearance/borderSize 卡片边框粗细(0=无)
    static int  s_imageAlign;    // Appearance/imageAlign 0左 1中 2右
    static int  s_labelAlign;    // Appearance/labelAlign  0左 1中 2右
    static int  s_labelGap;      // Appearance/labelSpacing 真=6px 假=0
    static bool s_showRating;    // Browser/showRating 颜色标记圈
    static bool s_sizeBytes;     // FileList/sizeInBytes 按字节显示大小

    friend class FileGrid;
};
