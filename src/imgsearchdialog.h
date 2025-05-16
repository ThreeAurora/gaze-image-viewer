#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QImage>
#include <QJsonDocument>

// 以文搜图对话框:万象图搜(imgseek)的客户端。三路融合检索
// (文件名/OCR 文字/CLIP 语义),HTTP 全异步(旧版 QEventLoop 同步
// 等待会整窗冻结,已废);结果为缩略图网格,带来源+分数角标。
class ImageSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageSearchDialog(QWidget* parent = nullptr);

private:
    void doSearch();
    void onResults(int seq, int status, const QJsonDocument& doc);
    void resort();                    // 相关性(回服务端序)/名称/时间,本地重排
    void loadThumbFor(int imageId);   // 工作线程取图,回 GUI 按 id 贴(与行号无关)
    void applyIcon(int imageId, const QImage& img);
    qint64 mtimeOf(QListWidgetItem* it);

    QLineEdit*   m_input;
    QComboBox*   m_model;     // CLIP 模型(引擎默认/cn_clip_b16/clip_b32)
    QComboBox*   m_sort;      // 相关性(服务端序)/名称/时间(本地重排)
    QPushButton* m_searchBtn;
    QLabel*      m_status;
    QListWidget* m_list;
    bool         m_running = false;
    int          m_seq = 0;   // 代际号:过期回调直接丢弃
    int          m_total = 0; // 服务端报告的命中总数
};
