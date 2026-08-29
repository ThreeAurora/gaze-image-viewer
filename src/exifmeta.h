#pragma once
// ═══════════════════════════════════════════════════════════════
// 图片内嵌元数据读取(EXIF/TIFF + PNG 文本块 + WebP EXIF)。
// 与 exifdate.h 不重复:那个只取三个日期标签供排序用,这个给面板列全表。
// 约束:
//   · 只读头部需要的段,不整文件载入;EXIF 段按段长精确读,
//     读到的字节数就是"可用长度",越界的偏移一律丢弃(不猜、不补零)。
//   · 每个取值前都过一遍 inRange —— 本仓库为 EXIF 越界读写崩过一次(#57)。
//   · 只在后台线程调用(纯计算 + 顺序读,不碰 GUI、不碰 QSettings)。
// ═══════════════════════════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QList>
#include <QFile>
#include <QByteArray>
#include <cstdint>
#include <cstring>

namespace ExifMeta {

struct Field {
    QString group;   // "图像" / "拍摄信息" / "相机" / "GPS" / "作者" / "注释" / "其他"
    QString name;    // 中文标签;未知标签用 "EXIF 0x9201"
    QString value;
};

static constexpr int kMaxFields = 400;   // 病态文件兜底:面板有 400 行也没人看

// ── TIFF 块读取器:偏移一律相对 TIFF 头(p),长度 n ──
class Tiff {
public:
    const uchar* p = nullptr;
    qint64 n = 0;
    bool le = false;

    bool ok() const { return p && n >= 8; }
    bool inRange(qint64 off, qint64 len) const {
        return off >= 0 && len >= 0 && len <= n && off <= n - len;
    }
    quint16 u16(qint64 o) const {
        return le ? quint16(p[o] | (p[o + 1] << 8))
                  : quint16((quint16(p[o]) << 8) | p[o + 1]);
    }
    qint16 i16(qint64 o) const { return qint16(u16(o)); }
    quint32 u32(qint64 o) const {
        return le ? (quint32(p[o]) | (quint32(p[o + 1]) << 8) | (quint32(p[o + 2]) << 16)
                   | (quint32(p[o + 3]) << 24))
                  : ((quint32(p[o]) << 24) | (quint32(p[o + 1]) << 16)
                     | (quint32(p[o + 2]) << 8) | quint32(p[o + 3]));
    }
    qint32 i32(qint64 o) const { return qint32(u32(o)); }
    float f32(qint64 o) const {
        quint32 v = u32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    double f64(qint64 o) const {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            le ? (v |= quint64(p[o + i]) << (8 * i)) : (v = (v << 8) | p[o + i]);
        double d;
        std::memcpy(&d, &v, 8);
        return d;
    }
};

inline int typeSize(quint16 t) {
    switch (t) {
    case 1: case 2: case 6: case 7: return 1;      // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8:  return 2;                     // SHORT SLONG
    case 4: case 9: case 11: return 4;             // LONG SLONG FLOAT
    case 5: case 10: return 8;                     // RATIONAL SRATIONAL
    case 12: return 8;                             // DOUBLE
    default: return 0;                             // 未知类型:整条丢弃
    }
}

// 条目数据指针:总量 <=4 字节时值就放在偏移字段里,否则是相对 TIFF 头的偏移
inline const uchar* valuePtr(const Tiff& t, const uchar* e, qint64* avail) {
    const quint16 type = t.u16(e + 2);
    const quint32 cnt  = t.u32(e + 4);
    const int sz = typeSize(type);
    if (sz <= 0) return nullptr;
    const quint64 total = quint64(cnt) * quint64(sz);
    if (total <= 4) { *avail = qint64(total); return e + 8; }
    const quint64 off = t.u32(e + 8);
    if (!t.inRange(qint64(off), qint64(total))) return nullptr;   // 越界/被截断
    *avail = qint64(total);
    return t.p + off;
}

inline QString asciiAt(const uchar* d, qint64 len) {
    // EXIF 规定 ASCII 以 NUL 结尾;相机偶尔不写 NUL 或写 UTF-8 中文,
    // 所以按 UTF-8 解(纯 ASCII 时两者逐字节相同)
    while (len > 0 && d[len - 1] == 0) --len;
    return QString::fromUtf8(reinterpret_cast<const char*>(d), int(len)).trimmed();
}

inline QString rationalAt(const Tiff& t, const uchar* d, qint64 avail, int i, bool sgn) {
    if (!d || avail < qint64((i + 1) * 8)) return QString();
    const qint64 o = i * 8;
    const double num = sgn ? double(t.i32(0)) : double(t.u32(0));   // placeholder,见下
    Q_UNUSED(num);
    auto at = [&](qint64 off) -> qint64 {
        return sgn ? qint64(t.i32(t.p ? (off) : (off))) : qint64(t.u32(off));
    };
    Q_UNUSED(at);
    // 注意:d 可能指向 TIFF 基址之外,所以这里的读取必须走 d 而不是 t.p
    auto rd = [&](qint64 off) -> quint32 {
        const uchar* b = d + off;
        return t.le ? (quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24))
                    : ((quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]));
    };
    const quint32 rawNum = rd(o), rawDen = rd(o + 4);
    const double num2 = sgn ? double(qint32(rawNum)) : double(rawNum);
    const double den2 = sgn ? double(qint32(rawDen)) : double(rawDen);
    if (den2 == 0.0) return QString();
    return QString::number(num2 / den2, 'g', 6);
}

