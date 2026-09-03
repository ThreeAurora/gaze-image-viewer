#pragma once
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include "settings.h"   // #106:shellMenu 读点统一走 AppSettings,别再自己拼 ini 路径
#include "i18n.h"
#include "constants.h"

// 系统集成:资源管理器右键菜单 / 打开方式注册 / 文件关联(HKCU,免管理员)
// 注:改关联后不发 SHCNE_ASSOCCHANGED(MinGW 里在 shlobj.h,要拖整个 COM
// 头);Explorer 对 HKCU\Software\Classes 是即用即读,新开菜单即可见。
namespace Integration {

// 文件关联(#204,2026-09-04 用户令)统一指向的 ProgId。
inline const QString kAssocProgId = QStringLiteral("Gaze.Image");

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

// ── 文件关联(#204):ProgId + 应用能力 + 逐扩展名登记 ──
// 关联范围 = IMAGE_EXTS ∪ RAW_EXTS(Gaze 能预览的静态图像全量)。视频刻意
// 不进:用户通常有专门播放器,绑走双击会惊扰(用户要就再加)。
// Windows 10 起"真正的默认"存在 FileExts\<ext>\UserChoice,带防篡改哈希,
// 程序直写无官方通路、第三方哈希破解一次系统更新就废 —— 绝不碰。微软钦定
// 的正路是下面三步,之后"默认"由 Windows 自己(带哈希)落笔:
//   ① ProgId Gaze.Image:默认值/图标/打开命令;
//   ② Software\Gaze\Capabilities + RegisteredApplications:Gaze 进入
//     系统设置→应用→默认应用 列表,"设为默认"一键绑全部类型;
//   ③ 每扩展名 OpenWithProgids:右键→打开方式 立即可选 Gaze(不等设默认)。
inline QStringList assocExtensions() {
    QStringList out;
    out.reserve(static_cast<int>(IMAGE_EXTS.size() + RAW_EXTS.size()));
    for (const QString& e : IMAGE_EXTS) out << e;
    for (const QString& e : RAW_EXTS) out << e;
    return out;
}

inline bool registerFileAssociations() {
    const QString exe = exeNative();
    QSettings p("HKEY_CURRENT_USER\\Software\\Classes\\Gaze.Image",
                QSettings::NativeFormat);
    p.setValue(".", gazeTr("Gaze 图片"));
    p.setValue("FriendlyTypeName", gazeTr("Gaze 图片"));
    QSettings ic("HKEY_CURRENT_USER\\Software\\Classes\\Gaze.Image\\DefaultIcon",
                 QSettings::NativeFormat);
    ic.setValue(".", exe + ",0");
    QSettings cm("HKEY_CURRENT_USER\\Software\\Classes\\Gaze.Image\\shell\\open\\command",
                 QSettings::NativeFormat);
    cm.setValue(".", "\"" + exe + "\" \"%1\"");
    bool ok = p.status() == QSettings::NoError
           && ic.status() == QSettings::NoError
           && cm.status() == QSettings::NoError;

    QSettings cap("HKEY_CURRENT_USER\\Software\\Gaze\\Capabilities",
                  QSettings::NativeFormat);
    cap.setValue("ApplicationName", QStringLiteral("Gaze"));
    cap.setValue("ApplicationDescription", gazeTr("轻量图片浏览器与管理器"));
    cap.setValue("ApplicationIcon", exe + ",0");
    const QStringList exts = assocExtensions();
    for (const QString& e : exts)
        cap.setValue("FileAssociations/" + e, kAssocProgId);
    ok = ok && cap.status() == QSettings::NoError;

    QSettings ra("HKEY_CURRENT_USER\\Software\\RegisteredApplications",
                 QSettings::NativeFormat);
    ra.setValue("Gaze", QStringLiteral("Software\\Gaze\\Capabilities"));
    ok = ok && ra.status() == QSettings::NoError;

    for (const QString& e : exts) {
        QSettings ow("HKEY_CURRENT_USER\\Software\\Classes\\" + e
                     + "\\OpenWithProgids", QSettings::NativeFormat);
        ow.setValue(kAssocProgId, QString());
        ok = ok && ow.status() == QSettings::NoError;
    }

    return ok;
}

// 只撤 Gaze 自己写入的登记:ProgId/Capabilities/RegisteredApplications 值
// /各扩展名下 Gaze.Image 这一条;扩展名键本身是系统的,一个都不删。
inline bool removeFileAssociations() {
    QSettings("HKEY_CURRENT_USER\\Software\\Classes\\Gaze.Image",
              QSettings::NativeFormat).remove("");
    QSettings("HKEY_CURRENT_USER\\Software\\Gaze",
              QSettings::NativeFormat).remove("");
    QSettings ra("HKEY_CURRENT_USER\\Software\\RegisteredApplications",
                 QSettings::NativeFormat);
    ra.remove("Gaze");
    bool ok = ra.status() == QSettings::NoError;
    for (const QString& e : assocExtensions()) {
        QSettings ow("HKEY_CURRENT_USER\\Software\\Classes\\" + e
                     + "\\OpenWithProgids", QSettings::NativeFormat);
        ow.remove(kAssocProgId);
        ok = ok && ow.status() == QSettings::NoError;
    }
    return ok;
}

inline bool isFileAssocRegistered() {
    QSettings ra("HKEY_CURRENT_USER\\Software\\RegisteredApplications",
                 QSettings::NativeFormat);
    return ra.contains("Gaze");
}

} // namespace Integration
