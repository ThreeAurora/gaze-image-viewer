#pragma once
#include <QObject>
#include <QSettings>
#include <QVariant>

// 单例 — 全局设置访问(便携 ini:exe 目录/xnnview.ini)
// section 划分照 XnView MP:General/Start/FileOps/Interface/Keyboard/Mouse/
//                          SwitchMode/Browser/FileList/Thumbs/Appearance/Viewer/
//                          Fullscreen/Cache/Integration
class AppSettings : public QObject {
    Q_OBJECT
public:
    static AppSettings& instance();

    // 通用存取(key 形如 "General/singleInstance")
    QVariant get(const QString& key, const QVariant& def = QVariant()) const;
    void     set(const QString& key, const QVariant& v);
    // 只落盘不广播 changed():仅用于"唯一读者是下次启动"的状态键
    // (Browser/lastDir、Browser/lastFile)。界面要即时跟随的设置必须用 set()。
    void     setPersist(const QString& key, const QVariant& v);
    // 只落盘不广播 changed():仅用于"唯一读者是下次启动"的状态键
    // (Browser/lastDir、Browser/lastFile)。界面要即时跟随的设置必须用 set()。
    void     setPersist(const QString& key, const QVariant& v);
    void     clearAll();   // 恢复默认(清空 ini,回到代码内默认值)

    QString iniPath() const;
    // ini 所在目录:缩略图库等随配置文件一起走(默认同为 exe 目录)
    QString dataDir() const;
    // #122 给设置页用的"下次启动会读哪里"预测(0 便携 / 1 %APPDATA% / 2 自定义目录)。
    // 换位置的提示必须说真话:值在改动当下就复制过去,生效是下次启动。
    static QString iniPathForLocation(int loc, const QString& customDir);

signals:
    void changed();

private:
    AppSettings();
    QSettings m_settings;
};