struct Rat { double v = 0; bool ok = false; };

inline Rat ratAt(const Tiff& t, const uchar* d, qint64 avail, int i, bool sgn) {
    Rat r;
    if (!d || avail < qint64((i + 1) * 8)) return r;
    auto rd = [&](qint64 off) -> quint32 {
        const uchar* b = d + off;
        return t.le ? (quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24))
                    : ((quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]));
    };
    const quint32 a = rd(i * 8), b = rd(i * 8 + 4);
    const double num = sgn ? double(qint32(a)) : double(a);
    const double den = sgn ? double(qint32(b)) : double(b);
    if (den == 0.0) return r;
    r.v = num / den;
    r.ok = true;
    return r;
}

inline QString numText(double v) {
    double a = std::fabs(v - std::llround(v));
    if (a < 1e-9) return QString::number(qint64(std::llround(v)));
    QString s = QString::number(v, 'f', v < 1 ? 4 : 2);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s;
}

// 曝光时间:1/125 s 这种写法比 0.008 s 更贴近人眼预期
inline QString exposureText(double sec) {
    if (sec <= 0) return QString();
    if (sec >= 1) return numText(sec) + QString::fromUtf8(" 秒");
    const double inv = 1.0 / sec;
    const double rin = std::llround(inv);
    if (rin > 0 && std::fabs(inv - rin) < 0.06 * rin)
        return QStringLiteral("1/%1 秒").arg(qint64(rin));
    return QStringLiteral("1/%1 秒").arg(numText(inv));
}

// GPS 度分秒:三个 RATIONAL + 一个 ASCII 参考
inline QString gpsCoord(const Tiff& t, const uchar* d, qint64 avail, QChar ref) {
    if (!d || avail < 24) return QString();
    const Rat deg = ratAt(t, d, avail, 0, false);
    const Rat min = ratAt(t, d, avail, 1, false);
    const Rat sec = ratAt(t, d, avail, 2, false);
    if (!deg.ok || !min.ok || !sec.ok) return QString();
    double v = deg.v + min.v / 60.0 + sec.v / 3600.0;
    if (ref == 'S' || ref == 'W') v = -v;
    return QStringLiteral("%1°%2′%3″%4")
        .arg(int(deg.v)).arg(min.v, 0, 'f', 4).arg(sec.v, 0, 'f', 3).arg(ref);
}

// 方向/曝光程序等枚举值的中文注释(取值本身仍显示,括号里是解释)
inline QString orientText(quint32 v) {
    static const char* k[] = { "", "正常", "水平镜像", "旋转180°", "垂直镜像",
                               "左上为右", "顺时针90°", "右上为左", "逆时针90°" };
    return v >= 1 && v <= 8 ? QString::fromUtf8(k[v]) : QString();
}

inline QString meterModeText(quint32 v) {
    switch (v) {
    case 1: return QString::fromUtf8("未知");
    case 2: return QString::fromUtf8("平均");
    case 3: return QString::fromUtf8("点测光");
    case 4: return QString::fromUtf8("多重");
    case 5: return QString::fromUtf8("模式");
    case 6: return QString::fromUtf8("优先");
    case 7: return QString::fromUtf8("自选");
    case 8: return QString::fromUtf8("交替");
    default: return QString();
    }
}

inline QString exposureModeText(quint32 v) {
    switch (v) {
    case 0: return QString::fromUtf8("自动");
    case 1: return QString::fromUtf8("手动");
    case 2: return QString::fromUtf8("自动包围");
    default: return QString();
    }
}

inline QString whiteBalanceText(quint32 v) {
    return v == 1 ? QString::fromUtf8("手动")
         : v == 0 ? QString::fromUtf8("自动") : QString();
}

inline QString flashText(quint32 v) {
    QString s = (v & 1) ? QString::fromUtf8("已闪") : QString::fromUtf8("未闪");
    if (v & 16) s += QString::fromUtf8("·强制");
    if (v & 32) s += QString::fromUtf8("·未填");
    if (v & 64) s += QString::fromUtf8("·红眼");
    return s;
}

inline QString sceneTypeText(quint32 v) {
    return v == 1 ? QString::fromUtf8("直射日光") : v == 2 ? QString::fromUtf8("阴天")
         : v == 3 ? QString::fromUtf8("阴影") : v == 4 ? QString::fromUtf8("白炽灯")
         : v == 5 ? QString::fromUtf8("荧光灯") : v == 6 ? QString::fromUtf8("水族馆")
         : v == 7 ? QString::fromUtf8("闪光灯") : QString();
}

