#pragma once
// EXIF 拍摄日期(DateTimeOriginal, tag 0x9003)轻量解析,供"EXIF 拍摄日期"排序。
// 只读文件头 64KB 找 APP1/Exif,不做完整元数据栈;未找到返回 0(调用方回退 mtime)。
#include <QString>
#include <QFile>
#include <QDateTime>

namespace ExifDate {

// 解析 TIFF 区(IFD0 → ExifIFD → 0x9003/0x9004/0x0132),off 为 TIFF 头起始
inline double parseTiff(const uchar* p, qint64 avail) {
    if (avail < 8) return 0;
    const bool le = (p[0] == 'I' && p[1] == 'I');
    if (!le && !(p[0] == 'M' && p[1] == 'M')) return 0;
    auto u16 = [le](const uchar* b) -> quint16 {
        return le ? (b[0] | (b[1] << 8)) : ((b[0] << 8) | b[1]);
    };
    auto u32 = [le](const uchar* b) -> quint32 {
        return le ? (b[0] | (b[1] << 8) | (b[2] << 16) | (quint32(b[3]) << 24))
                  : ((quint32(b[0]) << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
    };
    if (u16(p + 2) != 42) return 0;
    quint32 ifd0 = u32(p + 4);
    double result = 0;
    // 两轮:先 IFD0(找 ExifIFD 指针与 DateTime),再 ExifIFD(找拍摄日期)
    quint32 offsets[2] = { ifd0, 0 };
    for (int round = 0; round < 2 && offsets[round]; ++round) {
        quint32 off = offsets[round];
        if (off + 2 > (quint64)avail) break;
        const quint16 count = u16(p + off);
        if (off + 2 + count * 12ull > (quint64)avail) break;
        for (quint16 i = 0; i < count; ++i) {
            const uchar* e = p + off + 2 + i * 12;
            const quint16 tag = u16(e);
            const quint16 type = u16(e + 2);
            const quint32 cnt = u32(e + 4);
            if (round == 0 && tag == 0x8769) {          // Exif IFD 指针
                offsets[1] = u32(e + 8);
                continue;
            }
            // 日期标签(ASCII,20 字节 "YYYY:MM:DD HH:MM:SS\0")
            if (tag == 0x9003 || tag == 0x9004 || tag == 0x0132) {
                if (type != 2 || cnt < 19) continue;
                const quint32 valOff = u32(e + 8);   // 20 字节 ASCII 恒外置
                if (valOff + 19 > (quint64)avail) continue;
                QDateTime dt = QDateTime::fromString(
                    QString::fromLatin1((const char*)p + valOff, 19),
                    "yyyy:MM:dd HH:mm:ss");
                if (dt.isValid()) {
                    result = double(dt.toSecsSinceEpoch());
                    if (tag == 0x9003) return result;   // DateTimeOriginal 最优先
                }
            }
        }
    }
    return result;
}

inline double dateTimeOriginal(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    QByteArray head = f.read(65536);
    const uchar* p = (const uchar*)head.constData();
    const qint64 n = head.size();
    if (n < 4) return 0;
    // JPEG:扫 APP1 段找 "Exif\0\0"
    if (p[0] == 0xFF && p[1] == 0xD8) {
        qint64 i = 2;
        while (i + 4 <= n) {
            if (p[i] != 0xFF) { ++i; continue; }
            const uchar marker = p[i + 1];
            if (marker == 0xD8) { i += 2; continue; }            // SOI
            if (marker == 0xDA) break;                            // SOS:头结束
            if (i + 4 > n) break;
            const quint16 segLen = (p[i + 2] << 8) | p[i + 3];
            if (segLen < 2) break;
            if (marker == 0xE1 && i + 10 <= n
                && memcmp(p + i + 4, "Exif\0\0", 6) == 0)
                return parseTiff(p + i + 10, n - i - 10);
            i += 2 + segLen;
        }
        return 0;
    }
    // TIFF 本体(II*/MM*):直接解析
    return parseTiff(p, n);
}

} // namespace ExifDate
