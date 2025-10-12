#include "imgsearchdialog.h"
#include "imgsearch.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QJsonObject>
#include <QJsonArray>
#include <QToolTip>
#include <QThreadPool>
#include <QMetaObject>
#include <QCursor>
#include <QProcess>
#include <algorithm>

namespace {
constexpr int kMaxShown  = 200;              // 显示上限(服务端可能给更多)
constexpr int kPathRole  = Qt::UserRole;     // 完整路径
constexpr int kIdRole    = Qt::UserRole + 1; // imgseek image id(取缩略图的键)
constexpr int kRankRole  = Qt::UserRole + 2; // 服务端返回序(=相关性)
constexpr int kMtimeRole = Qt::UserRole + 3; // lastModified 缓存(选"时间"时才统计)

QString sourceName(const QString& type) {
    if (type == "name") return QString::fromUtf8("名称");
    if (type == "ocr")  return QString::fromUtf8("文字");
    if (type == "sem")  return QString::fromUtf8("语义");
    return type;
}
QString scoreText(const QJsonObject& s, int prec) {
    if (!s.contains("score")) return QString();
    return QString::number(s.value("score").toDouble(), 'f', prec);
}
}

ImageSearchDialog::ImageSearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("以文搜图 - 万象图搜"));
    resize(880, 640);
    setStyleSheet(
        QString::fromUtf8("QDialog{background:%1;}"
        "QLabel{color:%2;background:transparent;}"
        "QLineEdit{background:%3;color:%2;border:1px solid %4;"
        "border-radius:3px;padding:6px 10px;}"
        "QComboBox{background:%3;color:%2;border:1px solid %4;"
        "border-radius:3px;padding:5px 8px;}"
        "QComboBox QAbstractItemView{background:%3;color:%2;"
        "selection-background-color:%6;}"
        "QPushButton{background:%5;color:%2;border:1px solid %4;"
        "border-radius:3px;padding:6px 20px;}"
        "QPushButton:hover{border-color:%6;}"
        "QPushButton:disabled{color:%7;}"
        "QListWidget{background:%3;color:%2;border:1px solid %4;"
        "outline:none;}"
        "QListWidget::item{padding:2px;}"
        "QListWidget::item:selected{background:%6;}")
        .arg(C_WIN_BG, C_TEXT, C_CONTENT, C_SEPARATOR, C_TOOLBAR, C_ACCENT,
             C_TEXT_FAINT));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(8);

    auto* top = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText(QString::fromUtf8(
        "输入自然语言、文件名或图片中的文字,如:海边的日落 / IMG_2022 / 发票"));
    m_model = new QComboBox;
    m_model->addItem(QString::fromUtf8("引擎默认"), QString());
    m_model->addItem(QStringLiteral("cn_clip_b16"), QStringLiteral("cn_clip_b16"));
    m_model->addItem(QStringLiteral("clip_b32"), QStringLiteral("clip_b32"));
    m_model->setToolTip(QString::fromUtf8("CLIP 语义模型;引擎默认由服务端自选"));
    m_sort = new QComboBox;
    m_sort->addItem(QString::fromUtf8("按相关性"), 0);
    m_sort->addItem(QString::fromUtf8("按名称"), 1);
    m_sort->addItem(QString::fromUtf8("按时间"), 2);
    m_sort->setToolTip(QString::fromUtf8(
        "名称/时间为本地重排(服务端只保证相关性顺序)"));
    m_searchBtn = new QPushButton(QString::fromUtf8("搜索"));
    top->addWidget(m_input, 1);
    top->addWidget(m_model);
    top->addWidget(m_sort);
    top->addWidget(m_searchBtn);
    root->addLayout(top);

    m_status = new QLabel(QString::fromUtf8("输入关键词后回车;服务未运行时会自动拉起"));
    m_status->setStyleSheet("color:#B8B8B8;");
    root->addWidget(m_status);

    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setMovement(QListWidget::Static);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setIconSize(QSize(128, 128));
    m_list->setGridSize(QSize(148, 178));
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_list, 1);

    // 双击/右键"在 Gaze 中定位"共用:向上找到主窗口的 revealFile 槽
    auto revealInGaze = [this](const QString& path) {
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
    };

    connect(m_searchBtn, &QPushButton::clicked, this, &ImageSearchDialog::doSearch);
    connect(m_input, &QLineEdit::returnPressed, this, &ImageSearchDialog::doSearch);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_list->count() > 1) resort();
    });

    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this, revealInGaze](QListWidgetItem* it) {
        revealInGaze(it->data(kPathRole).toString());
    });

    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this, revealInGaze](const QPoint& pos) {
        QListWidgetItem* it = m_list->itemAt(pos);
        if (!it) return;
        const QString path = it->data(kPathRole).toString();
        if (path.isEmpty()) return;
        QMenu menu(this);
        QAction* aReveal = menu.addAction(QString::fromUtf8("在 Gaze 中定位"));
        QAction* aOpen   = menu.addAction(QString::fromUtf8("用系统默认程序打开"));
        QAction* aShow   = menu.addAction(QString::fromUtf8("在资源管理器中显示"));
        menu.addSeparator();
        QAction* aCopy   = menu.addAction(QString::fromUtf8("复制完整路径"));
        QAction* chosen = menu.exec(m_list->mapToGlobal(pos));
        if (chosen == aReveal) {
            revealInGaze(path);
        } else if (chosen == aOpen) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        } else if (chosen == aShow) {
            QProcess::startDetached("explorer",
                {"/select,", QDir::toNativeSeparators(path)});
        } else if (chosen == aCopy) {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        }
    });

    m_input->setFocus();
}

