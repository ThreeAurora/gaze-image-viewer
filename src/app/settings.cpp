#include "settings.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

AppSettings& AppSettings::instance() {
    static AppSettings s;
    return s;
}

// ── Integration/iniLocation(设置→系统集成→配置文件)──
//   0 程序文件夹(便携,默认) 1 系统文件夹 %APPDATA% 2 自定义目录
// 引导问题:选 1/2 时"该去哪读"本身也存不进远端文件,只能先读 exe 目录那份
// 便携 ini 拿到这两个键,再决定主配置落到哪。因此 exe 目录会保留一个只含
// Integration/* 的小引导文件,其余设置全部进目标位置。
static QString resolveIniPath() {
    const QString portable = QCoreApplication::applicationDirPath() + "/gaze.ini";
    QSettings boot(portable, QSettings::IniFormat);
    const int loc = boot.value("Integration/iniLocation", 0).toInt();
    if (loc == 1) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        return dir + "/gaze.ini";
    }
    if (loc == 2) {
        const QString dir = boot.value("Integration/customIniDir").toString().trimmed();
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            return QDir::fromNativeSeparators(dir) + "/gaze.ini";
        }
    }
    return portable;
}

AppSettings::AppSettings()
    : m_settings(resolveIniPath(), QSettings::IniFormat)
{}

// Integration/* 是"配置文件在哪"的引导键:它们必须同时存在于 exe 目录的
// 便携 ini 里,否则下次启动 resolveIniPath() 读不到,搬家会静默失效。
// 主配置被搬到别处时,这三个键的每次写入都同步镜像回引导文件
void AppSettings::set(const QString& key, const QVariant& v) {
    m_settings.setValue(key, v);
    const QString boot = QCoreApplication::applicationDirPath() + "/gaze.ini";
    if (key.startsWith(QStringLiteral("Integration/"))
        && m_settings.fileName() != boot) {
        QSettings b(boot, QSettings::IniFormat);
        b.setValue(key, v);
    }
    emit changed();
}

bool AppSettings::livePhotoAutoPlay() const {
    return m_settings.value("livephoto/autoplay", true).toBool();
}
void AppSettings::setLivePhotoAutoPlay(bool on) {
    m_settings.setValue("livephoto/autoplay", on);
}

int AppSettings::thumbnailSize() const {
    return m_settings.value("ui/thumbnail_size", 160).toInt();
}
void AppSettings::setThumbnailSize(int size) {
    m_settings.setValue("ui/thumbnail_size", size);
}

int AppSettings::startupMode() const {
    return m_settings.value("startup/mode", 0).toInt();
}
void AppSettings::setStartupMode(int mode) {
    m_settings.setValue("startup/mode", mode);
}

QString AppSettings::startupPath() const {
    return m_settings.value("startup/path", "").toString();
}
void AppSettings::setStartupPath(const QString& path) {
    m_settings.setValue("startup/path", path);
}

int AppSettings::theme() const {
    return m_settings.value("ui/theme", 0).toInt();
}
void AppSettings::setTheme(int t) {
    m_settings.setValue("ui/theme", t);
}

bool AppSettings::videoAutoPlay() const {
    return m_settings.value("video/autoplay", false).toBool();
}
void AppSettings::setVideoAutoPlay(bool on) {
    m_settings.setValue("video/autoplay", on);
}

QVariant AppSettings::get(const QString& key, const QVariant& def) const {
    return m_settings.value(key, def);
}

QString AppSettings::iniPath() const {
    return m_settings.fileName();
}

QString AppSettings::dataDir() const {
    return QFileInfo(m_settings.fileName()).absolutePath();
}

void AppSettings::clearAll() {
    m_settings.clear();   // 清空后 get() 返回代码内默认值(= 用户配置清单)
    m_settings.sync();
}

