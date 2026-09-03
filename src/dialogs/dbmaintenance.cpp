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
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QHeaderView>

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
    setStyleSheet(
        QString::fromUtf8("QDialog{background:%1;}"
        "QLabel{color:%2;background:transparent;}"
        "QTableWidget{background:%1;color:%2;border:1px solid %3;}"
        "QHeaderView::section{background:%1;color:%2;border:none;padding:4px;}"
        "QPushButton{background:%4;color:%2;border:1px solid %3;"
        "padding:5px 14px;border-radius:4px;}"
        "QPushButton:hover{border-color:%5;}").arg(C_CONTENT, C_TEXT, C_SEPARATOR, C_TOOLBAR, C_ACCENT));

    auto* root = new QVBoxLayout(this);

    m_summary = new QLabel;
    root->addWidget(m_summary);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({gazeTr("缓存目录"),
        gazeTr("文件数"), gazeTr("缩略图体积")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto* btns = new QHBoxLayout;
    auto addBtn = [this, &btns](const QString& txt, auto slot) {
        auto* b = new QPushButton(txt);
        connect(b, &QPushButton::clicked, this, slot);
        btns->addWidget(b);
    };
    addBtn(gazeTr("删除选中目录条目"), [this]() {
        auto sel = m_table->selectedItems();
        if (sel.isEmpty()) return;
        QString dir = m_table->item(sel.first()->row(), 0)->text();
        // 与"删除全部""重建缩略图"对齐:破坏性动作一律先问一句
        //(空格/回车误触这个按钮时,过去是静默 DELETE)
        if (QMessageBox::question(this, gazeTr("删除条目"),
            gazeTr("删除该目录的全部缩略图缓存条目?\n%1\n(浏览时会自动重建)").arg(dir))
            != QMessageBox::Yes) return;
        QSqlDatabase d = maintenanceDb();
        QSqlQuery q(d);
        q.prepare("DELETE FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        reload();
    });
    addBtn(gazeTr("重建缩略图"), [this]() { rebuildThumbs(); });
    addBtn(gazeTr("删除全部"), [this]() { deleteAll(); });
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
    m_allPaths.clear();
    m_byDir.clear();
    qint64 totalBytes = 0;
    int total = 0;

    QSqlQuery q(d);
    if (q.exec("SELECT key, LENGTH(png) FROM thumbs")) {
        while (q.next()) {
            QString p = q.value(0).toString();
            qint64 bytes = q.value(1).toLongLong();
            m_allPaths << p;
            totalBytes += bytes;
            ++total;
            // 目录提取(最后一个 '/' 前)
            int slash = p.lastIndexOf('/');
            if (slash < 0) slash = p.lastIndexOf('\\');
            QString dir = slash > 0 ? p.left(slash + 1) : p;
            auto& rec = m_byDir[dir];
            rec.first += 1;
            rec.second += bytes;
        }
    }

    // 数据库文件大小
    QFileInfo fi(d.databaseName());
    qint64 dbSize = fi.size();
    m_summary->setText(QString::fromUtf8(
        "数据库:%1  ·  缓存条目:%2  ·  缩略图合计:%3")
        .arg(fi.fileName() + QString(" (%1 MB)").arg(dbSize / 1024 / 1024))
        .arg(total)
        .arg(QString::asprintf("%.2f MB", totalBytes / 1024.0 / 1024.0)));

    // 按体积降序填表
    QVector<QPair<QString, QPair<int, qint64>>> rows;
    for (auto it = m_byDir.constBegin(); it != m_byDir.constEnd(); ++it)
        rows.append({it.key(), {it.value().first, it.value().second}});
    std::sort(rows.begin(), rows.end(),
              [](auto& a, auto& b) { return a.second.second > b.second.second; });

    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(rows[i].first));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(rows[i].second.first)));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString::asprintf("%.2f MB", rows[i].second.second / 1024.0 / 1024.0)));
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