void ImageSearchDialog::doSearch() {
    const QString q = m_input->text().trimmed();
    if (q.isEmpty() || m_running) return;
    ++m_seq;
    const int seq = m_seq;
    m_running = true;
    m_searchBtn->setEnabled(false);
    m_list->clear();
    m_status->setText(QString::fromUtf8("正在唤醒服务…(冷启动需加载模型,约 2 秒)"));

    ImgSearch::ensureRunningAsync(this, [this, seq, q](const QString& err) {
        if (seq != m_seq) return;
        if (!err.isEmpty()) {
            m_running = false;
            m_searchBtn->setEnabled(true);
            m_status->setText(err);
            return;
        }
        m_status->setText(QString::fromUtf8("搜索中:%1").arg(q));
        ImgSearch::searchAsync(q, m_model->currentData().toString(), this,
            [this, seq](int status, const QJsonDocument& doc) {
                onResults(seq, status, doc);
            });
    });
}

void ImageSearchDialog::onResults(int seq, int status, const QJsonDocument& doc) {
    if (seq != m_seq) return;
    m_running = false;
    m_searchBtn->setEnabled(true);
    if (status == 0) {
        m_status->setText(QString::fromUtf8("服务无响应(连接失败或超时)"));
        return;
    }
    if (status != 200) {
        m_status->setText(QString::fromUtf8("服务返回错误(HTTP %1)").arg(status));
        return;
    }
    const QJsonObject root = doc.object();
    if (root.isEmpty()) {
        m_status->setText(QString::fromUtf8("服务响应不是有效 JSON"));
        return;
    }
    m_total = qMax(root.value("total").toInt(), 0);
    const QJsonArray results = root.value("results").toArray();
    if (results.isEmpty()) {
        m_status->setText(QString::fromUtf8("没有匹配结果"));
        return;
    }

    const bool capped = results.size() > kMaxShown;
    const int n = qMin(results.size(), kMaxShown);
    for (int i = 0; i < n; ++i) {
        const QJsonObject o = results.at(i).toObject();
        const QJsonArray srcs = o.value("sources").toArray();
        // 角标取第一路来源+分数;全部来源在 tooltip 里
        QString badge;
        if (!srcs.isEmpty()) {
            const QJsonObject s0 = srcs.at(0).toObject();
            const QString sc = scoreText(s0, 2);
            badge = QString::fromUtf8("[%1%2] ")
                        .arg(sourceName(s0.value("type").toString()),
                             sc.isEmpty() ? QString() : sc);
        }
        const QString path = o.value("path").toString();
        QStringList tipLines;
        tipLines << path;
        for (const auto& sv : srcs) {
            const QJsonObject s = sv.toObject();
            const QString sc = scoreText(s, 3);
            tipLines << (sc.isEmpty()
                ? sourceName(s.value("type").toString())
                : QString::fromUtf8("%1: %2")
                      .arg(sourceName(s.value("type").toString()), sc));
        }
        // 可选片段:snippet/text/match 哪个在就显示哪个,都不在则不加
        for (const char* key : {"snippet", "text", "match"}) {
            const QString frag = o.value(QLatin1String(key)).toString();
            if (!frag.isEmpty()) { tipLines << frag; break; }
        }
        auto* it = new QListWidgetItem(badge + QFileInfo(path).fileName());
        it->setData(kPathRole, path);
        it->setData(kIdRole, o.value("id").toInt());
        it->setData(kRankRole, i);
        it->setToolTip(tipLines.join('\n'));
        m_list->addItem(it);
    }
    resort();   // 名称/时间模式在此本地重排;缩略图按 id 贴,与顺序无关

    QString head = QString::fromUtf8("共 %1 项").arg(m_total > 0 ? m_total : n);
    if (capped) head += QString::fromUtf8(",已显示前 %1").arg(n);
    m_status->setText(head + QString::fromUtf8("(双击在 Gaze 中打开)"));

    for (int i = 0; i < m_list->count(); ++i)
        loadThumbFor(m_list->item(i)->data(kIdRole).toInt());
}

