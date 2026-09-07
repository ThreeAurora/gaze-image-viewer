#pragma once
#include <QIcon>
#include <QDir>
#include <QCoreApplication>
#include <QHash>
#include <QApplication>
#include <QStyle>

// 应用图标库:运行时从 exe 目录 assets/icons/ 加载
// (图标取自 Lucide 线性图标集,MIT 可商用分发:白色细描边现代线性风格,
//  圆头圆角、语义直观,由 tools/fetch_lucide_icons.py 下载并渲染成 96px)
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