inline QString lightSourceText(quint32 v) {
    switch (v) {
    case 1: return QString::fromUtf8("日光");
    case 2: return QString::fromUtf8("荧光灯");
    case 3: return QString::fromUtf8("白炽灯");
    case 4: return QString::fromUtf8("闪光灯");
    case 9: return QString::fromUtf8("标准光 A");
    case 10: return QString::fromUtf8("标准光 B");
    case 11: return QString::fromUtf8("标准光 C");
    case 13: return QString::fromUtf8("D55");
    case 17: return QString::fromUtf8("D65");
    case 21: return QString::fromUtf8("F2");
    case 24: return QString::fromUtf8("标准光 F");
    default: return QString();
    }
}

// 纯结构性的标签(缩略图指针、色差分量表…)列出来只是噪音
inline bool isNoiseTag(quint16 tag) {
    switch (tag) {
    case 0x0100: case 0x0101:           // 子 IFD 里的宽高(与主图重复且常不是一回事)
    case 0x0102: case 0x0103: case 0x0106:
    case 0x0115: case 0x011C: case 0x011D: case 0x011E: case 0x011F:
    case 0x013F: case 0x0201: case 0x0202:
    case 0x020E: case 0x020F: case 0x0210: case 0x0211: case 0x0212: case 0x0213:
    case 0x021B:
    case 0x828D: case 0x828E: case 0x8649: case 0x8773: case 0x8828: case 0x8829:
    case 0x8830: case 0x927F:
    case 0xA005: case 0xA20C: case 0xA20E: case 0xA20F: case 0xA210: case 0xA214:
    case 0xA215: case 0xA217:
        return true;
    default: return false;
    }
}

struct TagInfo { const char* group; const char* name; };

