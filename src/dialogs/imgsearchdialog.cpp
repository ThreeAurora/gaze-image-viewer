#include "imgsearchdialog.h"
#include "imgsearch.h"
#include "constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFileInfo>
#include <QPixmap>
#include <QToolTip>
#include <QThreadPool>
#include <QMetaObject>
#include <QCursor>
#include <QPointer>
#include <QPointer>

ImageSearchDialog::ImageSearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("以文搜图 - 万象图搜"));
    resize(720, 560);
    setStyleSheet(
        "QDialog{background:" C_WIN_BG ";}"
        "QLabel{color:#FFFFFF;background:transparent;}"
        "QLineEdit{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:6px 10px;}"
        "QPushButton{background:" C_TOOLBAR ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:6px 20px;}"
        "QPushButton:hover{border-color:" C_ACCENT ";}"
        "QListWidget{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "outline:none;}"
        "QListWidget::item{padding:4px 6px;}"
        "QListWidget::item:selected{background:" C_ACCENT ";}");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(8);

    auto* top = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText(QString::fromUtf8(
        "输入自然语言、文件名或图片中的文字,如:海边的日落 / IMG_2022 / 发票"));
    m_searchBtn = new QPushButton(QString::fromUtf8("搜索"));
    top->addWidget(m_input, 1);
    top->addWidget(m_searchBtn);
    root->addLayout(top);

    m_status = new QLabel(QString::fromUtf8("首次使用请先确认服务已启动(自动拉起)"));
    m_status->setStyleSheet("color:#B8B8B8;");
    root->addWidget(m_status);

    m_list = new QListWidget;
    m_list->setIconSize(QSize(72, 72));
    m_list->setUniformItemSizes(false);
    m_list->setWordWrap(true);
    root->addWidget(m_list, 1);

    connect(m_searchBtn, &QPushButton::clicked, this, &ImageSearchDialog::doSearch);
    connect(m_input, &QLineEdit::returnPressed, this, &ImageSearchDialog::doSearch);

    // 双击结果 → 在 Gaze 主窗口中定位显示
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const QString path = it->data(Qt::UserRole).toString();
        if (path.isEmpty()) return;
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfSlot("revealFile(QString)") < 0)
            mw = mw->parent();
        if (mw) {
            QMetaObject::invokeMethod(mw, "revealFile", Q_ARG(QString, path));
            accept();
        } else {
            QToolTip::showText(QCursor::pos(), QString::fromUtf8("无法定位主窗口"));
        }
    });

    m_input->setFocus();
}

void ImageSearchDialog::doSearch() {
    const QString q = m_input->text().trimmed();
    if (q.isEmpty() || m_running) return;

    m_running = true;
    m_searchBtn->setEnabled(false);
    m_list->clear();
    m_status->setText(QString::fromUtf8("检查/启动服务..."));
    QCoreApplication::processEvents();

    const QString err = ImgSearch::ensureRunning();
    if (!err.isEmpty()) {
        m_status->setText(err);
        m_running = false;
        m_searchBtn->setEnabled(true);
        return;
    }

    m_status->setText(QString::fromUtf8("搜索中:%1").arg(q));
    QCoreApplication::processEvents();

    QString searchErr;
    const QJsonDocument doc = ImgSearch::search(q, &searchErr);
    if (!searchErr.isEmpty()) {
        m_status->setText(searchErr);
        m_running = false;
        m_searchBtn->setEnabled(true);
        return;
    }

    const QJsonArray results = doc.object().value("results").toArray();
    if (results.isEmpty()) {
        m_status->setText(QString::fromUtf8("没有匹配结果"));
    } else {
        m_status->setText(QString::fromUtf8("共 %1 项(双击在 Gaze 中打开)")
                              .arg(doc.object().value("total").toInt()));
    }

    // 文字行先行显示,缩略图逐项异步拉取
    for (const auto& v : results) {
        const QJsonObject o = v.toObject();
        const int id = o.value("id").toInt();
        const QString path = o.value("path").toString();
        QFileInfo fi(path);
        // 来源标签:name/ocr/sem
        QStringList srcs;
        for (const auto& s : o.value("sources").toArray()) {
            const QString t = s.toObject().value("type").toString();
            if (t == "name") srcs << QString::fromUtf8("名称");
            else if (t == "ocr") srcs << QString::fromUtf8("文字");
            else if (t == "sem") srcs << QString::fromUtf8("语义");
        }
        auto* it = new QListWidgetItem(
            QString::fromUtf8("[%1] %2").arg(srcs.join('+'), fi.fileName()));
        it->setData(Qt::UserRole, path);
        it->setToolTip(path);
        m_list->addItem(it);
        loadThumbFor(m_list->count() - 1, id);
    }

    m_running = false;
    m_searchBtn->setEnabled(true);
}

void ImageSearchDialog::loadThumbFor(int row, int imageId) {
    // 后台线程拉取,回 GUI 线程设置图标(对话框已关或行已不存在则丢弃)
    QPointer<ImageSearchDialog> self(this);
    QThreadPool::globalInstance()->start([self, row, imageId]() {
        const QByteArray data = ImgSearch::thumbBytes(imageId);
        if (data.isEmpty()) return;
        QPixmap pm;
        pm.loadFromData(data);
        if (pm.isNull()) return;
        QMetaObject::invokeMethod(self, [self, row, pm]() {
            if (!self || !self->m_list) return;
            if (row >= self->m_list->count()) return;
            QListWidgetItem* it = self->m_list->item(row);
            if (it && it->icon().isNull()) it->setIcon(QIcon(pm));
        }, Qt::QueuedConnection);
    });
}
