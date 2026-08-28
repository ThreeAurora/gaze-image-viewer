#pragma once
#include <QMenu>
#include <QVariantMap>

class FileCanvas;

class FileContextMenu : public QMenu {
    Q_OBJECT
public:
    // 自绘画布上的第 index 个条目(选中集合取自其所属 FileGrid)
    explicit FileContextMenu(FileCanvas* canvas, int index, QWidget* parent = nullptr);

private:
    void extractFrames(const QString& videoPath);

    QString     m_filePath;
    bool        m_isLive   = false;
    QVariantMap m_liveInfo;
};
