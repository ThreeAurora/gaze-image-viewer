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
    void     clearAll();   // 恢复默认(清空 ini,回到代码内默认值)

    // ── 常用具名项(旧接口,保留) ──
    bool livePhotoAutoPlay() const;
    void setLivePhotoAutoPlay(bool on);

    int  thumbnailSize() const;
    void setThumbnailSize(int size);

    int  startupMode() const;
    void setStartupMode(int mode);
    QString startupPath() const;
    void setStartupPath(const QString& path);

    int  theme() const;
    void setTheme(int t);

    bool videoAutoPlay() const;
    void setVideoAutoPlay(bool on);

    QString iniPath() const;
    // ini 所在目录:缩略图库等随配置文件一起走(默认同为 exe 目录)
    QString dataDir() const;

signals:
    void changed();

private:
    AppSettings();
    QSettings m_settings;
};
