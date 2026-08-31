#pragma once
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include "settings.h"   // #106:shellMenu 读点统一走 AppSettings,别再自己拼 ini 路径
#include "settings.h"   // #106:shellMenu 读点统一走 AppSettings,别再自己拼 ini 路径

// 系统集成:资源管理器右键菜单 / 打开方式注册(HKCU,免管理员)
namespace Integration {

inline QString exeNative() {
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// "用 xnnview 浏览"加入目录右键菜单(目录背景 + 目录节点)
inline bool addBrowseContextMenu() {
    const QString exe = exeNative();
    QSettings r1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\XnnviewBrowse",
                 QSettings::NativeFormat);
    r1.setValue(".", QString::fromUtf8("用 xnnview 浏览"));
    r1.setValue("Icon", exe);
    QSettings c1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\XnnviewBrowse\\command",
                 QSettings::NativeFormat);
    c1.setValue(".", "\"" + exe + "\" \"%1\"");

    QSettings r2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\XnnviewBrowse",
                 QSettings::NativeFormat);
    r2.setValue(".", QString::fromUtf8("用 xnnview 浏览"));
    r2.setValue("Icon", exe);
    QSettings c2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\XnnviewBrowse\\command",
                 QSettings::NativeFormat);
    c2.setValue(".", "\"" + exe + "\" \"%V\"");
    return r1.status() == QSettings::NoError && r2.status() == QSettings::NoError;
}

inline bool removeBrowseContextMenu() {
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\XnnviewBrowse",
              QSettings::NativeFormat).remove("");
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\XnnviewBrowse",
              QSettings::NativeFormat).remove("");
    return true;
}

// 加入"打开方式"应用列表(HKCU\...\Applications\xnnview.exe)
inline bool registerOpenWith() {
    const QString exe = exeNative();
    QSettings r("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\xnnview.exe",
                QSettings::NativeFormat);
    r.setValue(".", QString::fromUtf8("xnnview 图片浏览器"));
    r.setValue("FriendlyAppName", QString::fromUtf8("xnnview"));
    QSettings c("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\xnnview.exe\\shell\\open\\command",
                QSettings::NativeFormat);
    c.setValue(".", "\"" + exe + "\" \"%1\"");
    return r.status() == QSettings::NoError;
}

inline bool isBrowseMenuInstalled() {
    QSettings r("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\XnnviewBrowse",
                QSettings::NativeFormat);
    return r.contains(".");
}

} // namespace Integration
