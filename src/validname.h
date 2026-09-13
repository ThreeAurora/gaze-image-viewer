#pragma once
#include <QString>
#include "i18n.h"

// ═══════════════════════════════════════════
// Windows 文件/目录名校验(重命名、新建文件夹、布局名共用一份口径)
//   分隔符是硬闸门:这几处旧实现都把输入直接 absolutePath()+"/"+name 拼进路径,
//   一个 "a/b" 或 "..\\x" 就能让"重命名"把文件静默搬到别的目录 —— 界面上
//   什么都不会发生,用户以为改名失败,实际文件已经离开。
//   返回空串 = 可用;否则返回可直接展示给用户的整句原因。
// ═══════════════════════════════════════════
inline QString invalidNameReason(const QString& name) {
    if (name.isEmpty())
        return gazeTr("名称不能为空");

    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name)
        if (bad.contains(c) || c.unicode() < 0x20)
            return gazeTr("名称不能包含以下字符:\n/ \\ : * ? \" < > | 以及控制字符");

    // Windows 会静默去掉结尾的点和空格。宁可当场拒绝,也不要"改了名却少了几个字符"
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return gazeTr("名称不能以点号或空格结尾(Windows 会静默去掉)");

    // 保留设备名:看点号前的那一段,CON.txt 同样建不出来
    const QString stem = name.section(QLatin1Char('.'), 0, 0).toUpper();
    static const char* devices[] = { "CON", "PRN", "AUX", "NUL",
                                     "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
                                     "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9" };
    for (const char* d : devices)
        if (stem == QString::fromLatin1(d))
            return gazeTr("“%1”是 Windows 保留设备名，不能用作名称").arg(stem);

    if (name.size() > 255)
        return gazeTr("名称过长(上限 255 个字符)");
    return {};
}

// 新旧路径仅大小写不同(Windows 惯用的"规范化大小写"改名):NTFS 允许,
// 存在性检查必须放行 —— QFileInfo::exists 大小写不敏感,命中的是文件自己,
// 会误报"目标名已存在"。重命名各入口共用一份口径
inline bool isCaseOnlyRename(const QString& oldPath, const QString& newPath) {
    return oldPath.compare(newPath, Qt::CaseInsensitive) == 0;
}
