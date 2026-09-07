#pragma once
#include <QIcon>
#include <QDir>
#include <QCoreApplication>
#include <QHash>
#include <QApplication>
#include <QStyle>

// 应用图标库:运行时从 exe 目录 assets/icons/ 加载
// (图标取自 Material Design Icons,Apache 2.0 可商用分发;由
//  tools/fetch_mdi_icons.py 拉取并着色、Qt 渲染成 48px PNG,无版权纠纷)
namespace IconLib {

inline const QString iconDir() {
    static QString dir = QCoreApplication::applicationDirPath() + "/assets/icons";
    return dir;
}

// name 不含扩展名,如 "cmd_filter"、"viewas"
inline QIcon appIcon(const QString& name) {
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it.value();
    QString path = iconDir() + "/" + name + ".png";
    QIcon ic;
    if (QFileInfo::exists(path)) ic = QIcon(path);
    if (ic.isNull()) ic = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    cache[name] = ic;
    return ic;
}

} // namespace IconLib
