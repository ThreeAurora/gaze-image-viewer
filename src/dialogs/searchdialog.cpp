#include "searchdialog.h"
#include "fileentry.h"
#include "constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QCursor>
#include <QToolTip>
#include <QMetaObject>

namespace {
constexpr int kMaxResults = 20000;   // 命中上限:再多的行列表也没法看
constexpr int kMaxDirs    = 50000;   // 目录上限:junction 成环时靠它兜底
constexpr int kMaxDepth   = 40;      // 深度上限:同上,保证 BFS 一定终止
constexpr qint64 kTickBudgetMs = 6;  // 每拍最多花这么久,剩下还给事件循环

// 词分隔:空格/分号/逗号等价
QStringList splitTerms(const QString& text) {
    QString t = text;
    t.replace(QLatin1Char(';'), QLatin1Char(' ')).replace(QLatin1Char(','), QLatin1Char(' '));
    QStringList out;
    for (const QString& raw : t.split(QLatin1Char(' '))) {
        const QString term = raw.trimmed();
        if (!term.isEmpty()) out << term;
    }
    return out;
}

QRegularExpression wildcardTerm(const QString& term) {
    return QRegularExpression(
        QRegularExpression::wildcardToRegularExpression(term),
        QRegularExpression::CaseInsensitiveOption);
}

bool wildHit(const QList<QRegularExpression>& list, const QString& name) {
    for (const QRegularExpression& re : list)
        if (re.match(name).hasMatch()) return true;
    return false;
}

bool plainHit(const QStringList& list, const QString& name) {
    for (const QString& t : list)
        if (name.contains(t, Qt::CaseInsensitive)) return true;
    return false;
}
} // namespace

SearchDialog::SearchDialog(const QString& rootDir, QWidget* parent)
    : QDialog(parent), m_root(rootDir)
{
    setWindowTitle(QString::fromUtf8("搜索 - ") + QFileInfo(rootDir).fileName());
    setAttribute(Qt::WA_DeleteOnClose);   // 非模态:关掉就该回收
    resize(860, 560);
    setStyleSheet(
        "QDialog{background:" C_WIN_BG ";}"
        "QLabel{color:#FFFFFF;background:transparent;}"
        "QLineEdit{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:5px 8px;}"
        "QPushButton{background:" C_TOOLBAR ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:6px 18px;}"
        "QPushButton:hover{border-color:" C_ACCENT ";}"
        "QPushButton:disabled{color:#7A7A82;}"
        "QCheckBox{color:#FFFFFF;background:transparent;spacing:6px;}"
        "QTreeWidget{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "outline:none;}"
        "QTreeWidget::item{padding:3px 2px;}"
        "QTreeWidget::item:selected{background:" C_ACCENT ";}"
        "QHeaderView::section{background:" C_TOOLBAR ";color:#FFFFFF;"
        "border:1px solid " C_SEPARATOR ";padding:4px 6px;}");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    m_include = new QLineEdit;
    m_include->setPlaceholderText(QString::fromUtf8("子串或通配 * ?;多个词用空格分隔,任一命中即算"));
    m_exclude = new QLineEdit;
    m_exclude->setPlaceholderText(QString::fromUtf8("命中这些词的结果被排除(同样的词法规则)"));
    form->addWidget(new QLabel(QString::fromUtf8("名称:")), 0, 0);
    form->addWidget(m_include, 0, 1);
    form->addWidget(new QLabel(QString::fromUtf8("排除:")), 1, 0);
    form->addWidget(m_exclude, 1, 1);
    root->addLayout(form);

    auto* opts = new QHBoxLayout;
    opts->setSpacing(14);
    m_recurse = new QCheckBox(QString::fromUtf8("包含子文件夹"));
    m_recurse->setChecked(true);
    m_hidden  = new QCheckBox(QString::fromUtf8("包含隐藏项"));
    m_dirsToo = new QCheckBox(QString::fromUtf8("同时搜索文件夹"));
    m_runBtn  = new QPushButton(QString::fromUtf8("搜索"));
    m_stopBtn = new QPushButton(QString::fromUtf8("停止"));
    m_stopBtn->setEnabled(false);
    opts->addWidget(m_recurse);
    opts->addWidget(m_hidden);
    opts->addWidget(m_dirsToo);
    opts->addStretch(1);
    opts->addWidget(m_runBtn);
    opts->addWidget(m_stopBtn);
    root->addLayout(opts);

    m_status = new QLabel(QString::fromUtf8("范围:%1").arg(QDir::toNativeSeparators(rootDir)));
    m_status->setStyleSheet("color:#B8B8B8;");
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);

    m_results = new QTreeWidget;
    m_results->setColumnCount(4);
    m_results->setHeaderLabels({ QString::fromUtf8("名称"), QString::fromUtf8("大小"),
                                 QString::fromUtf8("修改时间"), QString::fromUtf8("位置") });
    m_results->setRootIsDecorated(false);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_results->header()->setStretchLastSection(true);
    root->addWidget(m_results, 1);

    connect(m_runBtn, &QPushButton::clicked, this, &SearchDialog::startSearch);
    connect(m_stopBtn, &QPushButton::clicked, this,
            [this]() { stopScan(QString::fromUtf8("已手动停止")); });
    connect(m_include, &QLineEdit::returnPressed, this, &SearchDialog::startSearch);
    connect(m_exclude, &QLineEdit::returnPressed, this, &SearchDialog::startSearch);
    connect(&m_ticker, &QTimer::timeout, this, &SearchDialog::stepScan);
    m_ticker.setSingleShot(true);
    m_ticker.setInterval(0);

    connect(m_results, &QTreeWidget::itemDoubleClicked,
            this, &SearchDialog::openResult);

    m_include->setFocus();
}

