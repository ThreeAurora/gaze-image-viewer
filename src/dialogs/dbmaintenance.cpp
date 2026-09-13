#include "dbmaintenance.h"
#include "constants.h"
#include "settings.h"
#include "dbprefix.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <algorithm>

namespace {
// 体积列:显示 MB 文本、按真实字节数排序(直接存字符串会让 9.9 排在 10.2 后面)
class BytesItem : public QTableWidgetItem {
public:
    explicit BytesItem(qint64 bytes) : m_bytes(bytes) {
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    QVariant data(int role) const override {
        return role == Qt::DisplayRole
            ? QVariant(QString::asprintf("%.2f MB", m_bytes / 1024.0 / 1024.0))
            : QTableWidgetItem::data(role);
    }
    bool operator<(const QTableWidgetItem& o) const override {
        const auto* rhs = dynamic_cast<const BytesItem*>(&o);
        return rhs ? m_bytes < rhs->m_bytes : QTableWidgetItem::operator<(o);
    }
private:
    qint64 m_bytes;
};
}

static QSqlDatabase maintenanceDb() {
    const QString conn = QStringLiteral("maint_db");
    if (!QSqlDatabase::contains(conn)) {
        QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", conn);
        d.setDatabaseName(AppSettings::instance().dataDir() + "/thumbnails.db");
        d.open();
    }
    return QSqlDatabase::database(conn);
}

DbMaintenanceDialog::DbMaintenanceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(gazeTr("缩略图数据库维护"));
    resize(760, 540);
    // 整表样式在应用级 QSS(QDialog#dbMaintDialog 规则组,#89 收敛)
    setObjectName(QStringLiteral("dbMaintDialog"));

    auto* root = new QVBoxLayout(this);