// 标签 → 中文分组/名称(UTF-8 字面量)
inline TagInfo tagInfo(quint16 tag) {
    switch (tag) {
    case 0x010F: return { "\xe7\x9b\xb8\xe6\x9c\xba", "\xe5\x88\xb6\xe9\x80\xa0\xe5\x95\x86" };            // 相机/制造商
    case 0x0110: return { "\xe7\x9b\xb8\xe6\x9c\xba", "\xe5\x9e\x8b\xe5\x8f\xb7" };                        // 相机/型号
    case 0x0112: return { "\xe5\x9b\xbe\xe5\x83\x8f", "\xe6\x96\xb9\xe5\x90\x91" };                        // 图像/方向
    case 0x011A: case 0x0132: case 0x013B:
    case 0x0128:                                                                                           // 分辨率单位
        return { "\xe5\x9b\xbe\xe5\x83\x8f", tag == 0x011A ? "\xe6\xb0\xb4\xe5\xb9\xb3\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87"
               : tag == 0x011B ? "\xe5\x9e\x82\xe7\x9b\xb4\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87"
               : tag == 0x0128 ? "\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87\xe5\x8d\x95\xe4\xbd\x8d"
                               : "\xe4\xbf\xae\xe6\x94\xb9\xe6\x97\xb6\xe9\x97\xb4" };                      // 修改时间
    case 0x011B: return { "\xe5\x9b\xbe\xe5\x83\x8f", "\xe5\x9e\x82\xe7\x9b\xb4\xe5\x88\x86\xe8\xbe\xa8\xe7\x8e\x87" };
    case 0x0131: return { "\xe5\x85\xb6\xe4\xbb\x96", "\xe7\xbc\x96\xe8\xbe\x91\xe8\xbd\xaf\xe4\xbb\xb6" };  // 其他/编辑软件
    case 0x013D: return { "\xe5\x85\xb6\xe4\xbb\x96", "\xe5\x83\x8f\xe7\xb4\xa0\xe5\x8d\x95\xe4\xbd\x8d" };
    case 0x829A: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9b\x9d\xe5\x85\x89\xe6\x97\xb6\xe9\x97\xb4" };
    case 0x829D: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x85\x89\xe5\x9c\x88" };
    case 0x8822: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9c\x80\xe5\xb0\x8f\xe5\x85\x89\xe5\x9c\x88" };
    case 0x8827: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "ISO" };
    case 0x9003: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x8b\x8d\xe6\x91\x84\xe6\x97\xb6\xe9\x97\xb4" };
    case 0x9004: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x88\x9b\xe5\xbb\xba\xe6\x97\xb6\xe9\x97\xb4" };
    case 0x9201: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x85\x89\xe5\x9c\x88\xe5\x80\xbc(AV)" };
    case 0x9202: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\xbf\xab\xe9\x97\xa8\xe5\x80\xbc(TV)" };
    case 0x9204: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9b\x9d\xe5\x85\x89\xe8\xa1\xa5\xe5\x81\xbf" };
    case 0x9205: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe7\x94\xb5\xe6\x98\x86\xe8\xa1\xa5\xe5\x81\xbf" };
    case 0x9206: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x88\x9b\xe4\xbd\x9c\xe5\x8f\x96\xe6\x99\xaf" };
    case 0x9207: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9b\x9d\xe5\x85\x89\xe5\x88\x86\xe6\x8b\x8d" };
    case 0x9208: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x8b\x8d\xe6\x91\x84\xe5\x88\x86\xe7\xa7\x92" };
    case 0x9209: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x88\x9b\xe5\xbb\xba\xe5\x88\x86\xe7\xa7\x92" };
    case 0x920A: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe7\x84\xa6\xe8\xb7\x9d" };
    case 0x9214: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x8b\x8d\xe6\x91\x84\xe5\x9c\xba\xe6\x99\xaf" };
    case 0x927C: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe7\x94\xa8\xe6\x88\xb7\xe6\xb3\xa8\xe9\x87\x8a" };
    case 0xA001: return { "\xe5\x85\xb6\xe4\xbb\x96", "\xe8\x89\xb2\xe5\xbd\xa9\xe7\xa9\xba\xe9\x97\xb4" };
    case 0xA002: return { "\xe5\x9b\xbe\xe5\x83\x8f", "\xe5\x83\x8f\xe7\xb4\xa0\xe5\xae\xbd" };
    case 0xA003: return { "\xe5\x9b\xbe\xe5\x83\x8f", "\xe5\x83\x8f\xe7\xb4\xa0\xe9\xab\x98" };
    case 0xA20E: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x88\x86\xe5\xb8\x83\xe5\x87\xbd\xe6\x95\xb0" };
    case 0xA20F: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9c\x80\xe5\xb0\x8f\xe5\x85\x89\xe5\x9c\x88\xe5\x80\xbc" };
    case 0xA401: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x85\x89\xe6\x9d\x83\xe6\xb5\x8b\xe5\x85\x89\xe6\xa8\xa1\xe5\xbc\x8f" };
    case 0xA402: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe6\x9b\x9d\xe5\x85\x89\xe6\xa8\xa1\xe5\xbc\x8f" };
    case 0xA403: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe7\x99\xbd\xe5\xb9\xb3\xe8\xa1\xa1" };
    case 0xA404: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\xaf\xbc\xe6\xb5\x8b\xe5\x85\x89\xe6\xa0\x87\xe7\xa8\x8b" };
    case 0xA405: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x9c\xba\xe6\x99\xaf\xe7\xb1\xbb\xe5\x9e\x8b" };
    case 0xA406: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe5\x85\x89\xe6\xba\x90\xe7\xb1\xbb\xe5\x9e\x8b" };
    case 0xA420: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe9\x95\x9c\xe5\xa4\xb4\xe5\x9e\x8b\xe5\x8f\xb7" };
    case 0xA433: return { "\xe6\x8b\x8d\xe6\x91\x84\xe4\xbf\xa1\xe6\x81\xaf", "\xe9\x95\x9c\xe5\xa4\xb4\xe5\x88\xbb\xe5\x8f\xb7" };
    case 0x8825: return { "GPS", "GPS \xe4\xbf\xa1\xe6\x81\xaf" };
    case 0x0000: return { "GPS", "GPS \xe7\x89\x88\xe6\x9c\xac" };
    case 0x0001: return { "GPS", "\xe7\xba\xac\xe5\xba\xa6\xe5\x8f\x82\xe8\x80\x83" };
    case 0x0002: return { "GPS", "\xe7\xba\xac\xe5\xba\xa6" };
    case 0x0003: return { "GPS", "\xe7\xbb\x8f\xe5\xba\xa6\xe5\x8f\x82\xe8\x80\x83" };
    case 0x0004: return { "GPS", "\xe7\xbb\x8f\xe5\xba\xa6" };
    case 0x0005: return { "GPS", "\xe6\xb5\xb7\xe6\x8b\x94\xe5\x8f\x82\xe8\x80\x83" };
    case 0x0006: return { "GPS", "\xe6\xb5\xb7\xe6\x8b\x94" };
    case 0x0007: return { "GPS", "GPS \xe6\x97\xb6\xe9\x97\xb4" };
    case 0x0008: return { "GPS", "GPS \xe5\x9c\xb0\xe5\x90\x8d" };
    case 0x000B: return { "GPS", "GPS \xe5\xa4\x84\xe7\x90\x86\xe6\x96\xb9\xe5\xbc\x8f" };
    case 0x0012: return { "GPS", "GPS \xe6\xb5\x8b\xe9\x87\x8f\xe7\x82\xb9" };
    case 0x0013: return { "GPS", "GPS \xe5\x9c\xb0\xe5\x9b\xbe\xe5\x9f\xba\xe5\x87\x86" };
    case 0x001A: return { "GPS", "GPS \xe6\x97\xa5\xe6\x9c\x9f" };
    case 0x001D: return { "GPS", "GPS \xe8\xb7\x9d\xe7\xa6\xbb\xe5\x8d\x95\xe4\xbd\x8d" };
    case 0x001F: return { "GPS", "GPS \xe5\xa4\x84\xe7\x90\x86\xe8\xbd\xaf\xe4\xbb\xb6" };
    case 0x0102: return { "\xe5\x85\xb6\xe4\xbb\x96", "\xe5\x8e\x8b\xe7\xbc\xa9" };
    case 0x013E: return { "\xe4\xbd\x9c\xe8\x80\x85", "\xe5\x89\xaf\xe4\xbd\x9c\xe8\x80\x85" };
    case 0x013B: return { "\xe4\xbd\x9c\xe8\x80\x85", "\xe7\x89\x88\xe6\x9d\x83" };
    case 0x13B : return { "\xe4\xbd\x9c\xe8\x80\x85", "\xe7\x89\x88\xe6\x9d\x83" };
    default:     return { "", "" };
    }
}

