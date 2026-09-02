#pragma once
#include <QMenu>
#include <QVariantMap>

class FileGrid;

class FileContextMenu : public QMenu {
    Q_OBJECT
public:
    // grid 当前列表里的第 index 个条目;多选集合取自同一 FileGrid
    explicit FileContextMenu(FileGrid* grid, int index, QWidget* parent = nullptr);

private:
    void extractFrames(const QString& videoPath);

    QString     m_filePath;
    bool        m_isLive   = false;
    QVariantMap m_liveInfo;
};
