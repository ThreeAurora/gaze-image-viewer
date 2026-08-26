#include "settings.h"
#include <QCoreApplication>
#include <QDir>

AppSettings& AppSettings::instance() {
    static AppSettings s;
    return s;
}

AppSettings::AppSettings()
    : m_settings(QCoreApplication::applicationDirPath() + "/gaze.ini",
                 QSettings::IniFormat)
{}

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

void AppSettings::set(const QString& key, const QVariant& v) {
    m_settings.setValue(key, v);
    emit changed();
}

QString AppSettings::iniPath() const {
    return m_settings.fileName();
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
