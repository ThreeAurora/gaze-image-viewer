#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QHash>

// 以文搜图对话框:调用本地万象图搜(imgseek)服务,三路融合检索
// (文件名/OCR 文字/CLIP 语义);双击结果在 Gaze 中定位显示
class ImageSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageSearchDialog(QWidget* parent = nullptr);

private:
    void doSearch();
    void loadThumbFor(int row, int imageId);

    QLineEdit*    m_input;
    QPushButton*  m_searchBtn;
    QLabel*       m_status;
    QListWidget*  m_list;
    bool          m_running = false;
};
