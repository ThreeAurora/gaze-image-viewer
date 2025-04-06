#pragma once
#include <QMenu>
#include <QVariantMap>

class FileCard;

class FileContextMenu : public QMenu {
    Q_OBJECT
public:
    explicit FileContextMenu(FileCard* card, QWidget* parent = nullptr);

private:
    void extractFrames(const QString& videoPath);

    QString     m_filePath;
    bool        m_isLive   = false;
    QVariantMap m_liveInfo;
};