bool AppSettings::livePhotoAutoPlay() const {
    return m_settings.value("livephoto/autoplay", true).toBool();
}
void AppSettings::setLivePhotoAutoPlay(bool on) {
    m_settings.setValue("livephoto/autoplay", on);
}

int AppSettings::thumbnailSize() const {
    return m_settings.value("ui/thumbnail_size", 160).toInt();
}
void AppSettings::setThumbnailSize(int size) {
    m_settings.setValue("ui/thumbnail_size", size);
}

int AppSettings::startupMode() const {
    return m_settings.value("startup/mode", 0).toInt();
}
void AppSettings::setStartupMode(int mode) {
    m_settings.setValue("startup/mode", mode);
}

QString AppSettings::startupPath() const {
    return m_settings.value("startup/path", "").toString();
}
void AppSettings::setStartupPath(const QString& path) {
    m_settings.setValue("startup/path", path);
}

int AppSettings::theme() const {
    return m_settings.value("ui/theme", 0).toInt();
}
void AppSettings::setTheme(int t) {
    m_settings.setValue("ui/theme", t);
}

bool AppSettings::videoAutoPlay() const {
    return m_settings.value("video/autoplay", false).toBool();
}
void AppSettings::setVideoAutoPlay(bool on) {
    m_settings.setValue("video/autoplay", on);
}

bool AppSettings::livePhotoAutoPlay() const {
    return m_settings.value("livephoto/autoplay", true).toBool();
}
void AppSettings::setLivePhotoAutoPlay(bool on) {
    m_settings.setValue("livephoto/autoplay", on);
}

int AppSettings::thumbnailSize() const {
    return m_settings.value("ui/thumbnail_size", 160).toInt();
}
void AppSettings::setThumbnailSize(int size) {
    m_settings.setValue("ui/thumbnail_size", size);
}

int AppSettings::startupMode() const {
    return m_settings.value("startup/mode", 0).toInt();
}
void AppSettings::setStartupMode(int mode) {
    m_settings.setValue("startup/mode", mode);
}

QString AppSettings::startupPath() const {
    return m_settings.value("startup/path", "").toString();
}
void AppSettings::setStartupPath(const QString& path) {
    m_settings.setValue("startup/path", path);
}

int AppSettings::theme() const {
    return m_settings.value("ui/theme", 0).toInt();
}
void AppSettings::setTheme(int t) {
    m_settings.setValue("ui/theme", t);
}

bool AppSettings::videoAutoPlay() const {
    return m_settings.value("video/autoplay", false).toBool();
}
void AppSettings::setVideoAutoPlay(bool on) {
    m_settings.setValue("video/autoplay", on);
}

bool AppSettings::livePhotoAutoPlay() const {
    return m_settings.value("livephoto/autoplay", true).toBool();
}
void AppSettings::setLivePhotoAutoPlay(bool on) {
    m_settings.setValue("livephoto/autoplay", on);
}

int AppSettings::thumbnailSize() const {
    return m_settings.value("ui/thumbnail_size", 160).toInt();
}
void AppSettings::setThumbnailSize(int size) {
    m_settings.setValue("ui/thumbnail_size", size);
}

int AppSettings::startupMode() const {
    return m_settings.value("startup/mode", 0).toInt();
}
void AppSettings::setStartupMode(int mode) {
    m_settings.setValue("startup/mode", mode);
}

QString AppSettings::startupPath() const {
    return m_settings.value("startup/path", "").toString();
}
void AppSettings::setStartupPath(const QString& path) {
    m_settings.setValue("startup/path", path);
}

int AppSettings::theme() const {
    return m_settings.value("ui/theme", 0).toInt();
}
void AppSettings::setTheme(int t) {
    m_settings.setValue("ui/theme", t);
}

bool AppSettings::videoAutoPlay() const {
    return m_settings.value("video/autoplay", false).toBool();
}
void AppSettings::setVideoAutoPlay(bool on) {
    m_settings.setValue("video/autoplay", on);
}