// IFD 名称:同一个标签在 IFD0 与 ExifIFD 里含义不同(0x013B 版权 / 0x13B 只是巧合)
struct Reader {
    const Tiff* t;
    QList<Field>* out;
    bool gpsIfd = false;

    void add(const QString& g, const QString& k, const QString& v) const {
        if (v.isEmpty() || out->size() >= kMaxFields) return;
        out->append(Field{ g, k, v });
    }

    // 单个条目的可读文本;返回 false 表示这条不具展示价值
    bool render(quint16 tag, const uchar* d, qint64 avail, const Tiff& tt,
                quint16 type, quint32 cnt, QString& text) const {
        if (!d) return false;
        const bool isGps = gpsIfd;
        if (isGps) {
            if (tag == 0x0002 && avail >= 24) {
                text = gpsCoord(tt, d, avail, QChar()); return !text.isEmpty();
            }
            if (tag == 0x0004 && avail >= 24) {
                text = gpsCoord(tt, d, avail, QChar()); return !text.isEmpty();
            }
        }
        switch (type) {
        case 2: {                                   // ASCII
            QString s = asciiAt(d, avail);
            if (tag == 0x927C) {                   // UserComment 前 8 字节是字符集标记
                if (avail > 8) {
                    const QByteArray head(reinterpret_cast<const char*>(d), 8);
                    if (head.startsWith("UNICODE") || head.startsWith("UTF-8")) {
                        if (head.startsWith("UNICODE")) {
                            const uchar* u = d + 8;
                            const qint64 bytes = avail - 8;
                            text = QString::fromUtf16(
                                reinterpret_cast<const char16_t*>(u), int(bytes / 2)).trimmed();
                            return !text.isEmpty();
                        }
                        s = asciiAt(d + 8, avail - 8);
                    } else {
                        s = asciiAt(d + 8, avail - 8);
                    }
                } else return false;
            }
            if (tag == 0x0001 || tag == 0x0003) {   // GPS 参考:单字符
                text = s; return !text.isEmpty();
            }
            text = s;
            return !text.isEmpty();
        }
        case 1: case 7: {                           // BYTE / UNDEFINED
            if (cnt <= 4 && (tag == 0x0000 || tag == 0x9000 || tag == 0xa000 || tag == 0x0201)) {
                QByteArray b(reinterpret_cast<const char*>(d), int(cnt));
                text = QString::fromUtf8(b).trimmed();
                if (!text.isEmpty()) return true;
            }
            if (cnt <= 8) {                        // 版本/枚举类小数组
                QStringList parts;
                for (quint32 i = 0; i < cnt; ++i) parts << QString::number(d[i]);
                text = parts.join(',');
                return true;
            }
            text = QStringLiteral("(%1 字节二进制)").arg(cnt);
            return tag == 0x927E || tag == 0x927D;  // 仅 Audio/MakerNote 以体积形式列出
        }
        case 3: case 4: case 8: case 9: case 11: {  // 整数族
            if (cnt == 1 || cnt > 16) {
                quint64 v = 0;
                if (type == 3) v = (avail >= 2 ? (d[0] | (d[1] << 8)) : 0);
                else if (type == 8) v = quint16(t->i16(0));
                else if (type == 9 || type == 11) v = quint64(t->i32(0));
                else v = (avail >= 4 ? t->u32(0) : (d[0] | (d[1] << 8) | (d[2] << 16) | (quint64(d[3]) << 24)));
                Q_UNUSED(v);
            }
            QStringList parts;
            for (quint32 i = 0; i < cnt && i < 16; ++i) {
                const uchar* b = d + i * size_t(typeSize(type));
                QString one;
                switch (type) {
                case 3: one = QString::number(quint32((b[0] | (b[1] << 8)))); break;
                case 8: one = QString::number(qint32(qint16(b[0] | (b[1] << 8)))); break;
                case 4: one = QString::number(quint32(b[0] | (quint32(b[1]) << 8)
                                                      | (quint32(b[2]) << 16) | (quint32(b[3]) << 24))); break;
                case 9: one = QString::number(qint32(b[0] | (quint32(b[1]) << 8)
                                                     | (quint32(b[2]) << 16) | (quint32(b[3]) << 24))); break;
                case 11: { quint32 raw = b[0] | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
                           float f; std::memcpy(&f, &raw, 4); one = numText(f); break; }
                default: break;
                }
                parts << one;
            }
            text = parts.join(cnt > 1 ? QString::fromUtf8(", ") : QString());
            return !text.isEmpty();
        }
        case 5: case 10: {                          // RATIONAL / SRATIONAL
            const bool sgn = (type == 10);
            QStringList parts;
            for (quint32 i = 0; i < cnt && i < 16; ++i) {
                const Rat r = ratAt(*t, d, avail, int(i), sgn);
                if (!r.ok) return false;
                parts << numText(r.v);
            }
            if (cnt == 1 && t) text = parts.value(0);
            else text = parts.join(QString::fromUtf8(", "));
            return !text.isEmpty();
        }
        case 12: {
            QStringList parts;
            for (quint32 i = 0; i < cnt && i < 16; ++i) {
                const double v = t->f64(qint64(d - t->p) + i * 8);
                parts << numText(v);
            }
            text = parts.join(QString::fromUtf8(", "));
            return !text.isEmpty();
        }
        default: return false;
        }
    }
};