// 词法在这里定形:含 * 或 ? 的整词编成正则,其余按子串。想按字面搜名字里带 *
// 的文件,这里不猜 —— 规则写进占位提示,和 XnView 的口径一致。
void SearchDialog::startSearch() {
    if (m_running) return;
    if (m_root.isEmpty() || !QFileInfo(m_root).isDir()) {
        m_status->setText(QString::fromUtf8("起始文件夹已不存在"));
        return;
    }
    m_incPlain.clear();
    m_incWild.clear();
    m_excPlain.clear();
    m_excWild.clear();
    for (const QString& t : splitTerms(m_include->text())) {
        if (t.contains(QLatin1Char('*')) || t.contains(QLatin1Char('?')))
            m_incWild.append(wildcardTerm(t));
        else
            m_incPlain.append(t);
    }
    for (const QString& t : splitTerms(m_exclude->text())) {
        if (t.contains(QLatin1Char('*')) || t.contains(QLatin1Char('?')))
            m_excWild.append(wildcardTerm(t));
        else
            m_excPlain.append(t);
    }
    if (m_incPlain.isEmpty() && m_incWild.isEmpty()) {
        m_status->setText(QString::fromUtf8("请输入名称条件"));
        return;
    }
    m_skipHidden = !m_hidden->isChecked();
    m_wantDirs   = m_dirsToo->isChecked();

    m_results->clear();
    m_queue.clear();
    m_queue.enqueue({ m_root, 0 });
    m_scannedDirs = 0;
    m_matches = 0;
    m_running = true;
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_status->setText(QString::fromUtf8("搜索中…"));
    m_ticker.start();
}

bool SearchDialog::matches(const QString& name) const {
    if (!(plainHit(m_incPlain, name) || wildHit(m_incWild, name))) return false;
    return !(plainHit(m_excPlain, name) || wildHit(m_excWild, name));
}

void SearchDialog::stepScan() {
    if (!m_running) return;
    QElapsedTimer clock;
    clock.start();
    const bool recurse = m_recurse->isChecked();

    while (!m_queue.isEmpty()) {
        const auto [dir, depth] = m_queue.dequeue();
        for (const FileEntry& fe : fastScanDir(dir)) {
            if (fe.isDir) {
                const bool hiddenDir = m_skipHidden && fe.hidden;
                if (m_wantDirs && !hiddenDir && matches(fe.name)) {
                    QTreeWidgetItem* it = new QTreeWidgetItem(m_results);
                    it->setIcon(0, folderIcon(16));
                    it->setText(0, fe.name);
                    it->setText(2, formatDate(fe.mtime));
                    it->setText(3, QDir::toNativeSeparators(dir));
                    it->setData(0, Qt::UserRole, fe.path);
                    it->setData(0, Qt::UserRole + 1, true);   // 标记:这一行是文件夹
                    ++m_matches;
                }
                if (recurse && !hiddenDir && depth + 1 <= kMaxDepth)
                    m_queue.enqueue({ fe.path, depth + 1 });
                continue;
            }
            if (m_skipHidden && fe.hidden) continue;
            if (!matches(fe.name)) continue;
            QTreeWidgetItem* it = new QTreeWidgetItem(m_results);
            it->setIcon(0, typeIcon(fe.ext));
            it->setText(0, fe.name);
            it->setText(1, formatSize(fe.size));
            it->setText(2, formatDate(fe.mtime));
            it->setText(3, QDir::toNativeSeparators(dir));
            it->setData(0, Qt::UserRole, fe.path);
            ++m_matches;
            if (m_matches >= kMaxResults) {
                stopScan(QString::fromUtf8("已达 %1 条命中上限,其余未扫").arg(kMaxResults));
                return;
            }
        }
        ++m_scannedDirs;
        if (m_scannedDirs >= kMaxDirs) {
            stopScan(QString::fromUtf8("已达 %1 个目录上限(可能遇到链接环),其余未扫")
                         .arg(kMaxDirs));
            return;
        }
        if (clock.elapsed() >= kTickBudgetMs) break;
    }

    if (m_queue.isEmpty()) {
        stopScan(QString());
        return;
    }
    m_status->setText(QString::fromUtf8("搜索中:已扫 %1 个目录,命中 %2 项…")
                          .arg(m_scannedDirs).arg(m_matches));
    m_ticker.start();
}

void SearchDialog::stopScan(const QString& tail) {
    m_running = false;
    m_ticker.stop();
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    QString text = QString::fromUtf8("已扫 %1 个目录,命中 %2 项 —— 双击结果在主窗口定位")
                       .arg(m_scannedDirs).arg(m_matches);
    if (!tail.isEmpty()) text += QString::fromUtf8(" —— ") + tail;
    m_status->setText(text);
}

void SearchDialog::closeEvent(QCloseEvent* ev) {
    m_running = false;
    m_ticker.stop();
    QDialog::closeEvent(ev);
}

// 结果落回主窗口:文件走 revealFile(进目录并选中),文件夹行直接进该目录
void SearchDialog::openResult(QTreeWidgetItem* it) {
    if (!it) return;
    const QString path = it->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    const bool isDir = it->data(0, Qt::UserRole + 1).toBool();
    const char* slot = isDir ? "navigateTo(QString)" : "revealFile(QString)";
    QObject* mw = this;
    while (mw && mw->metaObject()->indexOfSlot(slot) < 0) mw = mw->parent();
    if (!mw) {
        QToolTip::showText(QCursor::pos(), QString::fromUtf8("无法定位主窗口"));
        return;
    }
    QMetaObject::invokeMethod(mw, slot, Q_ARG(QString, path));
    if (isDir) accept();
}
