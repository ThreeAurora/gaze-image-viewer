#pragma once
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include "fileentry.h"

class FileCard : public QFrame {
    Q_OBJECT
public:
    explicit FileCard(QWidget* parent = nullptr);

    void setup(const FileEntry& entry, int cardSize);
    void setThumbnail(const QPixmap& pixmap);
    void setSelected(bool sel, bool multi = false);
    void setMarked(bool marked);
    void deactivate();

    QString filePath()     const { return m_filePath; }
    bool    isActive()     const { return m_active; }
    bool    isLivePhoto()  const { return m_isLive; }

    void detectLivePhoto();

signals:
    void clicked(FileCard* card);
    void doubleClicked(FileCard* card);

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
    bool    m_marked   = false;
    bool    m_isLive   = false;
    QString m_selColor = "#0078D7";
    QString m_nameBg   = "#1A1A1A";

    QLabel* m_thumbLabel = nullptr;
    QLabel* m_nameLabel  = nullptr;
    QLabel* m_liveBadge  = nullptr;
    QLabel* m_starLabel  = nullptr;
    QLabel* m_starLabel  = nullptr;

    static int  s_border;        // Appearance/borderSize 卡片边框粗细(0=无)
    static int  s_imageAlign;    // Appearance/imageAlign 0左 1中 2右
    static int  s_labelAlign;    // Appearance/labelAlign  0左 1中 2右
    static int  s_labelGap;      // Appearance/labelSpacing 真=2px 假=0
    static bool s_showRating;    // Browser/showRating 颜色标记圈

    friend class FileGrid;
};