// 解析一个 IFD。返回 ExifIFD/GPSIFD 指针(通过出参)
inline void parseIfd(const Tiff& t, quint32 off, Reader& rd, int depth,
                     QList<QPair<quint16, quint32>>& subIfds) {
    if (depth > 3 || subIfds.size() > 8) return;
    if (!t.inRange(off, 2)) return;
    const quint16 count = t.u16(off);
    if (!t.inRange(off, 2 + qint64(count) * 12)) return;
    for (quint16 i = 0; i < count; ++i) {
        const uchar* e = t.p + off + 2 + i * 12;
        const quint16 tag  = t.u16(e);
        const quint16 type = t.u16(e + 2);
        const quint32 cnt  = t.u32(e + 4);
        const int sz = typeSize(type);
        if (sz <= 0) continue;
        if ((type == 4 || type == 9) && tag == 0x8769 && cnt == 1
            && !rd.gpsIfd) {                                   // Exif 子 IFD 指针
            subIfds.append(qMakePair(quint16(0x8769), t.u32(e + 8)));
            continue;
        }
        if (tag == 0x8825 && type == 4 && cnt == 1) {          // GPS 子 IFD 指针
            subIfds.append(qMakePair(quint16(0x8825), t.u32(e + 8)));
            continue;
        }
        if (isNoiseTag(tag) && !(rd.gpsIfd)) continue;
        qint64 avail = 0;
        const uchar* d = valuePtr(t, e, &avail);
        if (!d) continue;
        QString text;
        if (!rd.render(tag, d, avail, t, type, cnt, text)) continue;
        if (text.isEmpty()) continue;

        // 人话化:枚举值补注释,光圈/曝光/焦距加单位
        const bool inExif = !rd.gpsIfd;
        if (tag == 0x0112) {
            const Rat o = ratAt(t, d, avail, 0, false);
            text = orientText(cnt ? quint32(text.toUInt()) : 0);
            if (text.isEmpty()) text = QStringLiteral("未知(%1)").arg(o.v);
        } else if (tag == 0x829A && inExif) {
            const Rat v = ratAt(t, d, avail, 0, false);
            if (v.ok) text = exposureText(v.v);
        } else if (tag == 0x829D && inExif) {
            const Rat v = ratAt(t, d, avail, 0, false);
            if (v.ok) text = QStringLiteral("f/%1").arg(numText(v.v));
        } else if (tag == 0x8822 && inExif) {
            const Rat v = ratAt(t, d, avail, 0, false);
            if (v.ok) text = QStringLiteral("f/%1").arg(numText(v.v));
        } else if (tag == 0x920A && inExif) {
            const Rat v = ratAt(t, d, avail, 0, false);
            if (v.ok) text = numText(v.v) + QString::fromUtf8(" mm");
        } else if (tag == 0xA404 && inExif) {
            const Rat v = ratAt(t, d, avail, 0, false);
            if (v.ok) text = numText(v.v) + QString::fromUtf8(" m");
        } else if (tag == 0xA401) {
            const QString t2 = meterModeText(text.toUInt());
            if (!t2.isEmpty()) text += QStringLiteral(" (%1)").arg(t2);
        } else if (tag == 0xA402) {
            const QString t2 = exposureModeText(text.toUInt());
            if (!t2.isEmpty()) text += QStringLiteral(" (%1)").arg(t2);
        } else if (tag == 0xA403) {
            const QString t2 = whiteBalanceText(text.toUInt());
            if (!t2.isEmpty()) text += QStringLiteral(" (%1)").arg(t2);
        } else if (tag == 0xA405) {
            const QString t2 = sceneTypeText(text.toUInt());
            if (!t2.isEmpty()) text += QStringLiteral(" (%1)").arg(t2);
        } else if (tag == 0xA406) {
            const QString t2 = lightSourceText(text.toUInt());
            if (!t2.isEmpty()) text += QStringLiteral(" (%1)").arg(t2);
        } else if (tag == 0x9209 && inExif) {
            text += QStringLiteral(" (%1)").arg(text.toUInt() ? "开" : "关");
        } else if (tag == 0x9208 && inExif) {
            text = flashText(text.toUInt());
        } else if (tag == 0xA001) {
            const uint v = text.toUInt();
            text = v == 1 ? QString::fromUtf8("sRGB") : v == 2 ? QString::fromUtf8("\xc3\x97cc.662-1993")
                                                                : QStringLiteral("0x%1").arg(v, 4, 16, QChar('0'));
        }

        TagInfo ti = tagInfo(rd.gpsIfd ? tag : tag);
        QString group = QString::fromUtf8(ti.group);
        QString name  = QString::fromUtf8(ti.name);
        if (name.isEmpty()) {
            group = rd.gpsIfd ? QString::fromUtf8("GPS") : QString::fromUtf8("其他");
            name = QStringLiteral("EXIF 0x%1").arg(tag, 4, 16, QChar('0')).toUpper();
        }
        if (rd.gpsIfd) group = QString::fromUtf8("GPS");
        else if (group.isEmpty()) group = inExif ? QString::fromUtf8("拍摄信息")
                                                 : QString::fromUtf8("图像");
        // GPS 经纬度要配对参考字母:参考在前、坐标在后,面板读起来才对
        rd.add(group, name, text);
    }
}

