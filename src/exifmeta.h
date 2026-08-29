#pragma once
// ═══════════════════════════════════════════════════════════════
// 图片内嵌元数据读取(EXIF/TIFF + PNG 文本块 + WebP EXIF)。
// 与 exifdate.h 不重复:那个只取三个日期标签供排序,这个给面板列全表。
//
// 分两层是刻意的:
//   A 层(Raw):字节 → 数值/文本。与标签语义无关,可以独立验证。
//   B 层(read):标签号 → 中文名 + 人话格式。标签号全部由 A 层探针
//     对着 Windows Shell 的独立 EXIF 实现逐个核对后才写死,不凭记忆。
//     (凭记忆写标签号 = 把"快门"显示成"闪光灯"这类静默错值,比不显示更糟)
//
// 约束:
//   · 按段精确读取,读到的字节数就是可用长度;越界偏移一律丢弃,不补零不猜。
//   · 每个取值前都过 inRange —— 本仓库为 EXIF 越界读写崩过一次(#57)。
//   · 只在后台线程调用(纯计算 + 顺序读,不碰 GUI、不碰 QSettings)。
// ═══════════════════════════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <QFile>
#include <QByteArray>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace ExifMeta {

static constexpr int   kMaxFields   = 400;         // 病态文件兜底
static constexpr qint64 kMaxChunk   = 32 << 20;    // 单个元数据段最多读 32MB

// ───────────────────────────── A 层 ─────────────────────────────
struct Raw {
    QString  ifd;              // "IFD0" / "Exif" / "GPS"
    quint16  tag  = 0;
    quint16  type = 0;
    quint32  cnt  = 0;
    QString  text;             // ASCII/UTF-8 解出来的文本(非文本类型为空)
    QVector<double> nums;      // 数值(rational 已相除);非数值类型为空
    QByteArray bytes;          // 原始字节(UNDEFINED/BYTE 数组要看它)
};

class Tiff {
public:
    const uchar* p = nullptr;
    qint64 n = 0;
    bool le = false;

    bool ok() const { return p && n >= 8; }
    bool inRange(qint64 off, qint64 len) const {
        return len >= 0 && off >= 0 && len <= n && off <= n - len;
    }
    quint16 u16p(const uchar* b) const {
        return le ? quint16(b[0] | (b[1] << 8)) : quint16((quint16(b[0]) << 8) | b[1]);
    }
    quint32 u32p(const uchar* b) const {
        return le ? (quint32(b[0]) | (quint32(b[1]) << 8)
                    | (quint32(b[2]) << 16) | (quint32(b[3]) << 24))
                  : ((quint32(b[0]) << 24) | (quint32(b[1]) << 16)
                     | (quint32(b[2]) << 8) | quint32(b[3]));
    }
    quint16 u16(qint64 o) const { return u16p(p + o); }
    quint32 u32(qint64 o) const { return u32p(p + o); }
    quint32 uAt(const uchar* b, int i) const { return u32p(b + i * 4); }   // 从任意指针按端序取 u32
    quint16 uAt16(const uchar* b, int i) const { return u16p(b + i * 2); }
};

inline int typeSize(quint16 t) {
    switch (t) {
    case 1: case 2: case 6: case 7: return 1;      // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8:  return 2;                     // SHORT SLONG
    case 4: case 9: case 11: return 4;             // LONG SLONG FLOAT
    case 5: case 10: case 12: return 8;            // RATIONAL SRATIONAL DOUBLE
    default: return 0;                             // 未知类型:整条丢弃
    }
}

// 条目数据指针:总量 <=4 字节时值就在偏移字段里,否则是相对 TIFF 头的偏移
inline const uchar* valuePtr(const Tiff& t, const uchar* e, qint64* avail) {
    const quint16 type = t.u16p(e + 2);
    const quint32 cnt  = t.u32p(e + 4);
    const int sz = typeSize(type);
    if (sz <= 0) return nullptr;
    const quint64 total = quint64(cnt) * quint64(sz);
    if (total <= 4) { *avail = qint64(total); return e + 8; }
    const quint64 off = t.u32p(e + 8);
    if (!t.inRange(qint64(off), qint64(total))) return nullptr;   // 越界/被截断
    *avail = qint64(total);
    return t.p + off;
}