    m_summary = new QLabel;
    root->addWidget(m_summary);

    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({gazeTr("缓存目录"),
        gazeTr("文件数"), gazeTr("标记"), gazeTr("缩略图"), gazeTr("体积")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 5; ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    auto* btns = new QHBoxLayout;
    auto addBtn = [this, &btns](const QString& txt, auto slot) {
        auto* b = new QPushButton(txt);
        connect(b, &QPushButton::clicked, this, slot);
        btns->addWidget(b);
    };
    addBtn(gazeTr("删除选中目录条目"), [this]() { deleteSelected(); });
    addBtn(gazeTr("同步文件夹"), [this]() { syncSelected(); });
    addBtn(gazeTr("重新定位"), [this]() { relocateSelected(); });
    addBtn(gazeTr("导出清单"), [this]() { exportCsv(); });
    addBtn(gazeTr("删除全部"), [this]() { deleteAll(); });
    addBtn(gazeTr("重建缩略图"), [this]() { rebuildThumbs(); });
    btns->addStretch();
    auto* closeBtn = new QPushButton(gazeTr("关闭"));
    // 显式默认:不设时 Enter 与"空格=确认"都落在**创建最早**的按钮上,而那是
    // "删除选中目录条目"(直接 DELETE,无二次确认)。实测见 cache/tmp/space_confirm_test.cpp
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    reload();
}

void DbMaintenanceDialog::reload() {
    QSqlDatabase d = maintenanceDb();
    m_byDir.clear();
    qint64 totalBytes = 0;
    int totalThumbs = 0;
    int totalLabels = 0;

    auto dirOf = [](const QString& path) {
        int slash = path.lastIndexOf('/');
        if (slash < 0) slash = path.lastIndexOf('\\');
        return slash > 0 ? path.left(slash + 1) : path;
    };

    // 缩略图键 = 媒体路径|尺寸|代际后缀;Windows 文件名不可能含 '|',首个 '|' 前即路径
    QSqlQuery q(d);
    if (q.exec("SELECT key, LENGTH(png) FROM thumbs")) {
        while (q.next()) {
            const QString key = q.value(0).toString();
            const qint64 bytes = q.value(1).toLongLong();
            const int bar = key.indexOf('|');
            const QString file = bar > 0 ? key.left(bar) : key;
            DirStat& rec = m_byDir[dirOf(file)];
            rec.files.insert(file);
            rec.thumbs += 1;
            rec.bytes += bytes;
            totalBytes += bytes;
            ++totalThumbs;
        }
    }
    if (q.exec("SELECT path FROM labels")) {
        while (q.next()) {
            const QString file = q.value(0).toString();
            DirStat& rec = m_byDir[dirOf(file)];
            rec.files.insert(file);
            rec.labels += 1;
            ++totalLabels;
        }
    }

    QFileInfo fi(d.databaseName());
    qint64 dbSize = fi.size();
    m_summary->setText(gazeTr(
        "数据库:%1  ·  目录:%2  ·  缓存条目:%3  ·  标记:%4  ·  缩略图合计:%5")
        .arg(fi.fileName() + QString(" (%1 MB)").arg(dbSize / 1024 / 1024))
        .arg(m_byDir.size())
        .arg(totalThumbs)
        .arg(totalLabels)
        .arg(QString::asprintf("%.2f MB", totalBytes / 1024.0 / 1024.0)));

    // 按目录名升序填表;填表期间关排序,否则 sortByColumn 会边插边搬行
    m_table->setSortingEnabled(false);
    QList<QString> dirs = m_byDir.keys();
    std::sort(dirs.begin(), dirs.end());
    auto numItem = [](qint64 v) {
        auto* it = new QTableWidgetItem;
        it->setData(Qt::DisplayRole, v);   // 数值型 variant,点表头按数值排
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return it;
    };
    m_table->setRowCount(dirs.size());
    for (int i = 0; i < dirs.size(); ++i) {
        const DirStat& rec = m_byDir[dirs[i]];
        m_table->setItem(i, 0, new QTableWidgetItem(dirs[i]));
        m_table->setItem(i, 1, numItem(rec.files.size()));
        m_table->setItem(i, 2, numItem(rec.labels));
        m_table->setItem(i, 3, numItem(rec.thumbs));
        m_table->setItem(i, 4, new BytesItem(rec.bytes));
    }
    m_table->setSortingEnabled(true);
}

QStringList DbMaintenanceDialog::selectedDirs() const {
    QStringList out;
    const auto rows = m_table->selectionModel()->selectedRows(0);
    for (const auto& idx : rows) out << idx.data().toString();
    return out;
}

void DbMaintenanceDialog::deleteSelected() {
    const QStringList dirs = selectedDirs();
    if (dirs.isEmpty()) return;
    // 与"删除全部""重建缩略图"对齐:破坏性动作一律先问一句
    QString preview = dirs.size() <= 5
        ? dirs.join(QLatin1Char('\n'))
        : dirs.mid(0, 5).join(QLatin1Char('\n')) +
              gazeTr("\n…等 %1 个目录").arg(dirs.size());
    if (QMessageBox::question(this, gazeTr("删除条目"),
        gazeTr("删除所选 %1 个目录的全部缓存条目?\n%2\n(浏览时会自动重建)")
            .arg(dirs.size()).arg(preview))
        != QMessageBox::Yes) return;
    QSqlDatabase d = maintenanceDb();
    for (const QString& dir : dirs) {
        const QString bare = dir.endsWith('/') ? dir.left(dir.size() - 1) : dir;
        QSqlQuery q(d);
        q.prepare("DELETE FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        q.prepare("DELETE FROM labels WHERE path LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        // dirsize 落盘形态(带不带尾斜杠)未定,精确+前缀双句兜底
        q.prepare("DELETE FROM dirsize WHERE path = ?");
        q.addBindValue(bare);
        q.exec();
        q.prepare("DELETE FROM dirsize WHERE path LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
    }
    reload();
}

void DbMaintenanceDialog::syncSelected() {
    const QStringList dirs = selectedDirs();
    if (dirs.isEmpty()) {
        QMessageBox::information(this, gazeTr("同步文件夹"),
            gazeTr("先在列表中选中要同步的目录。"));
        return;
    }
    if (QMessageBox::question(this, gazeTr("同步文件夹"),
        gazeTr("清除所选目录中源文件已不存在的缩略图与标记条目?继续?"))
        != QMessageBox::Yes) return;

    QSqlDatabase d = maintenanceDb();
    QStringList deadThumbs, deadLabels;
    for (const QString& dir : dirs) {
        QSqlQuery q(d);
        q.prepare("SELECT key FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        while (q.next()) {
            const QString key = q.value(0).toString();
            const int bar = key.indexOf('|');
            if (bar > 0 && !QFileInfo::exists(key.left(bar)))
                deadThumbs << key;
        }
        q.prepare("SELECT path FROM labels WHERE path LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        while (q.next()) {
            const QString path = q.value(0).toString();
            if (!QFileInfo::exists(path)) deadLabels << path;
        }
    }

    if (!d.transaction()) return;
    QSqlQuery del(d);
    del.prepare("DELETE FROM thumbs WHERE key = ?");
    for (const QString& key : deadThumbs) { del.addBindValue(key); del.exec(); }
    del.prepare("DELETE FROM labels WHERE path = ?");
    for (const QString& path : deadLabels) { del.addBindValue(path); del.exec(); }
    d.commit();

    reload();
    QMessageBox::information(this, gazeTr("同步文件夹"),
        gazeTr("已清除失效缩略图 %1 条、失效标记 %2 条。")
            .arg(deadThumbs.size()).arg(deadLabels.size()));
}

void DbMaintenanceDialog::relocateSelected() {
    const QStringList dirs = selectedDirs();
    if (dirs.size() != 1) {
        QMessageBox::information(this, gazeTr("重新定位"),
            gazeTr("请选中恰好一行目录。"));
        return;
    }
    const QString oldDir = dirs.first();
    bool ok = false;
    const QString input = QInputDialog::getText(this, gazeTr("重新定位"),
        gazeTr("把目录 %1 的缓存记录搬到新路径:").arg(oldDir),
        QLineEdit::Normal, oldDir, &ok);
    if (!ok) return;
    QString newDir = QDir::fromNativeSeparators(input.trimmed());
    if (!newDir.isEmpty() && !newDir.endsWith('/')) newDir += '/';
    // 搬移实现是"复制到新键再删旧键",新路径在原路径之下会自吞刚搬入的记录。
    // 比较必须大小写不敏感:Windows 路径同义,而键是 BINARY 排序、删除的 LIKE
    // 又不分大小写 —— 仅大小写不同的"新路径"插入后随即被整批删除
    if (newDir.isEmpty()
        || newDir.compare(oldDir, Qt::CaseInsensitive) == 0
        || newDir.startsWith(oldDir, Qt::CaseInsensitive)) {
        QMessageBox::warning(this, gazeTr("重新定位"),
            gazeTr("新路径不能与原路径相同或位于原路径之下。"));
        return;
    }
    if (QMessageBox::question(this, gazeTr("重新定位"),
        gazeTr("把 %1 下的缓存记录改挂到 %2?\n(只改数据库记录,不搬动任何文件)")
            .arg(oldDir).arg(newDir))
        != QMessageBox::Yes) return;

    QSqlDatabase d = maintenanceDb();
    if (!d.transaction()) return;
    QSqlQuery q(d);
    // thumbs:键=路径|尺寸|后缀,整串前缀替换;撞新键时 REPLACE 保留新档
    q.prepare("INSERT OR REPLACE INTO thumbs(key,png,mtime,atime) "
              "SELECT ?||substr(key,?),png,mtime,atime FROM thumbs "
              "WHERE key LIKE ? ESCAPE '\\'");
    q.addBindValue(newDir);
    q.addBindValue(oldDir.size() + 1);
    q.addBindValue(likePrefixPattern(oldDir));
    q.exec();
    q.prepare("DELETE FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
    q.addBindValue(likePrefixPattern(oldDir));
    q.exec();
    // labels:path 即媒体路径;UPDATE 撞主键时 REPLACE 落新删旧
    q.prepare("UPDATE OR REPLACE labels SET path = ?||substr(path,?) "
              "WHERE path LIKE ? ESCAPE '\\'");
    q.addBindValue(newDir);
    q.addBindValue(oldDir.size() + 1);
    q.addBindValue(likePrefixPattern(oldDir));
    q.exec();
    // dirsize 两种落盘形态分别搬:前缀形(带尾斜杠子目录)+ 精确形(裸目录本身)
    q.prepare("INSERT OR REPLACE INTO dirsize(path,size,basis,computed) "
              "SELECT ?||substr(path,?),size,basis,computed FROM dirsize "
              "WHERE path LIKE ? ESCAPE '\\'");
    q.addBindValue(newDir);
    q.addBindValue(oldDir.size() + 1);
    q.addBindValue(likePrefixPattern(oldDir));
    q.exec();
    const QString oldBare = oldDir.left(oldDir.size() - 1);
    q.prepare("UPDATE OR REPLACE dirsize SET path = ?||substr(path,?) "
              "WHERE path = ?");
    q.addBindValue(newDir);
    q.addBindValue(oldBare.size() + 1);
    q.addBindValue(oldBare);
    q.exec();
    q.prepare("DELETE FROM dirsize WHERE path LIKE ? ESCAPE '\\'");
    q.addBindValue(likePrefixPattern(oldDir));
    q.exec();
    q.prepare("DELETE FROM dirsize WHERE path = ?");
    q.addBindValue(oldBare);
    q.exec();
    if (!d.commit()) d.rollback();

    reload();
}

void DbMaintenanceDialog::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(this, gazeTr("导出清单"),
        QStringLiteral("db_inventory.csv"), gazeTr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, gazeTr("导出清单"), gazeTr("无法写入文件。"));
        return;
    }
    QTextStream ts(&f);
    ts.setGenerateByteOrderMark(true);   // BOM:Excel 双击直开不乱码
    auto csvField = [](QString s) {
        s.replace('"', QLatin1String("\"\""));
        return '"' + s + '"';
    };
    ts << csvField(gazeTr("缓存目录")) << ',' << csvField(gazeTr("文件数")) << ','
       << csvField(gazeTr("标记")) << ',' << csvField(gazeTr("缩略图")) << ','
       << csvField(gazeTr("体积(MB)")) << "\r\n";
    QList<QString> dirs = m_byDir.keys();
    std::sort(dirs.begin(), dirs.end());
    for (const QString& dir : dirs) {
        const DirStat& rec = m_byDir[dir];
        ts << csvField(dir) << ',' << rec.files.size() << ',' << rec.labels
           << ',' << rec.thumbs << ','
           << QString::asprintf("%.2f", rec.bytes / 1024.0 / 1024.0) << "\r\n";
    }
}

void DbMaintenanceDialog::deleteAll() {
    if (QMessageBox::question(this, gazeTr("删除全部"),
        gazeTr("确认清空全部缩略图缓存?(浏览时会自动重建)"))
        != QMessageBox::Yes) return;
    QSqlQuery q(maintenanceDb());
    q.exec("DELETE FROM thumbs");
    reload();
}

void DbMaintenanceDialog::rebuildThumbs() {
    if (QMessageBox::question(this, gazeTr("重建缩略图"),
        gazeTr("清空缓存后,下次浏览文件夹时将按当前设置自动重建缩略图。继续?"))
        != QMessageBox::Yes) return;
    QSqlQuery q(maintenanceDb());
    q.exec("DELETE FROM thumbs");
    reload();
}

// ═══════════════════════════════════════════
// 文件夹大小缓存维护(2026-09-05 用户令):dirsize 表逐条列出
// ═══════════════════════════════════════════
void DirSizeMaintenanceDialog::reload() {
    QSqlDatabase d = maintenanceDb();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    qint64 totalBytes = 0;
    QSqlQuery q(d);
    int row = 0;
    if (q.exec("SELECT path, size, computed, basis FROM dirsize ORDER BY path")) {
        while (q.next()) {
            const QString path = q.value(0).toString();
            const qint64 bytes = q.value(1).toLongLong();
            const qint64 ms    = q.value(2).toLongLong();
            const QString basis = q.value(3).toString();
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            m_table->setItem(r, 0, new QTableWidgetItem(path));
            m_table->setItem(r, 1, new BytesItem(bytes));
            m_table->setItem(r, 2, new QTableWidgetItem(
                QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy/M/d HH:mm")));
            // 失效键 = mtime|子项数|字节和:首段是 mtime 毫秒,细节给 tooltip
            auto* basisIt = new QTableWidgetItem(basis.section('|', 0, 0));
            basisIt->setToolTip(basis);
            m_table->setItem(r, 3, basisIt);
            totalBytes += bytes;
            ++row;
        }
    }
    QFileInfo fi(d.databaseName());
    m_summary->setText(gazeTr(
        "数据库:%1  ·  缓存目录:%2  ·  合计:%3")
        .arg(fi.fileName() + QString(" (%1 MB)").arg(fi.size() / 1024 / 1024))
        .arg(row)
        .arg(QString::asprintf("%.2f MB", totalBytes / 1024.0 / 1024.0)));
    m_table->setSortingEnabled(true);
}

QStringList DirSizeMaintenanceDialog::selectedPaths() const {
    QStringList out;
    const auto rows = m_table->selectionModel()->selectedRows(0);
    for (const auto& idx : rows) out << idx.data().toString();
    return out;
}

DirSizeMaintenanceDialog::DirSizeMaintenanceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(gazeTr("文件夹大小数据库维护"));
    resize(760, 540);
    setObjectName(QStringLiteral("dbMaintDialog"));   // 与缩略图维护同一套样式

    auto* root = new QVBoxLayout(this);
    m_summary = new QLabel;
    root->addWidget(m_summary);

    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({ gazeTr("目录"), gazeTr("缓存大小"),
                                         gazeTr("记账时间"), gazeTr("失效键(mtime)") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    root->addWidget(m_table, 1);

    auto* bar = new QHBoxLayout;
    auto* delSel = new QPushButton(gazeTr("删除选中"));
    auto* delAll = new QPushButton(gazeTr("清空"));
    auto* sync   = new QPushButton(gazeTr("同步(移除已不存在的目录)"));
    bar->addWidget(delSel);
    bar->addWidget(delAll);
    bar->addStretch(1);
    bar->addWidget(sync);
    root->addLayout(bar);

    connect(delSel, &QPushButton::clicked, this, [this]() {
        const QStringList sel = selectedPaths();
        if (sel.isEmpty()) return;
        QSqlDatabase d = maintenanceDb();
        d.transaction();
        QSqlQuery del(d);
        del.prepare("DELETE FROM dirsize WHERE path = ?");
        for (const QString& p : sel) { del.addBindValue(p); del.exec(); }
        d.commit();
        reload();
    });
    connect(delAll, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, gazeTr("清空"),
                gazeTr("清空全部文件夹大小缓存?(选中目录时会自动重新统计)"))
            != QMessageBox::Yes) return;
        QSqlQuery q(maintenanceDb());
        q.exec("DELETE FROM dirsize");
        reload();
    });
    connect(sync, &QPushButton::clicked, this, [this]() {
        QSqlDatabase d = maintenanceDb();
        QStringList gone;
        QSqlQuery q(d);
        if (q.exec("SELECT path FROM dirsize")) {
            while (q.next()) {
                const QString p = q.value(0).toString();
                if (!QFileInfo::exists(p)) gone << p;
            }
        }
        d.transaction();
        QSqlQuery del(d);
        del.prepare("DELETE FROM dirsize WHERE path = ?");
        for (const QString& p : gone) { del.addBindValue(p); del.exec(); }
        d.commit();
        QMessageBox::information(this, gazeTr("同步"),
            gazeTr("已移除 %1 条失效目录缓存。").arg(gone.size()));
        reload();
    });

    reload();
}
