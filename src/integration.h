#pragma once
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include "settings.h"   // #106:shellMenu 读点统一走 AppSettings,别再自己拼 ini 路径
#include "i18n.h"

// 系统集成:资源管理器右键菜单 / 打开方式注册(HKCU,免管理员)
namespace Integration {

inline QString exeNative() {
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// Integration/shellMenu:是否连"目录背景"(shell)一起注册。
// 关掉后只在目录/盘符节点上出现,资源管理器空白处右键不再有 Gaze。
inline bool shellMenuEnabled() {
    return AppSettings::instance().get("Integration/shellMenu", true).toBool();
}

// "用 Gaze 浏览"加入目录右键菜单(目录节点;shellMenu 开时再加目录背景)
inline bool addBrowseContextMenu() {
    const QString exe = exeNative();
    QSettings r1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse",
                 QSettings::NativeFormat);
    r1.setValue(".", gazeTr("用 Gaze 浏览"));
    r1.setValue("Icon", exe);
    QSettings c1("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse\\command",
                 QSettings::NativeFormat);
    c1.setValue(".", "\"" + exe + "\" \"%1\"");

    bool ok = r1.status() == QSettings::NoError;
    QSettings r2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse",
                 QSettings::NativeFormat);
    QSettings c2("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse\\command",
                 QSettings::NativeFormat);
    if (shellMenuEnabled()) {
        r2.setValue(".", gazeTr("用 Gaze 浏览"));
        r2.setValue("Icon", exe);
        c2.setValue(".", "\"" + exe + "\" \"%V\"");
        ok = ok && r2.status() == QSettings::NoError;
    } else {
        r2.remove("");   // 之前注册过就撤掉
    }
    return ok;
}

inline bool removeBrowseContextMenu() {
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\GazeBrowse",
              QSettings::NativeFormat).remove("");
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\Background\\shell\\GazeBrowse",
              QSettings::NativeFormat).remove("");
    return true;
}

// 加入"打开方式"应用列表(HKCU\...\Applications\Gaze.exe;键名须与 exe 同名)
inline bool registerOpenWith() {
    const QString exe = exeNative();
    QSettings r("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\Gaze.exe",
                QSettings::NativeFormat);
    r.setValue(".", gazeTr("Gaze 图片浏览器"));
    r.setValue("FriendlyAppName", QString::fromUtf8("Gaze"));
    QSettings c("HKEY_CURRENT_USER\\Software\\Classes\\Applications\\Gaze.exe\\shell\\open\\command",
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
