#include "settings.h"
#include "logger.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>

AppSettings& AppSettings::instance() {
    static AppSettings s;
    return s;
}

// ── Integration/iniLocation(设置→系统集成→配置文件)──
//   0 程序文件夹(便携,默认) 1 系统文件夹 %APPDATA% 2 自定义目录
// 引导问题:选 1/2 时"该去哪读"本身也存不进远端文件,只能先读 exe 目录那份
// 便携 ini 拿到这两个键,再决定主配置落到哪。因此 exe 目录会保留一个只含
// Integration/* 的小引导文件,其余设置全部进目标位置。
// #122 补的一块:换位置时把**当前这份**配置整体拷到目标(目标已存在则不动)。
//   少了这一步,用户从便携切到 %APPDATA% 后看到的是"所有设置回到默认"——
//   值其实还在旧文件里,只是没人再读它。观感等同于设置被清空。
// 2026-09-08 卡死事故修复:QSettings 的 ini 后端默认要"原子同步"——读写都要
// 抢 ini 同目录的 Gaze.ini.lock(QLockFile)。实测:只要有一个实例持有这把锁
// (卡死、被强杀留下的陈旧锁也算),其余实例会卡在 QSettings::value() 里无限
// 等待 —— 表象是启动后白屏、GUI 线程再无心跳、程序"再也起不来"。
// 栈证据:GUI 线程 事件循环 → 绘制代码(读 Browser/previewBackColor 等)
//        → QSettings::value → Qt6Core 信号量 → ntdll 等待,永不返回。
// Gaze 是单用户便携应用,配置读写只在进程内串行,并不需要跨进程原子同步:
// 关掉它,QSettings 不再碰锁文件,这类死锁从根上消失。
static void disableAtomicSync(QSettings& s) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    s.setAtomicSyncRequired(false);
#else
    Q_UNUSED(s);
#endif
}

static QString pathForLocation(int loc, const QString& customDir) {
    const QString portable = QCoreApplication::applicationDirPath() + "/Gaze.ini";
    if (loc == 1) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return dir.isEmpty() ? portable : dir + "/Gaze.ini";
    }
    if (loc == 2 && !customDir.trimmed().isEmpty())
        return QDir::fromNativeSeparators(customDir.trimmed()) + "/Gaze.ini";
    return portable;
}

static QString resolveIniPath() {
    const QString portable = QCoreApplication::applicationDirPath() + "/Gaze.ini";
    QSettings boot(portable, QSettings::IniFormat);
    disableAtomicSync(boot);
    const int loc = boot.value("Integration/iniLocation", 0).toInt();
    const QString target = pathForLocation(
        loc, boot.value("Integration/customIniDir").toString());
    if (target != portable) {
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFileInfo::exists(target) && QFileInfo::exists(portable))
            QFile::copy(portable, target);
    }
    return target;
}

AppSettings::AppSettings()
    : m_settings(resolveIniPath(), QSettings::IniFormat)
{
    disableAtomicSync(m_settings);
}

void AppSettings::set(const QString& key, const QVariant& v) {
    setPersist(key, v);
    emit changed();
}

// 只落值不广播:这些键的唯一读者是下次启动(MainWindow 启动块),
// 广播会让每换一个文件都重跑外观/标题/预览重绘(实测 4.2~4.8 ms/次)
//
// Integration/* 是"配置文件在哪"的引导键:它们必须同时存在于 exe 目录的
// 便携 ini 里,否则下次启动 resolveIniPath() 读不到,搬家会静默失效。
// 主配置被搬到别处时,这三个键的每次写入都同步镜像回引导文件
void AppSettings::setPersist(const QString& key, const QVariant& v) {
    m_settings.setValue(key, v);
    const QString boot = QCoreApplication::applicationDirPath() + "/Gaze.ini";
    if (key.startsWith(QStringLiteral("Integration/"))
        && m_settings.fileName() != boot) {
        QSettings b(boot, QSettings::IniFormat);
        disableAtomicSync(b);
        b.setValue(key, v);
    }
    // #122:换配置文件位置的当下就把值带过去(生效仍是下次启动,但数据不落下)。
    // 只在这一个键上动手,且目标已存在就不覆盖 —— 不覆盖是"别吃掉已有配置"。
    if (key == QStringLiteral("Integration/iniLocation")
        || key == QStringLiteral("Integration/customIniDir")) {
        QSettings b(boot, QSettings::IniFormat);
        disableAtomicSync(b);
        const int loc = b.value("Integration/iniLocation", 0).toInt();
        const QString target = pathForLocation(
            loc, b.value("Integration/customIniDir").toString());
        const QString cur = m_settings.fileName();
        if (target != cur) {
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!QFile::exists(target))
                QFile::copy(cur, target);
        }
    }
}

QVariant AppSettings::get(const QString& key, const QVariant& def) const {
    return m_settings.value(key, def);
}

QString AppSettings::iniPath() const {
    return m_settings.fileName();
}

QString AppSettings::iniPathForLocation(int loc, const QString& customDir) {
    return pathForLocation(loc, customDir);
}

QString AppSettings::dataDir() const {
    return QFileInfo(m_settings.fileName()).absolutePath();
}

void AppSettings::clearAll() {
    // 恢复默认=整份 ini 清空,必须留痕:配置"截断"类报告的第一物证
    Logger::event(QStringLiteral("settings clearAll: ini wiped (%1)")
                      .arg(m_settings.fileName()));
    m_settings.clear();   // 清空后 get() 返回代码内默认值(= 用户配置清单)
    m_settings.sync();
    // 必须和 set() 一样广播:否则"恢复默认"之后网格/树/标题/预览/缩略图
    // 全部还挂着旧值,只有重启才恢复(五个 changed() 订阅者都收不到通知)
    emit changed();
}
