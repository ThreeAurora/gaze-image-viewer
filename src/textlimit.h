#pragma once
// ═══════════════════════════════════════════
// 文本预览截断(#111)
// 用户 2026-08-31 明令:「即使是打勾，也应该对行数过长的文本进行截断，
// 具体多少行不会卡，你必须有个数」。这个"数"是量出来的,不是拍的 ——
// 探针 cache/tmp/textbench{,.2,.3,.4,.5}.cpp 在本机 Qt 6.8.3 / QTextEdit
// (只读、420x700、默认按宽换行)实测:
//   行数几乎不是瓶颈:80000 行 ×65 字符 setPlainText 184ms、拖滚动条 3.4ms/帧
//   真正的雷是"没有空格的超长单行"(压缩 JS / JSON / base64 / 单行日志):
//     1 行    8KB →    20ms     1 行   64KB →   941ms
//     1 行   16KB →    71ms     1 行  128KB →  3705ms
//     1 行  512KB → 62390ms(62 秒界面彻底冻死 —— 旧代码正漏在这里)
//   单行越长代价按二次方增长(无处断词,换行片段表 O(n²));有空格时同长度仅 59ms。
// 因此三条上限一起生效,而不是只卡行数:
//   maxBytes 512KB(读多少)、maxLines 5000(显示多少)、maxLineChars 2000(每行显示多少)
// 三条同时顶满的最坏组合实测:
//   256 行 ×2000 无空格(=512KB) → set 7ms、逐帧渲染 worst 2.0ms
//   5000 行 ×102 无空格 (≈512KB) → set 10ms、worst 1.0ms
//   对照未加保护的 1 行 512KB     → set 62390ms
// ═══════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QFile>
#include "i18n.h"

namespace TextCut {

constexpr qint64 maxBytes     = 512 * 1024;
constexpr int    maxLines     = 5000;
constexpr int    maxLineChars = 2000;

struct Clip {
    QString text;        // 截断后的正文(不含提示行)
    qint64  shownBytes = 0;
    int     lines      = 0;   // 实际显示行数
    bool    byteCut    = false;   // 文件比读进来的部分长
    bool    lineCut    = false;   // 行数超过 maxLines
    int     longLines  = 0;   // 被按字符数切断的超长行条数
    qint64  totalBytes = 0;   // 文件总大小(用于说清还有多少没显示)
};

// 读文件头部 maxBytes 字节并解码 UTF-8。
// 边界上被切碎的 UTF-8 序列会被丢掉(否则末尾凭空多个 ?)。
inline QString readHead(QFile& f, bool* byteCut, qint64* total) {
    const qint64 size = f.size();
    if (total) *total = size;
    QByteArray raw = f.read(maxBytes);
    if (byteCut) *byteCut = size > raw.size();
    // 丢掉尾部不完整序列:回看最多 3 字节找序列首字节,长度不足就截掉
    int n = raw.size();
    for (int back = 1; back <= 4 && back <= n; ++back) {
        const uchar c = static_cast<uchar>(raw.at(n - back));
        if ((c & 0x80) == 0) break;                      // ASCII,序列完整
        if ((c & 0xC0) == 0xC0) {                        // 找到一个序列的首字节
            const int want = (c & 0xE0) == 0xE0 ? 2
                           : (c & 0xF0) == 0xF0 ? 3
                           : (c & 0xF8) == 0xF8 ? 4 : 1;
            if (back < want) n -= back;                  // 后续字节没读全 → 丢掉
            break;
        }
        if ((c & 0xC0) != 0x80) break;                   // 非法字节,交给 fromUtf8 处理
    }
    if (n < raw.size()) raw = raw.left(n);
    return QString::fromUtf8(raw);
}

// 按 maxLines / maxLineChars 截断(输入应是已 readHead 出来的文本)
inline Clip clip(const QString& raw, bool byteCut_, qint64 totalBytes_) {
    Clip out;
    out.byteCut    = byteCut_;
    out.totalBytes = totalBytes_;
    out.shownBytes = raw.toUtf8().size();

    const QStringList src = raw.split(QLatin1Char('\n'));
    const int keepLines = qMin(src.size(), maxLines);
    out.lineCut = src.size() > maxLines;
    out.lines   = keepLines;

    QStringList dst;
    dst.reserve(keepLines);
    for (int i = 0; i < keepLines; ++i) {
        QString line = src.at(i);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.size() > maxLineChars) {
            line = line.left(maxLineChars) + QStringLiteral("…");
            ++out.longLines;
        }
        dst << line;
    }
    out.text = dst.join(QLatin1Char('\n'));
    return out;
}

// 提示语(纯文本、无前导换行)。没有截任何东西时返回空串。
inline QString noticeOf(const Clip& c) {
    QStringList notes;
    if (c.lineCut)
        notes << gazeTr("行数超过 %1，仅显示前 %2 行").arg(maxLines).arg(c.lines);
    else if (c.byteCut)
        notes << gazeTr("文件 %1 KB，仅读取前 %2 KB")
                     .arg(c.totalBytes / 1024).arg(c.shownBytes / 1024);
    if (c.longLines > 0)
        notes << gazeTr("%1 行超过 %2 字符，每行只显示前 %2 字符")
                     .arg(c.longLines).arg(maxLineChars);
    return notes.isEmpty() ? QString()
                           : gazeTr("已截断：%1").arg(notes.join(QString::fromUtf8("；")));
}

// 纯文本预览用:截断 + 末尾追加提示行
inline QString apply(const QString& raw, bool byteCut_, qint64 totalBytes_) {
    const Clip c = clip(raw, byteCut_, totalBytes_);
    const QString n = noticeOf(c);
    return n.isEmpty() ? c.text : c.text + QStringLiteral("\n\n—— ") + n + QStringLiteral(" ——");
}

} // namespace TextCut
