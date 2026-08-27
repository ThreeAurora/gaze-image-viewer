#include "labelstore.h"
#include "settings.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QCoreApplication>
#include <QVariant>
#include <QAlgorithm>

LabelStore& LabelStore::instance() {
    static LabelStore inst;
    return inst;
}

LabelStore::LabelStore() = default;

QSqlDatabase LabelStore::db() {
    const QString conn = QStringLiteral("label_db");
    if (!QSqlDatabase::contains(conn)) {
        QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", conn);
        d.setDatabaseName(QCoreApplication::applicationDirPath() + "/thumbnails.db");
        d.open();
        QSqlQuery q(d);
        q.exec("CREATE TABLE IF NOT EXISTS labels "
               "(path TEXT PRIMARY KEY, color INTEGER NOT NULL)");
    }
    return QSqlDatabase::database(conn);
}

void LabelStore::setColor(const QString& path, int color) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return;
    QSqlQuery q(d);
    if (color <= 0) {
        q.prepare("DELETE FROM labels WHERE path = ?");
        q.addBindValue(path);
        q.exec();
    } else {
        q.prepare("INSERT OR REPLACE INTO labels(path, color) VALUES(?, ?)");
        q.addBindValue(path);
        q.addBindValue(color);
        q.exec();
    }
}

int LabelStore::colorFor(const QString& path) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return 0;
    QSqlQuery q(d);
    q.prepare("SELECT color FROM labels WHERE path = ?");
    q.addBindValue(path);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

QHash<QString,int> LabelStore::colorsForDir(const QString& dir) {
    QHash<QString,int> out;
    QSqlDatabase d = db();
    if (!d.isOpen()) return out;
    QString prefix = dir;
    if (!prefix.endsWith('/') && !prefix.endsWith('\\')) prefix += '/';
    QSqlQuery q(d);
    q.prepare("SELECT path, color FROM labels WHERE path LIKE ? || '%' AND color > 0");
    q.addBindValue(prefix);
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toString(), q.value(1).toInt());
    }
    return out;
}

void LabelStore::removePaths(const QStringList& paths) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return;
    QSqlQuery q(d);
    q.prepare("DELETE FROM labels WHERE path = ?");
    for (const auto& p : paths) { q.addBindValue(p); q.exec(); }
}

QColor LabelStore::colorValue(int color) {
    switch (color) {
    case 1: return QColor("#E53935"); // 红
    case 2: return QColor("#FB8C00"); // 橙
    case 3: return QColor("#FDD835"); // 黄
    case 4: return QColor("#43A047"); // 绿
    case 5: return QColor("#1E88E5"); // 蓝
    default: return QColor();
    }
}
