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

// ═══ 格式标签颜色(ini "LabelColors/*";FileCard 文件名底色) ═══
// 存储:LabelColors/map = "gif:#8F7D00;mp4:#B25E00;..."(ext 不带点,小写)
static QString labelColorsKey() { return QStringLiteral("LabelColors/map"); }

static QHash<QString, QColor> loadLabelColors() {
    QHash<QString, QColor> m;
    const QString s = AppSettings::instance().get(labelColorsKey(), QString()).toString();
    if (s.isEmpty()) {
        // 首次使用:内置默认(gif 黄绿 / 视频组橙红),与旧版硬编码一致
        m.insert("gif", QColor("#8F7D00"));
        for (const QString& e : VIDEO_EXTS)
            m.insert(e.mid(1).toLower(), QColor("#B25E00"));
        return m;
    }
    for (const QString& pair : s.split(';', Qt::SkipEmptyParts)) {
        int c = pair.indexOf(':');
        if (c <= 0) continue;
        QColor col(pair.mid(c + 1));
        if (col.isValid()) m.insert(pair.left(c).toLower(), col);
    }
    return m;
}

static void saveLabelColors(const QHash<QString, QColor>& m) {
    QStringList parts;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        parts << it.key() + ":" + it.value().name(QColor::HexRgb);
    parts.sort();
    AppSettings::instance().set(labelColorsKey(), parts.join(';'));
}

QColor LabelColors::colorForExt(const QString& extNoDot) {
    return loadLabelColors().value(extNoDot.toLower(), fallbackColor());
}

QColor LabelColors::fallbackColor() {
    return QColor(AppSettings::instance().get(
        "LabelColors/fallback", QStringLiteral("#191919")).toString());
}

void LabelColors::setFallbackColor(const QColor& c) {
    AppSettings::instance().set("LabelColors/fallback", c.name(QColor::HexRgb));
}

QList<QPair<QString, QColor>> LabelColors::all() {
    QHash<QString, QColor> m = loadLabelColors();
    QList<QPair<QString, QColor>> out;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        out << qMakePair(it.key(), it.value());
    std::sort(out.begin(), out.end(),
              [](const QPair<QString, QColor>& a, const QPair<QString, QColor>& b) {
                  return a.first < b.first;
              });
    return out;
}

void LabelColors::set(const QString& extNoDot, const QColor& c) {
    QHash<QString, QColor> m = loadLabelColors();
    m.insert(extNoDot.toLower(), c);
    saveLabelColors(m);
}

void LabelColors::remove(const QString& extNoDot) {
    QHash<QString, QColor> m = loadLabelColors();
    m.remove(extNoDot.toLower());
    saveLabelColors(m);
}

bool LabelColors::enabled() {
    return AppSettings::instance().get("Appearance/formatColor", true).toBool();
}
