#pragma once
#include <QString>

// ═══════════════════════════════════════════
// 目录前缀匹配 key/path 列时的 LIKE 转义(配 SQL 里的 ESCAPE '\')
//   不转义 = 把文件路径当模式用:'_' 是单字符通配、'%' 是通配、'\' 是转义符,
//   而这三样都是合法的 Windows 文件名字符。实测后果:
//   选 E:/my_photos/ 会连带删掉 E:/myXphotos/;目录名 "100%" 直接清空整张表。
// ═══════════════════════════════════════════
inline QString likePrefixPattern(const QString& dir) {
    QString out;
    out.reserve(dir.size() + 8);
    for (const QChar c : dir) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('%') || c == QLatin1Char('_'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out + QLatin1String("\\%");   // 串尾的通配才是"前缀匹配"的意思
}