inline void parseTiff(const Tiff& t, QList<Field>& out) {
    if (!t.ok()) return;
    if (t.u16(2) != 42) return;                     // 只认经典 EXIF TIFF
    const quint32 ifd0 = t.u32(4);
    Reader rd0{ &t, &out, false };
    QList<QPair<quint16, quint32>> subs;
    parseIfd(t, ifd0, rd0, 0, subs);
    // Exif 子 IFD 与 GPS 子 IFD 各自独立(都挂在 IFD0 上)
    for (const auto& s : subs) {
        Reader r{ &t, &out, s.first == 0x8825 };
        QList<QPair<quint16, quint32>> more;
        parseIfd(t, s.second, r, 1, more);
    }
}

// ── 容器:JPEG APP1 / TIFF / PNG eXIf+tEXt+iTXt / WebP EXIF ──
inline void appendField(QList<Field>& out, const QString& g, const QString& k, const QString& v) {
    if (v.isEmpty() || out.size() >= kMaxFields) return;
    out.append(Field{ g, k, v });
}

inline void parseJpeg(QFile& f, QList<Field>& out) {
    // f 已确认以 FFD8 开头
    quint64 pos = 2;
    for (int guard = 0; guard < 256; ++guard) {
        if (!f.seek(pos)) return;
        const QByteArray hdr = f.read(4);
        if (hdr.size() < 4) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        if (h[0] != 0xFF) { ++pos; continue; }
        const uchar marker = h[1];
        if (marker == 0xD8) { pos += 2; continue; }
        if (marker == 0xD9 || marker == 0xDA) return;            // EOI / SOS:头结束
        const quint16 segLen = quint16((h[2] << 8) | h[3]);
        if (segLen < 2) return;
        const quint64 body = pos + 2;                            // 段长度字段之后
        const quint64 bodyLen = quint64(segLen) - 2;
        if (marker == 0xE1) {                                    // APP1:Exif 或 XMP
            f.seek(qint64(body));
            const QByteArray head = f.read(12);
            if (head.size() >= 10
                && std::memcmp(head.constData(), "Exif\0\0", 6) == 0) {
                const qint64 tiffLen = qint64(bodyLen) - 6;
                if (tiffLen > 0) {
                    f.seek(qint64(body + 6));
                    QByteArray blk = f.read(int(tiffLen));
                    Tiff t;
                    t.p = reinterpret_cast<const uchar*>(blk.constData());
                    t.n = blk.size();
                    t.le = (t.n >= 2 && t.p[0] == 'I' && t.p[1] == 'I');
                    if (t.n >= 8 && !t.le && t.p[0] == 'M' && t.p[1] == 'M') t.le = false;
                    parseTiff(t, out);
                }
            }
        } else if (marker == 0xFE) {                             // COM: JPEG 注释
            f.seek(qint64(body));
            const QByteArray c = f.read(int(bodyLen));
            appendField(out, QString::fromUtf8("\xe6\xb3\xa8\xe9\x87\x8a"),   // 注释
                        QString::fromUtf8("\xe5\x9b\xbe\xe5\x83\x8f\xe6\xb3\xa8\xe9\x87\x8a"),  // 图像注释
                        QString::fromUtf8(c).trimmed());
        }
        pos = body + bodyLen;
    }
}