inline QString trimNul(QString s) {
    while (s.endsWith(QChar('\0'))) s.chop(1);
    return s.trimmed();
}

// UserComment/相关字段的字符集前缀:"ASCII\0\0\0" "UNICODE\0" "JIS\0\0\0\0\0" "\0\0\0\0\0\0\0\0"
inline QString decodedComment(const uchar* d, qint64 avail) {
    if (avail <= 8) return QString();
    const QByteArray head(reinterpret_cast<const char*>(d), 8);
    const uchar* body = d + 8;
    const qint64 len = avail - 8;
    if (head.startsWith("UNICODE"))
        return trimNul(QString::fromUtf16(reinterpret_cast<const char16_t*>(body), int(len / 2)));
    return trimNul(QString::fromUtf8(reinterpret_cast<const char*>(body), int(len)));
}

inline void fillRaw(const Tiff& t, Raw& r, const uchar* d, qint64 avail) {
    r.bytes = QByteArray(reinterpret_cast<const char*>(d), int(avail > 512 ? 512 : avail));
    switch (r.type) {
    case 2: {                                        // ASCII:NUL 结尾
        r.text = trimNul(QString::fromUtf8(reinterpret_cast<const char*>(d), int(avail)));
        break;
    }
    case 3: case 8:                                  // SHORT / SLONG
        for (quint32 i = 0; i < r.cnt; ++i) {
            const quint16 v = t.uAt16(d, int(i));
            r.nums << (r.type == 8 ? double(qint16(v)) : double(v));
        }
        break;
    case 4: case 9:                                  // LONG / SLONG
        for (quint32 i = 0; i < r.cnt; ++i) {
            const quint32 v = t.uAt(d, int(i));
            r.nums << (r.type == 9 ? double(qint32(v)) : double(v));
        }
        break;
    case 5: case 10:                                 // RATIONAL / SRATIONAL
        for (quint32 i = 0; i < r.cnt; ++i) {
            const quint32 a = t.uAt(d, int(i * 2));
            const quint32 b = t.uAt(d, int(i * 2 + 1));
            const double num = (r.type == 10) ? double(qint32(a)) : double(a);
            const double den = (r.type == 10) ? double(qint32(b)) : double(b);
            r.nums << (den == 0.0 ? 0.0 : num / den);
        }
        break;
    case 1: case 6: case 7:                          // BYTE / SBYTE / UNDEFINED
        for (qint64 i = 0; i < avail; ++i) r.nums << double(d[i]);
        // 版本号一类短数组其实是可读文本(0x9000 "0230");只在纯可打印时给出
        if (avail > 0 && avail <= 8) {
            bool printable = true;
            for (qint64 i = 0; i < avail; ++i)
                if (d[i] < 0x20 || d[i] > 0x7E) { printable = false; break; }
            if (printable)
                r.text = QString::fromLatin1(reinterpret_cast<const char*>(d), int(avail));
        }
        break;
    case 11:                                         // FLOAT
        for (quint32 i = 0; i < r.cnt; ++i) {
            const quint32 v = t.uAt(d, int(i));
            float f; std::memcpy(&f, &v, 4);
            r.nums << double(f);
        }
        break;
    case 12:                                         // DOUBLE
        for (quint32 i = 0; i < r.cnt; ++i) {
            const quint32 lo = t.uAt(d, int(i * 2)), hi = t.uAt(d, int(i * 2 + 1));
            quint64 v;
            if (t.le) v = quint64(lo) | (quint64(hi) << 32);
            else      v = (quint64(lo) << 32) | quint64(hi);
            double dd; std::memcpy(&dd, &v, 8);
            r.nums << dd;
        }
        break;
    default: break;
    }
}

