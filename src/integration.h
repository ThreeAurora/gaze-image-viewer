#pragma once
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

// 系统集成:资源管理器右键菜单 / 打开方式注册(HKCU,免管理员)
namespace Integration {

inline QString exeNative() {
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// "用 gaze 浏览"加入目录右键菜单(目录背景 + 目录节点)
inline bool addBrowseContextMenu() {
    const QString exe = exeNative();
    QSettings r1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse",
                 QSettings::NativeFormat);
    r1.setValue(".", QString::fromUtf8("用 gaze 浏览"));
    r1.setValue("Icon", exe);
    QSettings c1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse\\command",
                 QSettings::NativeFormat);
    c1.setValue(".", "\"" + exe + "\" \"%1\"");

    QSettings r2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse",
                 QSettings::NativeFormat);
    r2.setValue(".", QString::fromUtf8("用 gaze 浏览"));
    r2.setValue("Icon", exe);
    QSettings c2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse\\command",
                 QSettings::NativeFormat);
    c2.setValue(".", "\"" + exe + "\" \"%V\"");
    return r1.status() == QSettings::NoError && r2.status() == QSettings::NoError;
}

inline bool removeBrowseContextMenu() {
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse",
              QSettings::NativeFormat).remove("");
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse",
              QSettings::NativeFormat).remove("");
    return true;
}

// 加入"打开方式"应用列表(HKCU\...\Applications\gaze.exe)
inline bool registerOpenWith() {
    const QString exe = exeNative();
    QSettings r("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\gaze.exe",
                QSettings::NativeFormat);
    r.setValue(".", QString::fromUtf8("gaze 图片浏览器"));
    r.setValue("FriendlyAppName", QString::fromUtf8("gaze"));
    QSettings c("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\gaze.exe\\shell\\open\\command",
                QSettings::NativeFormat);
    c.setValue(".", "\"" + exe + "\" \"%1\"");
    return r.status() == QSettings::NoError;
}

inline bool isBrowseMenuInstalled() {
    QSettings r("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse",
                QSettings::NativeFormat);
    return r.contains(".");
}

} // namespace Integration