inline quint32 be32(const uchar* b) {
    return (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | b[3];
}
inline quint16 be16(const uchar* b) { return quint16((b[0] << 8) | b[1]); }

inline void parsePng(QFile& f, QList<Field>& out) {
    if (!f.seek(8)) return;
    for (int guard = 0; guard < 512; ++guard) {
        const QByteArray hdr = f.read(8);
        if (hdr.size() < 8) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        const quint32 len = be32(h);
        const QByteArray type = QByteArray::fromRawData(h + 4, 4);
        const qint64 body = qint64(8) + qint64(f.pos()) - 8;     // 数据段起点
        if (type == "eXIf") {
            f.seek(body);
            QByteArray blk = f.read(int(len));
            if (blk.size() >= 8) {
                Tiff t;
                t.p = reinterpret_cast<const uchar*>(blk.constData());
                t.n = blk.size();
                t.le = (t.p[0] == 'I' && t.p[1] == 'I');
                parseTiff(t, out);
            }
        } else if (type == "tEXt" || type == "iTXt") {
            f.seek(body);
            const QByteArray d = f.read(int(len));
            const int nul = d.indexOf('\0');
            if (nul > 0) {
                const QString key = QString::fromLatin1(d.constData(), nul);
                QString val;
                if (type == "tEXt") {
                    val = QString::fromUtf8(d.constData() + nul + 1, d.size() - nul - 1);
                } else if (d.size() > nul + 3) {
                    // iTXt: keyword\0 compFlag compMethod langTag\0 translated\0 text
                    int q = nul + 3;
                    int l = d.indexOf('\0', q); if (l < 0) return;
                    int k = d.indexOf('\0', l + 1); if (k < 0) return;
                    val = QString::fromUtf8(d.constData() + k + 1, d.size() - k - 1);
                }
                val = val.trimmed();
                if (!val.isEmpty())
                    appendField(out, QString::fromUtf8("\xe6\xb3\xa8\xe9\x87\x8a"), key, val);  // 注释
            }
        } else if (type == "IEND") return;
        f.seek(body + qint64(len) + 4);                          // 跳过 CRC
    }
}

inline void parseWebp(QFile& f, QList<Field>& out) {
    if (!f.seek(12)) return;                                     // RIFF/size/WEBP
    qint64 pos = 12;
    for (int guard = 0; guard < 256; ++guard) {
        if (!f.seek(pos)) return;
        const QByteArray hdr = f.read(8);
        if (hdr.size() < 8) return;
        const uchar* h = reinterpret_cast<const uchar*>(hdr.constData());
        const QByteArray type(h.constData(), 4);
        const quint32 len = be32(h + 4);
        const qint64 body = pos + 8;
        if (type == "EXIF") {
            f.seek(body);
            QByteArray blk = f.read(int(len));
            if (blk.size() >= 8) {
                Tiff t;
                t.p = reinterpret_cast<const uchar*>(blk.constData());
                t.n = blk.size();
                t.le = (t.p[0] == 'I' && t.p[1] == 'I');
                parseTiff(t, out);
            }
        }
        pos = body + qint64(len) + (len & 1);                    // 偶数对齐
    }
}

// 唯一对外入口:返回按分组顺序排好的键值表(不含文件自身属性,那些由面板补)
inline QList<Field> read(const QString& path) {
    QList<Field> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray magic = f.read(12);
    if (magic.size() < 12) return out;
    const uchar* m = reinterpret_cast<const uchar*>(magic.constData());
    if (m[0] == 0xFF && m[1] == 0xD8) {
        parseJpeg(f, out);
    } else if (m[0] == 'I' && m[1] == 'I' && m[4] == 42) {
        f.seek(0);
        QByteArray blk = f.read(4 * 1024 * 1024);
        Tiff t; t.p = reinterpret_cast<const uchar*>(blk.constData());
        t.n = blk.size(); t.le = true;
        parseTiff(t, out);
    } else if (m[0] == 'M' && m[1] == 'M' && m[6] == 42) {
        f.seek(0);
        QByteArray blk = f.read(4 * 1024 * 1024);
        Tiff t; t.p = reinterpret_cast<const uchar*>(blk.constData());
        t.n = blk.size(); t.le = false;
        parseTiff(t, out);
    } else if (std::memcmp(m, "\x89PNG\r\n\x1a\n", 8) == 0) {
        parsePng(f, out);
    } else if (std::memcmp(m, "RIFF", 4) == 0 && std::memcmp(m + 8, "WEBP", 4) == 0) {
        parseWebp(f, out);
    }
    return out;
}

} // namespace ExifMeta