// 走一个 IFD,收集条目;子 IFD 指针按 out 里的标签号识别后递归
inline bool parseIfdRaw(const Tiff& t, quint32 off, const QString& ifdName,
                        int depth, QList<Raw>& out) {
    if (depth > 2 || !t.inRange(off, 2) || out.size() >= kMaxFields) return false;
    const quint16 count = t.u16(off);
    if (!t.inRange(off, 2 + qint64(count) * 12 + 4)) return false;
    QVector<quint32> exifSub, gpsSub;
    for (quint16 i = 0; i < count; ++i) {
        const uchar* e = t.p + off + 2 + i * 12;
        const quint16 tag  = t.u16p(e);
        const quint16 type = t.u16p(e + 2);
        const quint32 cnt  = t.u32p(e + 4);
        if (typeSize(type) <= 0) continue;
        qint64 avail = 0;
        const uchar* d = valuePtr(t, e, &avail);
        if (!d) continue;                            // 越界/被截断:整条丢
        if (ifdName == QLatin1String("IFD0") && type == 4 && cnt == 1) {
            if (tag == 0x8769) { exifSub.append(t.u32p(e + 8)); continue; }
            if (tag == 0x8825) { gpsSub.append(t.u32p(e + 8));  continue; }
        }
        Raw r;
        r.ifd = ifdName;
        r.tag = tag; r.type = type; r.cnt = cnt;
        fillRaw(t, r, d, avail);
        out.append(r);
    }
    bool any = !out.isEmpty();
    for (quint32 o : exifSub) any |= parseIfdRaw(t, o, QStringLiteral("Exif"), depth + 1, out);
    for (quint32 o : gpsSub)  any |= parseIfdRaw(t, o, QStringLiteral("GPS"),  depth + 1, out);
    return any;
}

inline bool parseTiffRaw(const Tiff& t, QList<Raw>& out) {
    if (!t.ok() || t.u16(2) != 42) return false;     // 只认经典 EXIF TIFF(魔数 42)
    if (t.u16(0) != ('I' + ('I' << 8)) && t.u16(0) != ('M' + ('M' << 8))) return false;
    return parseIfdRaw(t, t.u32(4), QStringLiteral("IFD0"), 0, out);
}

inline Tiff tiffOf(const QByteArray& blk) {
    Tiff t;
    t.p = reinterpret_cast<const uchar*>(blk.constData());
    t.n = blk.size();
    t.le = (t.n >= 2 && t.p[0] == 'I' && t.p[1] == 'I');
    return t;
}

