#include "labelstore.h"
#include "settings.h"
#include "constants.h"
#include "dbprefix.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QCoreApplication>
#include <QDir>
#include <QVariant>
#include <QFileInfo>
#include <algorithm>

LabelStore& LabelStore::instance() {
    static LabelStore inst;
    return inst;
}

LabelStore::LabelStore() = default;

QSqlDatabase LabelStore::db() {
    const QString conn = QStringLiteral("label_db");
    if (!QSqlDatabase::contains(conn)) {
        QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", conn);
        d.setDatabaseName(AppSettings::instance().dataDir() + "/thumbnails.db");
        d.open();
        QSqlQuery q(d);
        q.exec("CREATE TABLE IF NOT EXISTS labels "
               "(path TEXT PRIMARY KEY, color INTEGER NOT NULL)");
        // 历史数据迁移:旧版扫描产出混合分隔符路径(E:/dir\file),统一为 '/';
        // Windows 文件名不可能含 '\',幂等且安全
        q.exec("UPDATE labels SET path = REPLACE(path, '\\', '/')");
    }
    return QSqlDatabase::database(conn);
}

void LabelStore::setColor(const QString& path, int color) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return;
    QSqlQuery q(d);
    const QString key = QDir::fromNativeSeparators(path);
    if (color <= 0) {
        q.prepare("DELETE FROM labels WHERE path = ?");
        q.addBindValue(key);
        q.exec();
    } else {
        q.prepare("INSERT OR REPLACE INTO labels(path, color) VALUES(?, ?)");
        q.addBindValue(key);
        q.addBindValue(color);
        q.exec();
    }
}

int LabelStore::colorFor(const QString& path) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return 0;
    QSqlQuery q(d);
    q.prepare("SELECT color FROM labels WHERE path = ?");
    q.addBindValue(QDir::fromNativeSeparators(path));
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

QHash<QString,int> LabelStore::colorsForDir(const QString& dir) {
    QHash<QString,int> out;
    QSqlDatabase d = db();
    if (!d.isOpen()) return out;
    QString prefix = QDir::fromNativeSeparators(dir);
    if (!prefix.endsWith('/') && !prefix.endsWith('\\')) prefix += '/';
    QSqlQuery q(d);
    q.prepare("SELECT path, color FROM labels WHERE path LIKE ? ESCAPE '\\' AND color > 0");
    q.addBindValue(likePrefixPattern(prefix));
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
    for (const auto& p : paths) {
        q.addBindValue(QDir::fromNativeSeparators(p));
        q.exec();
    }
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
// ⚠ 性能:colorForExt 在每张卡片 setup 时调用(快速滚动一屏 20+ 次),
//   绝不允许每次读 ini——进程内缓存,仅修改时落盘(perf.log 实锤过 926ms 布局)
namespace {
struct LCCache {
    QHash<QString, QColor> map;
    QColor fallback = QColor("#191919");
    bool fallbackExplicit = false;   // 用户在颜色编辑器显式设过默认色则双主题都用它
    bool enabled = true;
    bool loaded = false;
};
LCCache& lc() { static LCCache c; return c; }
void ensureLoaded() {
    if (lc().loaded) return;
    lc().loaded = true;
    // 空默认探测:ini 无此键=未显式设置,查询时随主题取值(#248 浅色下名字条
    // 不再是黑底);有键=用户显式选的颜色,深浅两主题都尊重
    const QString fb = AppSettings::instance().get(
        "LabelColors/fallback", QString()).toString();
    lc().fallbackExplicit = !fb.isEmpty();
    if (lc().fallbackExplicit)
        lc().fallback = QColor(fb);
    lc().enabled = AppSettings::instance().get("Appearance/formatColor", true).toBool();
    const QString s = AppSettings::instance().get(
        QStringLiteral("LabelColors/map"), QString()).toString();
    if (s.isEmpty()) {
        // 首次使用:内置默认(gif 黄绿 / 视频组橙红),与旧版硬编码一致
        lc().map.insert("gif", QColor("#8F7D00"));
        for (const QString& e : VIDEO_EXTS)
            lc().map.insert(e.mid(1).toLower(), QColor("#B25E00"));
        return;
    }
    for (const QString& pair : s.split(';', Qt::SkipEmptyParts)) {
        int c = pair.indexOf(':');
        if (c <= 0) continue;
        QColor col(pair.mid(c + 1));
        if (col.isValid()) lc().map.insert(pair.left(c).toLower(), col);
    }
}
static void saveLabelColors() {
    QStringList parts;
    for (auto it = lc().map.constBegin(); it != lc().map.constEnd(); ++it)
        parts << it.key() + ":" + it.value().name(QColor::HexRgb);
    parts.sort();
    AppSettings::instance().set(QStringLiteral("LabelColors/map"), parts.join(';'));
}
} // namespace

QColor LabelColors::colorForExt(const QString& extNoDot) {
    ensureLoaded();
    const QString k = extNoDot.toLower();
    // 未显式设置时随主题走:深色保持已验收的 #191919,浅色给白底(与卡片底同色,
    // 名字条自然消隐);绘制期求值,切换主题即查即变,无需注册刷新钩子
    if (!lc().fallbackExplicit && !lc().map.contains(k))
        return QColor(Theme::T("#191919", "#FFFFFF"));
    return lc().map.value(k, lc().fallback);
}

QColor LabelColors::fallbackColor() {
    ensureLoaded();
    return lc().fallbackExplicit ? lc().fallback
                                 : QColor(Theme::T("#191919", "#FFFFFF"));
}

void LabelColors::setFallbackColor(const QColor& c) {
    ensureLoaded();
    lc().fallback = c;
    lc().fallbackExplicit = true;
    AppSettings::instance().set("LabelColors/fallback", c.name(QColor::HexRgb));
}

QList<QPair<QString, QColor>> LabelColors::all() {
    ensureLoaded();
    QList<QPair<QString, QColor>> out;
    for (auto it = lc().map.constBegin(); it != lc().map.constEnd(); ++it)
        out << qMakePair(it.key(), it.value());
    std::sort(out.begin(), out.end(),
              [](const QPair<QString, QColor>& a, const QPair<QString, QColor>& b) {
                  return a.first < b.first;
              });
    return out;
}

void LabelColors::set(const QString& extNoDot, const QColor& c) {
    ensureLoaded();
    lc().map.insert(extNoDot.toLower(), c);
    saveLabelColors();
}

void LabelColors::remove(const QString& extNoDot) {
    ensureLoaded();
    lc().map.remove(extNoDot.toLower());
    saveLabelColors();
}

bool LabelColors::enabled() {
    ensureLoaded();
    return lc().enabled;
}

void LabelColors::setEnabled(bool on) {
    ensureLoaded();
    lc().enabled = on;
    AppSettings::instance().set("Appearance/formatColor", on);
}

// 设置页改了 Appearance/formatColor / LabelColors/* 后调用,下次查询重读 ini。
// colorForExt 是逐卡片热路径,这里只清脏标记、不做磁盘 IO,读回由下一次查询顺带完成
void LabelColors::reload() {
    lc().loaded = false;
}