void ImageSearchDialog::resort() {
    if (m_list->count() < 2) return;
    QList<QListWidgetItem*> items;
    items.reserve(m_list->count());
    while (m_list->count()) items.append(m_list->takeItem(0));
    switch (m_sort->currentData().toInt()) {
    case 1: {   // 名称:QCollator 自然排序(IMG_2 < IMG_10)
        QCollator coll;
        coll.setNumericMode(true);
        coll.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(items.begin(), items.end(),
                  [&coll](QListWidgetItem* a, QListWidgetItem* b) {
            return coll.compare(
                QFileInfo(a->data(kPathRole).toString()).fileName(),
                QFileInfo(b->data(kPathRole).toString()).fileName()) < 0;
        });
        break;
    }
    case 2:     // 时间:取修改时间(逐条 stat 只在选中此模式时发生并缓存)
        std::sort(items.begin(), items.end(),
                  [this](QListWidgetItem* a, QListWidgetItem* b) {
            return mtimeOf(a) < mtimeOf(b);
        });
        break;
    default:    // 相关性:恢复服务端返回序
        std::sort(items.begin(), items.end(),
                  [](QListWidgetItem* a, QListWidgetItem* b) {
            return a->data(kRankRole).toInt() < b->data(kRankRole).toInt();
        });
        break;
    }
    for (QListWidgetItem* it : items) m_list->addItem(it);
}

qint64 ImageSearchDialog::mtimeOf(QListWidgetItem* it) {
    const QVariant v = it->data(kMtimeRole);
    if (v.isValid()) return v.toLongLong();
    const qint64 m = QFileInfo(it->data(kPathRole).toString())
                         .lastModified().toMSecsSinceEpoch();
    it->setData(kMtimeRole, m);
    return m;
}

void ImageSearchDialog::loadThumbFor(int imageId) {
    // 后台线程拉取,回 GUI 线程按 imageId 贴图标(重排不影响,行号无关)
    QPointer<ImageSearchDialog> self(this);
    QThreadPool::globalInstance()->start([self, imageId]() {
        const QByteArray data = ImgSearch::thumbBytes(imageId);
        QImage img;
        if (!data.isEmpty()) img.loadFromData(data);
        QMetaObject::invokeMethod(self, [self, imageId, img]() {
            if (!self || img.isNull()) return;
            self->applyIcon(imageId, img);
        }, Qt::QueuedConnection);
    });
}

void ImageSearchDialog::applyIcon(int imageId, const QImage& img) {
    const QPixmap pm = QPixmap::fromImage(img);
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(kIdRole).toInt() == imageId && it->icon().isNull())
            it->setIcon(QIcon(pm));
    }
}
