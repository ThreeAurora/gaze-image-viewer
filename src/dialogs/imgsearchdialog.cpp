#include "imgsearchdialog.h"
#include "imgsearch.h"
#include "constants.h"
#include "i18n.h"

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
#include <QPointer>
#include <QProcess>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int kMaxShown  = 200;              // 显示上限(服务端可能给更多)
constexpr int kPathRole  = Qt::UserRole;     // 完整路径
constexpr int kIdRole    = Qt::UserRole + 1; // imgseek image id(取缩略图的键)
constexpr int kRankRole  = Qt::UserRole + 2; // 服务端返回序(=相关性)
constexpr int kMtimeRole = Qt::UserRole + 3; // lastModified 缓存(选"时间"时才统计)

QString sourceName(const QString& type) {
    if (type == "name") return gazeTr("名称");
    if (type == "ocr")  return gazeTr("文字");
    if (type == "sem")  return gazeTr("语义");
    return type;
}
QString scoreText(const QJsonObject& s, int prec) {
    if (!s.contains("score")) return QString();
    return QString::number(s.value("score").toDouble(), 'f', prec);
}
}

ImageSearchDialog::ImageSearchDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(gazeTr("以文搜图 - 万象图搜"));
    resize(880, 640);
    // 整表样式在应用级 QSS(QDialog#imgSearchDialog 规则组,#89 收敛)
    setObjectName(QStringLiteral("imgSearchDialog"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(8);

    // ── 2026-09-05 用户令:索引目录管理整体搬进 Gaze,不再依赖网页端 ──
    // 左栏 = 索引目录(添加/扫描/重试 + 右键纳入/暂停/删除)与服务状态;
    // 右栏 = 原搜索区。全部走 docs/imgseek/API_SCHEMA.md 定案字段。
    auto* body = new QHBoxLayout;
    body->setSpacing(10);
    root->addLayout(body, 1);

    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(6);
    auto* leftTitle = new QLabel(gazeTr("索引目录"));
    leftTitle->setStyleSheet(QStringLiteral("font-weight:600;background:transparent;"));
    leftCol->addWidget(leftTitle);

    m_folderList = new QListWidget;
    m_folderList->setFixedWidth(280);
    m_folderList->setWordWrap(true);
    m_folderList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_folderList->setToolTip(gazeTr(
        "右键目录:纳入/移出搜索、暂停/恢复索引、删除目录(服务端自动清理)"));
    leftCol->addWidget(m_folderList, 1);

    auto* leftBtns = new QHBoxLayout;
    leftBtns->setSpacing(4);
    auto* addBtn = new QPushButton(gazeTr("添加目录…"));
    addBtn->setToolTip(gazeTr("把一个目录加入索引(服务端自动扫描其中的图片)"));
    m_scanBtn = new QPushButton(gazeTr("开始扫描"));
    m_scanBtn->setToolTip(gazeTr("触发服务端全量/增量扫描(缩略图/OCR/语义向量)"));
    auto* retryBtn = new QPushButton(gazeTr("重试失败"));
    retryBtn->setToolTip(gazeTr("对永久失败条目按阶段重跑"));
    leftBtns->addWidget(addBtn);
    leftBtns->addWidget(m_scanBtn, 1);
    leftBtns->addWidget(retryBtn);
    leftCol->addLayout(leftBtns);

    m_svcStatus = new QLabel(gazeTr("服务状态:—"));
    m_svcStatus->setObjectName("imgSearchStatus");
    m_svcStatus->setWordWrap(true);
    leftCol->addWidget(m_svcStatus);
    m_svcModel = new QLabel(gazeTr("激活模型:—"));
    m_svcModel->setObjectName("imgSearchStatus");
    leftCol->addWidget(m_svcModel);

    auto* leftW = new QWidget;
    leftW->setLayout(leftCol);
    body->addWidget(leftW);

    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(8);
    body->addLayout(rightCol, 1);

    // 目录列表条目:UserRole=id,UserRole+1=path,UserRole+2=enabled,UserRole+3=paused
    connect(m_folderList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        QListWidgetItem* it = m_folderList->itemAt(pos);
        if (!it) return;
        const int id = it->data(Qt::UserRole).toInt();
        const bool enabled = it->data(Qt::UserRole + 2).toInt() != 0;
        const bool paused  = it->data(Qt::UserRole + 3).toInt() != 0;
        QMenu menu(this);
        QAction* aToggle = menu.addAction(enabled
            ? gazeTr("移出搜索与索引") : gazeTr("纳入搜索与索引"));
        QAction* aPause  = menu.addAction(paused
            ? gazeTr("恢复索引处理") : gazeTr("暂停索引处理(保留搜索)"));
        menu.addSeparator();
        QAction* aDel    = menu.addAction(gazeTr("删除目录"));
        QAction* chosen  = menu.exec(m_folderList->mapToGlobal(pos));
        if (chosen == aToggle) toggleFolder(id, !enabled);
        else if (chosen == aPause) pauseFolder(id, !paused);
        else if (chosen == aDel) removeFolder(id);
    });
    connect(addBtn, &QPushButton::clicked, this, [this]() { addFolder(); });
    connect(m_scanBtn, &QPushButton::clicked, this, [this]() { startScan(); });
    connect(retryBtn, &QPushButton::clicked, this, [this, retryBtn]() {
        QMenu menu(this);
        QAction* aThumb = menu.addAction(gazeTr("重跑缩略图(thumb)"));
        QAction* aOcr   = menu.addAction(gazeTr("重跑文字识别(ocr)"));
        QAction* aEmb   = menu.addAction(gazeTr("重跑语义向量(embed,当前模型)"));
        QAction* chosen = menu.exec(retryBtn->mapToGlobal(QPoint(0, retryBtn->height())));
        if (chosen == aThumb) retryStage(QStringLiteral("thumb"));
        else if (chosen == aOcr) retryStage(QStringLiteral("ocr"));
        else if (chosen == aEmb) retryStage(QStringLiteral("embed"));
    });

    // 可见期间 5s 轮询服务状态(扫描进度/速率),隐藏即停
    m_svcTimer = new QTimer(this);
    m_svcTimer->setInterval(5000);
    connect(m_svcTimer, &QTimer::timeout, this, [this]() { refreshServiceStatus(); });

    auto* top = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setPlaceholderText(gazeTr(
        "输入自然语言、文件名或图片中的文字,如:海边的日落 / IMG_2022 / 发票"));
    m_model = new QComboBox;
    m_model->addItem(gazeTr("引擎默认"), QString());
    m_model->addItem(QStringLiteral("cn_clip_b16"), QStringLiteral("cn_clip_b16"));
    m_model->addItem(QStringLiteral("clip_b32"), QStringLiteral("clip_b32"));
    m_model->setToolTip(gazeTr("CLIP 语义模型;引擎默认由服务端自选"));
    m_sort = new QComboBox;
    m_sort->addItem(gazeTr("按相关性"), 0);
    m_sort->addItem(gazeTr("按名称"), 1);
    m_sort->addItem(gazeTr("按时间"), 2);
    m_sort->setToolTip(gazeTr(
        "名称/时间为本地重排(服务端只保证相关性顺序)"));
    m_searchBtn = new QPushButton(gazeTr("搜索"));
    top->addWidget(m_input, 1);
    top->addWidget(m_model);
    top->addWidget(m_sort);
    top->addWidget(m_searchBtn);
    rightCol->addLayout(top);

    m_status = new QLabel(gazeTr("输入关键词后回车;服务未运行时会自动拉起"));
    m_status->setObjectName(QStringLiteral("imgSearchStatus"));
    rightCol->addWidget(m_status);

    m_list = new QListWidget;
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setMovement(QListWidget::Static);
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setIconSize(QSize(128, 128));
    m_list->setGridSize(QSize(148, 178));
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    rightCol->addWidget(m_list, 1);

    // 双击/右键"在 Gaze 中定位"共用:向上找到主窗口的 revealFile
    // (Q_INVOKABLE 而非槽,须用 indexOfMethod —— 与 searchdialog.cpp 同因)
    auto revealInGaze = [this](const QString& path) {
        if (path.isEmpty()) return;
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("revealFile(QString)") < 0)
            mw = mw->parent();
        if (mw) {
            QMetaObject::invokeMethod(mw, "revealFile", Q_ARG(QString, path));
            accept();
        } else {
            QToolTip::showText(QCursor::pos(), gazeTr("无法定位主窗口"));
        }
    };

    connect(m_searchBtn, &QPushButton::clicked, this, &ImageSearchDialog::doSearch);
    connect(m_input, &QLineEdit::returnPressed, this, &ImageSearchDialog::doSearch);
    // 打开即对账索引目录与服务状态(铁律 #222:绝不自动拉起服务,离线只提示)
    refreshFolders();
    m_svcTimer->start();
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
        QAction* aReveal = menu.addAction(gazeTr("在 Gaze 中定位"));
        QAction* aOpen   = menu.addAction(gazeTr("用系统默认程序打开"));
        QAction* aShow   = menu.addAction(gazeTr("在资源管理器中显示"));
        menu.addSeparator();
        QAction* aCopy   = menu.addAction(gazeTr("复制完整路径"));
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
    m_status->setText(gazeTr("正在唤醒服务…(冷启动需加载模型,约 2 秒)"));

    ImgSearch::ensureRunningAsync(this, [this, seq, q](const QString& err) {
        if (seq != m_seq) return;
        if (!err.isEmpty()) {
            m_running = false;
            m_searchBtn->setEnabled(true);
            m_status->setText(err);
            return;
        }
        m_status->setText(gazeTr("搜索中:%1").arg(q));
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
        m_status->setText(gazeTr("服务无响应(连接失败或超时)"));
        return;
    }
    if (status != 200) {
        m_status->setText(gazeTr("服务返回错误(HTTP %1)").arg(status));
        return;
    }
    const QJsonObject root = doc.object();
    if (root.isEmpty()) {
        m_status->setText(gazeTr("服务响应不是有效 JSON"));
        return;
    }
    m_total = qMax(root.value("total").toInt(), 0);
    const QJsonArray results = root.value("results").toArray();
    if (results.isEmpty()) {
        m_status->setText(gazeTr("没有匹配结果"));
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
            badge = gazeTr("[%1%2] ")
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
                : gazeTr("%1: %2")
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

    QString head = gazeTr("共 %1 项").arg(m_total > 0 ? m_total : n);
    if (capped) head += gazeTr(",已显示前 %1").arg(n);
    m_status->setText(head + gazeTr("(双击在 Gaze 中打开)"));

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

// ═══════════════════════════════════════════
// 索引目录管理(Tier2 #133):字段零猜测,全部按 docs/imgseek/API_SCHEMA.md
// ═══════════════════════════════════════════
void ImageSearchDialog::refreshFolders() {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::foldersAsync(this, [self](int status, const QJsonDocument& doc) {
        if (!self) return;
        if (status == 0) {
            self->m_svcStatus->setText(gazeTr(
                "服务状态:离线(设置 → 以文搜图 可开自动启动,或手动运行 main.py)"));
            self->m_folderList->clear();
            return;
        }
        if (status != 200) return;
        self->m_folderList->clear();
        const QJsonArray folders = doc.object().value("folders").toArray();
        for (const auto& fv : folders) {
            const QJsonObject f = fv.toObject();
            const bool enabled = f.value("enabled").toInt() != 0;
            const bool paused  = f.value("paused").toInt() != 0;
            QString tag;
            if (!enabled) tag = gazeTr(" [已移出]");
            else if (paused) tag = gazeTr(" [索引已暂停]");
            auto* it = new QListWidgetItem(gazeTr("%1%2\n已处理 %3 · 待处理 %4 · 失效 %5")
                .arg(f.value("path").toString(), tag)
                .arg(f.value("processed").toInt())
                .arg(f.value("pending").toInt())
                .arg(f.value("dead").toInt()));
            it->setData(Qt::UserRole, f.value("id").toInt());
            it->setData(Qt::UserRole + 1, f.value("path").toString());
            it->setData(Qt::UserRole + 2, enabled ? 1 : 0);
            it->setData(Qt::UserRole + 3, paused ? 1 : 0);
            self->m_folderList->addItem(it);
        }
        self->refreshServiceStatus();
    });
}

void ImageSearchDialog::refreshServiceStatus() {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::statusAsync(this, [self](int status, const QJsonDocument& doc) {
        if (!self || status != 200) return;
        const QJsonObject o = doc.object();
        const bool scanning = o.value("scanning").toBool();
        const double rate = o.value("rate_per_sec").toDouble();
        const int pendThumb = o.value("pending").toObject().value("thumb").toInt();
        const int pendOcr   = o.value("pending").toObject().value("ocr").toInt();
        const int failThumb = o.value("failed").toObject().value("thumb").toInt();
        const int failOcr   = o.value("failed").toObject().value("ocr").toInt();
        self->m_svcStatus->setText(gazeTr(
            "服务状态:%1 · %2 张/秒\n待处理 缩略图 %3 / 文字 %4 · 失败 %5/%6 · 库内 %7 张")
            .arg(scanning ? gazeTr("扫描中") : gazeTr("空闲"))
            .arg(rate, 0, 'f', 1)
            .arg(pendThumb).arg(pendOcr).arg(failThumb).arg(failOcr)
            .arg(o.value("images_total").toInt()));
        self->m_svcModel->setText(gazeTr("激活模型:%1")
            .arg(o.value("active_model").toString()));
        self->m_scanBtn->setEnabled(!scanning);
    });
}

void ImageSearchDialog::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, gazeTr("添加索引目录"));
    if (dir.isEmpty()) return;
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::postJsonAsync(QStringLiteral("/api/folders"),
        QJsonDocument(QJsonObject{{"path", QDir::toNativeSeparators(dir)}}).toJson(),
        8000, this, [self](int status, const QJsonDocument& doc) {
        if (!self) return;
        if (status == 200 || status == 400) {
            const QString msg = doc.object().value("msg").toString();
            self->m_svcStatus->setText(msg.isEmpty()
                ? gazeTr("已添加目录") : gazeTr("添加目录:%1").arg(msg));
        } else {
            self->m_svcStatus->setText(gazeTr("添加目录失败(HTTP %1)").arg(status));
        }
        self->refreshFolders();
    });
}