inline quint32 be32(const uchar* b) {
    return (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | b[3];
}

// JPEG:逐段走,APP1("Exif\0\0")喂 TIFF,COM 收成"注释",APP1(XMP) 跳过不装懂
inline void parseJpegRaw(QFile& f, QList<Raw>& out) {
    quint64 pos = 2;
    for (int guard = 0; guard < 512 && f.size() > 0; ++guard) {
        if (!f.seek(qint64(pos))) return;
        const QByteArray hdr = f.read(4);
        if (hdr.size() < 4) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        if (h[0] != 0xFF) { ++pos; continue; }       // 填充字节/丢同步:一步一挪
        const uchar marker = h[1];
        if (marker == 0xFF) { pos += 1; continue; }  // 连续 0xFF 填充
        if (marker == 0xD8) { pos += 2; continue; }
        if (marker == 0xD9 || marker == 0xDA) return;   // EOI / SOS:熵编码段开始
        const quint64 bodyLen = quint64((h[2] << 8) | h[3]) - 2;
        const quint64 body = pos + 2;
        if (marker == 0xE1 && bodyLen >= 12) {
            f.seek(qint64(body));
            const QByteArray head = f.read(10);
            if (head.size() == 10 && std::memcmp(head.constData(), "Exif\0\0", 6) == 0) {
                const qint64 len = qint64(bodyLen) - 6;
                f.seek(qint64(body + 6));
                Tiff t = tiffOf(f.read(int(qMin<qint64>(len, kMaxChunk))));
                parseTiffRaw(t, out);
            }
        } else if (marker == 0xFE && bodyLen > 0) {   // COM
            f.seek(qint64(body));
            const QByteArray c = f.read(int(qMin<qint64>(bodyLen, kMaxChunk)));
            Raw r; r.ifd = QStringLiteral("JPEG"); r.tag = 0xFE; r.type = 2;
            r.text = trimNul(QString::fromUtf8(c));
            if (!r.text.isEmpty()) out.append(r);
        }
        pos = body + bodyLen;
    }
}

inline void parsePngRaw(QFile& f, QList<Raw>& out) {
    if (!f.seek(8)) return;
    for (int guard = 0; guard < 1024; ++guard) {
        const qint64 hdrPos = f.pos();
        const QByteArray hdr = f.read(8);
        if (hdr.size() < 8) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        const quint64 len = be32(h);
        const char* ty = reinterpret_cast<const char*>(h + 4);
        const qint64 body = hdrPos + 8;
        if (std::memcmp(ty, "eXIf", 4) == 0) {
            if (!f.seek(body)) return;
            Tiff t = tiffOf(f.read(int(qMin<quint64>(len, kMaxChunk))));
            parseTiffRaw(t, out);
        } else if (std::memcmp(ty, "tEXt", 4) == 0 || std::memcmp(ty, "iTXt", 4) == 0) {
            if (!f.seek(body)) return;
            const QByteArray d = f.read(int(qMin<quint64>(len, kMaxChunk)));
            const int nul = d.indexOf('\0');
            if (nul > 0) {
                QString val;
                if (ty[0] == 't') {
                    val = QString::fromUtf8(d.constData() + nul + 1, d.size() - nul - 1);
                } else if (d.size() > nul + 3) {     // iTXt: key\0 compFlag compMethod lang\0 trans\0 text
                    const int l = d.indexOf('\0', nul + 3);
                    const int k = l < 0 ? -1 : d.indexOf('\0', l + 1);
                    if (k >= 0 && d[nul + 1] == 0)   // 压缩标志非 0 时不解(没有 zlib 原语)
                        val = QString::fromUtf8(d.constData() + k + 1, d.size() - k - 1);
                }
                val = val.trimmed();
                if (!val.isEmpty()) {
                    Raw r; r.ifd = QStringLiteral("PNG"); r.tag = 0xFE; r.type = 2;
                    r.text = QString::fromLatin1(d.constData(), nul) + QStringLiteral(" = ") + val;
                    out.append(r);
                }
            }
        } else if (std::memcmp(ty, "IEND", 4) == 0) return;
        if (!f.seek(body + qint64(len) + 4)) return; // 跳过 CRC 到下一块
    }
}

inline void parseWebpRaw(QFile& f, QList<Raw>& out) {
    qint64 pos = 12;                                  // RIFF / size / WEBP
    for (int guard = 0; guard < 256; ++guard) {
        if (!f.seek(pos)) return;
        const QByteArray hdr = f.read(8);
        if (hdr.size() < 8) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        const quint32 len = be32(h + 4);
        const qint64 body = pos + 8;
        if (std::memcmp(h, "EXIF", 4) == 0) {
            if (!f.seek(body)) return;
            Tiff t = tiffOf(f.read(int(qMin<quint64>(len, kMaxChunk))));
            parseTiffRaw(t, out);
        }
        pos = body + qint64(len) + (len & 1);         // 偶数对齐
    }
}

inline QList<Raw> readRaw(const QString& path) {
    QList<Raw> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray magic = f.read(12);
    if (magic.size() < 12) return out;
    const uchar* m = reinterpret_cast<const uchar*>(magic.constData());
    if (m[0] == 0xFF && m[1] == 0xD8) {
        parseJpegRaw(f, out);
    } else if ((m[0] == 'I' && m[1] == 'I' && m[2] == 42)
            || (m[0] == 'M' && m[1] == 'M' && m[2] == 0)) {
        f.seek(0);
        Tiff t = tiffOf(f.read(int(kMaxChunk)));
        if (m[0] == 'M' && m[1] == 'M') t.le = false;
        parseTiffRaw(t, out);
    } else if (std::memcmp(m, "\x89" "PNG\r\n\x1a\n", 8) == 0) {
        parsePngRaw(f, out);
    } else if (std::memcmp(m, "RIFF", 4) == 0 && std::memcmp(m + 8, "WEBP", 4) == 0) {
        parseWebpRaw(f, out);
    }
    return out;
}

// ───────────────────────────── B 层 ─────────────────────────────
// 标签号 → 分组/中文名 + 取值格式。见文件头注释:号必须由探针确认。
struct Field { QString group; QString name; QString value; };

// 占位:B 层在标签号探针核对完成后填入(exif_probe.cpp 的对照输出)
inline QList<Field> read(const QString& path) {
    QList<Field> out;
    const QList<Raw> raws = readRaw(path);
    for (const Raw& r : raws) {
        if (r.ifd == QLatin1String("JPEG") || r.ifd == QLatin1String("PNG")) {
            out.append(Field{ QString::fromUtf8("注释"),
                              QString::fromUtf8("文本"), r.text });
        }
    }
    return out;
}

} // namespace ExifMeta