void ImageSearchDialog::removeFolder(int id) {
    if (QMessageBox::question(this, gazeTr("删除索引目录"),
            gazeTr("删除后服务端会自动清理该目录的索引数据。继续?"))
        != QMessageBox::Yes) return;
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::deleteAsync(QStringLiteral("/api/folders/%1").arg(id),
        15000, this, [self](int status, const QJsonDocument&) {
        if (!self) return;
        self->m_svcStatus->setText(status == 200
            ? gazeTr("目录已删除") : gazeTr("删除失败(HTTP %1)").arg(status));
        self->refreshFolders();
    });
}

void ImageSearchDialog::toggleFolder(int id, bool enabled) {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::postJsonAsync(QStringLiteral("/api/folders/%1/toggle").arg(id),
        QJsonDocument(QJsonObject{{"enabled", enabled}}).toJson(),
        8000, this, [self](int, const QJsonDocument&) { if (self) self->refreshFolders(); });
}

void ImageSearchDialog::pauseFolder(int id, bool on) {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::postJsonAsync(QStringLiteral("/api/folders/%1/pause").arg(id),
        QJsonDocument(QJsonObject{{"on", on}}).toJson(),
        8000, this, [self](int, const QJsonDocument&) { if (self) self->refreshFolders(); });
}

void ImageSearchDialog::startScan() {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::postJsonAsync(QStringLiteral("/api/scan/start"), QByteArray("{}"),
        8000, this, [self](int status, const QJsonDocument&) {
        if (!self) return;
        self->m_svcStatus->setText(status == 200
            ? gazeTr("已触发扫描") : gazeTr("触发扫描失败(HTTP %1)").arg(status));
        self->refreshServiceStatus();
    });
}

void ImageSearchDialog::retryStage(const QString& stage) {
    QPointer<ImageSearchDialog> self(this);
    ImgSearch::postJsonAsync(QStringLiteral("/api/retry"),
        QJsonDocument(QJsonObject{{"stage", stage}}).toJson(),
        8000, this, [self, stage](int status, const QJsonDocument& doc) {
        if (!self) return;
        if (status == 200)
            self->m_svcStatus->setText(gazeTr("已重置 %1 条失败项(%2)")
                .arg(doc.object().value("reset").toInt()).arg(stage));
        else
            self->m_svcStatus->setText(gazeTr("重试失败(HTTP %1)").arg(status));
        self->refreshServiceStatus();
    });
}
